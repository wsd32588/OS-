#ifndef TINYOS_TEST_USER_MEMORY_FIXTURE_HPP
#define TINYOS_TEST_USER_MEMORY_FIXTURE_HPP

#include "host_support.hpp"
#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tinyos_test {

// PMM has global state: keep one active pool and run these tests sequentially.
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

class UserMemoryFixture {
private:
    PagePool<8> pool_;

public:
    static constexpr uint64_t code_address = UINT64_C(0x00400000);
    static constexpr uint64_t stack_address = code_address + SV39_PAGE_SIZE;
    static constexpr uint64_t code_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_X | SV39_PTE_A;
    static constexpr uint64_t stack_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D;

    Sv39PageTable *root = nullptr;
    unsigned long pages_after_root = 0;
    UserMemory memory{};

    UserMemoryFixture() {
        root = sv39_create_page_table();
        require(root != nullptr, "allocate borrowed root page table");
        pages_after_root = pmm_available_pages();
        memory.root = root;
        memory.code_page = pmm_alloc_page();
        memory.stack_page = pmm_alloc_page();
        require(memory.code_page != nullptr && memory.stack_page != nullptr,
                "allocate owned code and stack pages");
        memory.code_address = code_address;
        memory.stack_address = stack_address;

        require_equal(sv39_map_page(root, code_address,
            reinterpret_cast<uintptr_t>(memory.code_page), code_flags),
            SV39_OK, "map owned code page");
        require_equal(sv39_map_page(root, stack_address,
            reinterpret_cast<uintptr_t>(memory.stack_page), stack_flags),
            SV39_OK, "map owned stack page");
        require_equal(pmm_available_pages(), pages_after_root - 4,
                      "code, stack, L0 and L1 consume four pages");
        expect_mapping(code_address, memory.code_page, code_flags);
        expect_mapping(stack_address, memory.stack_page, stack_flags);
    }

    UserMemoryState snapshot() const {
        return capture_user_memory(memory);
    }

    void expect_released() const {
        require_equal(query_mapping(root, code_address).result,
                      SV39_ERR_NOT_MAPPED, "released code mapping is absent");
        require_equal(query_mapping(root, stack_address).result,
                      SV39_ERR_NOT_MAPPED, "released stack mapping is absent");
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
