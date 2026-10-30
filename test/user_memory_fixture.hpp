#ifndef TINYOS_TEST_USER_MEMORY_FIXTURE_HPP
#define TINYOS_TEST_USER_MEMORY_FIXTURE_HPP

#include "host_support.hpp"
#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tinyos_test {

// PMM has global state: keep one active pool and run these tests sequentially.
/*
 * GCC -O2 会把 PagePool 构造内联进 fixture，并误报
 * “‘fixture’ may be used uninitialized”。
 * storage_ 已经由 storage_{} 值初始化，这里只屏蔽这条已知误报；
 * Clang 不认识 GCC 的 -Wmaybe-uninitialized，所以显式排除。
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

template <std::size_t PageCount>
class PagePool {
public:
    PagePool() {
        static_assert(PageCount > 0);
        require_equal(pmm_init(storage_.data(), storage_.data() + storage_.size()),
                      0, "initialize host page pool");
        require_equal(pmm_available_pages(), PageCount, "host page pool capacity");
    }

    PagePool(const PagePool &) = delete;
    PagePool &operator=(const PagePool &) = delete;

private:
    alignas(SV39_PAGE_SIZE) std::array<uint8_t, PageCount * SV39_PAGE_SIZE> storage_{};
};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

class UserMemoryFixture {
private:
    PagePool<8> pool_;

public:
    static constexpr uint64_t code_address = UINT64_C(0x00400000);
    static constexpr uint64_t stack_address = code_address + SV39_PAGE_SIZE;
    static constexpr uint64_t data_address = stack_address + 2 * SV39_PAGE_SIZE;
    static constexpr uint64_t code_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_X | SV39_PTE_A;
    static constexpr uint64_t stack_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D;
    static constexpr uint64_t data_falgs = SV39_PTE_U | SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D;
    Sv39PageTable *root = nullptr;
    unsigned long pages_after_root = 0;
    UserMemory memory{};
    std::size_t data_page_count_ = 0;

    explicit UserMemoryFixture(bool with_data = false,
                               std::size_t data_page_count = 1) {
        data_page_count_ = with_data ? data_page_count : 0;
        require(data_page_count_ <= USER_MEMORY_MAX_PAGE - 2,
                "fixture content count fits descriptor");
        root = sv39_create_page_table();
        require(root != nullptr, "allocate borrowed root page table");
        pages_after_root = pmm_available_pages();
        memory.root = root;
        memory.page_count = 2 + data_page_count_;
        memory.pages[0].virtual_address = code_address;
        memory.pages[1].virtual_address = stack_address;
        for (std::size_t i = 0; i < data_page_count_; ++i) {
            memory.pages[2 + i].virtual_address = data_address + i * SV39_PAGE_SIZE;
        }
        for (std::size_t i = 0; i < memory.page_count; ++i) {
            UserMemoryPage& page = memory.pages[i];
            page.physical_page = pmm_alloc_page();
            require(page.physical_page != nullptr, "allocate owned user content page");
        }
        for (std::size_t i = 0; i < memory.page_count; ++i) {
            const UserMemoryPage& page = memory.pages[i];
            const uint64_t flags = i == 0 ? code_flags : stack_flags;
            require_equal(sv39_map_page(root, page.virtual_address,
                reinterpret_cast<uintptr_t>(page.physical_page), flags),
                SV39_OK, "map owned user content page");
            expect_mapping(page.virtual_address, page.physical_page, flags);
        }
        require_equal(pmm_available_pages(),
                      pages_after_root - (memory.page_count + 2),
                      "content pages and two intermediate tables are allocated");
    }

    UserMemoryState snapshot() const {
        return capture_user_memory(memory);
    }

    void expect_released() const {
        require_equal(query_mapping(root, code_address).result,
                      SV39_ERR_NOT_MAPPED, "released code mapping is absent");
        require_equal(query_mapping(root, stack_address).result,
                      SV39_ERR_NOT_MAPPED, "released stack mapping is absent");
        for (std::size_t i = 0; i < data_page_count_; ++i) {
            require_equal(query_mapping(root, data_address + i * SV39_PAGE_SIZE).result,
                          SV39_ERR_NOT_MAPPED, "released data mapping is absent");
        }
        require_equal(pmm_available_pages(), pages_after_root, "owned pages restored");
        require_equal(snapshot().memory, MemoryDescriptor{}, "descriptor is empty");
        require_equal(root->entries[sv39_vpn_index(code_address, 2)],
                      UINT64_C(0), "empty user page-table branch is removed");
    }

private:
    void expect_mapping(uint64_t address, void *page, uint64_t flags) const {
        const Mapping mapping = query_mapping(root, address);
        require_equal(mapping.result, SV39_OK, "prepared mapping exists");
        require_equal(mapping.physical_address, reinterpret_cast<uintptr_t>(page),
                      "prepared mapping points to owned page");
        require_equal(mapping.flags, flags | SV39_PTE_V, "prepared mapping permissions");
    }
};

} // namespace tinyos_test

#endif
