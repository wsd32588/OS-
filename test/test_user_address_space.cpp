#include "mm/user_address_space.h"

#include "user_memory_fixture.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace {
using namespace tinyos_test;

constexpr uint64_t USER_ADDRESS = UINT64_C(0x40000000);
constexpr uint64_t USER_FLAGS = SV39_PTE_U | SV39_PTE_R | SV39_PTE_W |
    SV39_PTE_A | SV39_PTE_D;

struct PageSnapshot {
    const void* address;
    std::array<unsigned char, SV39_PAGE_SIZE> bytes;
    bool operator==(const PageSnapshot&) const = default;
};

struct AddressSpaceState {
    Sv39PageTable* root;
    const Sv39PageTable* kernel_root;
    MemoryDescriptor memory;
    unsigned long available_pages;
    unsigned long allocation_calls;
    CallCounts calls;
    uint64_t satp;
    std::vector<PageSnapshot> pages;
    bool operator==(const AddressSpaceState&) const = default;
};

void capture_page(std::vector<PageSnapshot>& pages, const void* address) {
    PageSnapshot snapshot{address, {}};
    std::memcpy(snapshot.bytes.data(), address, snapshot.bytes.size());
    pages.push_back(snapshot);
}

class AddressSpaceFixture {
private:
    PagePool<32> pool_;
    std::array<void*, 2> shared_content_{};
    std::vector<const Sv39PageTable*> shared_tables_;

public:
    Sv39PageTable* kernel_root = nullptr;
    unsigned long pages_after_template = 0;

    AddressSpaceFixture() {
        host_set_free_failure(nullptr);
        host_set_flush_failure(0);
        host_set_satp(0);
        kernel_root = sv39_create_page_table();
        require(kernel_root != nullptr, "allocate address-space template root");
        shared_tables_.push_back(kernel_root);

        const std::array<uint64_t, 2> addresses{
            UINT64_C(0x10000000), UINT64_C(0x80200000)};
        for (std::size_t i = 0; i < addresses.size(); ++i) {
            shared_content_[i] = pmm_alloc_page();
            require(shared_content_[i] != nullptr, "allocate shared mapping content");
            std::memset(shared_content_[i], static_cast<int>(0x31 + i), SV39_PAGE_SIZE);
            require_equal(sv39_map_page(kernel_root, addresses[i],
                reinterpret_cast<uintptr_t>(shared_content_[i]),
                SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D),
                SV39_OK, "prepare shared kernel mapping");
            const auto* level_one = reinterpret_cast<const Sv39PageTable*>(
                sv39_pte_physical_address(
                    kernel_root->entries[sv39_vpn_index(addresses[i], 2)]));
            const auto* level_zero = reinterpret_cast<const Sv39PageTable*>(
                sv39_pte_physical_address(
                    level_one->entries[sv39_vpn_index(addresses[i], 1)]));
            shared_tables_.push_back(level_one);
            shared_tables_.push_back(level_zero);
        }
        pages_after_template = pmm_available_pages();
    }

    ~AddressSpaceFixture() {
        host_set_free_failure(nullptr);
        host_set_flush_failure(0);
        host_set_satp(0);
    }

    void create(UserAddressSpace& space) const {
        require_equal(user_address_space_create(kernel_root, &space), SV39_OK,
                      "create a private user root");
        require(space.root != nullptr && space.root != kernel_root,
                "private root differs from borrowed template");
        require_equal(space.kernel_root, kernel_root, "keep borrowed kernel template");
    }

    void load_one_page(UserAddressSpace& space) const {
        void* page = pmm_alloc_page();
        require(page != nullptr, "allocate private user content");
        std::memset(page, 0xA5, SV39_PAGE_SIZE);
        require_equal(sv39_map_page(space.root, USER_ADDRESS,
            reinterpret_cast<uintptr_t>(page), USER_FLAGS), SV39_OK,
            "map private user content");
        space.user_memory = UserMemory{};
        space.user_memory.root = space.root;
        space.user_memory.page_count = 1;
        space.user_memory.pages[0] = UserMemoryPage{USER_ADDRESS, page};
    }

    std::vector<PageSnapshot> shared_snapshot() const {
        std::vector<PageSnapshot> result;
        for (const auto* table : shared_tables_) {
            capture_page(result, table);
        }
        for (const void* content : shared_content_) {
            capture_page(result, content);
        }
        return result;
    }

    AddressSpaceState snapshot(const UserAddressSpace& space) const {
        AddressSpaceState result{space.root, space.kernel_root,
            describe(space.user_memory), pmm_available_pages(),
            host_allocation_calls(), host_call_counts(), sv39_read_satp(),
            shared_snapshot()};
        if (space.root != nullptr && space.root != kernel_root) {
            capture_page(result.pages, space.root);
            const Sv39Pte branch = space.root->entries[1];
            if (branch != 0) {
                const auto* level_one = reinterpret_cast<const Sv39PageTable*>(
                    sv39_pte_physical_address(branch));
                capture_page(result.pages, level_one);
                const Sv39Pte leaf_branch = level_one->entries[0];
                if (leaf_branch != 0) {
                    capture_page(result.pages, reinterpret_cast<const void*>(
                        sv39_pte_physical_address(leaf_branch)));
                }
            }
        }
        if (space.user_memory.page_count != 0 &&
            space.user_memory.pages[0].physical_page != nullptr) {
            capture_page(result.pages, space.user_memory.pages[0].physical_page);
        }
        return result;
    }

    void require_no_shared_frees(std::size_t event_start) const {
        for (std::size_t i = event_start; i < host_call_events().size(); ++i) {
            const CallEvent& event = host_call_events()[i];
            if (event.kind != CallKind::Free) {
                continue;
            }
            for (const void* table : shared_tables_) {
                require(event.address != reinterpret_cast<uintptr_t>(table),
                        "never attempt to free borrowed kernel tables");
            }
            for (const void* page : shared_content_) {
                require(event.address != reinterpret_cast<uintptr_t>(page),
                        "never attempt to free shared kernel content");
            }
        }
    }
};

void require_empty(const UserAddressSpace& space) {
    require(space.root == nullptr && space.kernel_root == nullptr,
            "released outer roots are empty");
    require_equal(describe(space.user_memory), MemoryDescriptor{},
                  "released inner descriptor is empty");
}

uint64_t active_satp(const Sv39PageTable* root) {
    // Nonzero ASID must not change which root PPN is active.
    return (UINT64_C(8) << 60) | (UINT64_C(0xA55A) << 44) |
        (reinterpret_cast<uintptr_t>(root) >> SV39_PAGE_SHIFT);
}

void test_empty_and_root_only_release() {
    AddressSpaceFixture fixture;
    require_equal(user_address_space_release(nullptr), SV39_ERR_INVALID_ARGUMENT,
                  "NULL address-space release is rejected");
    UserAddressSpace space{};
    expect_no_change([&] { return fixture.snapshot(space); },
        [&] { return user_address_space_release(&space); }, SV39_OK);
    fixture.create(space);
    require_equal(pmm_available_pages(), fixture.pages_after_template - 1,
                  "create allocates exactly one private root");
    const auto shared = fixture.shared_snapshot();
    require_equal(user_address_space_release(&space), SV39_OK,
                  "release root-only space succeeds");
    require_empty(space);
    require_equal(pmm_available_pages(), fixture.pages_after_template,
                  "root-only release restores its private root page");
    require_equal(fixture.shared_snapshot(), shared, "root-only release preserves sharing");
    expect_no_change([&] { return fixture.snapshot(space); },
        [&] { return user_address_space_release(&space); }, SV39_OK);
}

void test_create_rejects_before_allocation() {
    AddressSpaceFixture fixture;
    UserAddressSpace output{};
    const Sv39Pte original = fixture.kernel_root->entries[0];
    fixture.kernel_root->entries[0] |= SV39_PTE_R;
    expect_no_change([&] { return fixture.snapshot(output); },
        [&] { return user_address_space_create(fixture.kernel_root, &output); },
        SV39_ERR_INVALID_ARGUMENT);
    fixture.kernel_root->entries[0] = original;

    fixture.kernel_root->entries[1] = original;
    expect_no_change([&] { return fixture.snapshot(output); },
        [&] { return user_address_space_create(fixture.kernel_root, &output); },
        SV39_ERR_INVALID_ARGUMENT);
    fixture.kernel_root->entries[1] = 0;

    fixture.create(output);
    expect_no_change([&] { return fixture.snapshot(output); },
        [&] { return user_address_space_create(fixture.kernel_root, &output); },
        SV39_ERR_INVALID_ARGUMENT);
    require_equal(user_address_space_release(&output), SV39_OK,
                  "release original output after rejected overwrite");
}

void test_loaded_release_preserves_shared_pages() {
    AddressSpaceFixture fixture;
    UserAddressSpace space{};
    fixture.create(space);
    fixture.load_one_page(space);
    require_equal(pmm_available_pages(), fixture.pages_after_template - 4,
                  "loaded space owns root, content and two private tables");
    require_equal(query_mapping(fixture.kernel_root, USER_ADDRESS).result,
                  SV39_ERR_NOT_MAPPED, "template has no private user mapping");
    const auto shared = fixture.shared_snapshot();
    const std::size_t event_start = host_call_events().size();
    host_set_satp(active_satp(fixture.kernel_root));
    require_equal(user_address_space_release(&space), SV39_OK,
                  "release nonactive loaded space succeeds");
    require_empty(space);
    require_equal(pmm_available_pages(), fixture.pages_after_template,
                  "loaded release restores all four owned pages");
    require_equal(fixture.shared_snapshot(), shared,
                  "loaded release preserves borrowed tables and content bytes");
    require_equal(sv39_read_satp(), active_satp(fixture.kernel_root),
                  "release does not switch active root");
    fixture.require_no_shared_frees(event_start);
}

void test_private_roots_isolate_the_same_virtual_address() {
    AddressSpaceFixture fixture;
    UserAddressSpace first{};
    UserAddressSpace second{};
    fixture.create(first);
    fixture.create(second);
    fixture.load_one_page(first);
    fixture.load_one_page(second);
    require(first.root != second.root, "simultaneous spaces have different private roots");
    require(first.root->entries[USER_ADDRESS_SPACE_ROOT_INDEX] !=
        second.root->entries[USER_ADDRESS_SPACE_ROOT_INDEX],
        "simultaneous spaces have different user page-table branches");
    require_equal(pmm_available_pages(), fixture.pages_after_template - 8,
                  "two spaces each own a root, content and two private tables");

    const Mapping first_mapping = query_mapping(first.root, USER_ADDRESS);
    const Mapping second_mapping = query_mapping(second.root, USER_ADDRESS);
    require_equal(first_mapping.result, SV39_OK, "first space maps the shared user VA");
    require_equal(second_mapping.result, SV39_OK, "second space maps the same user VA");
    require(first_mapping.physical_address != second_mapping.physical_address,
            "the same user VA translates to different physical pages");
    require_equal(query_mapping(fixture.kernel_root, USER_ADDRESS).result,
                  SV39_ERR_NOT_MAPPED, "template has neither private user mapping");

    const auto shared = fixture.shared_snapshot();
    const auto second_before_write = fixture.snapshot(second);
    auto* first_bytes = reinterpret_cast<unsigned char*>(first_mapping.physical_address);
    first_bytes[0] = 0x12;
    first_bytes[SV39_PAGE_SIZE - 1] = 0x34;
    require_equal(fixture.snapshot(second), second_before_write,
                  "writing the first translation preserves the second space and shared pages");

    host_set_satp(active_satp(second.root));
    auto expected_second = fixture.snapshot(second);
    const std::size_t event_start = host_call_events().size();
    require_equal(user_address_space_release(&first), SV39_OK,
                  "release first space while the second root remains active");
    require_empty(first);
    expected_second.available_pages += 4;
    expected_second.calls.unmap += 1;
    expected_second.calls.flush += 1;
    expected_second.calls.free += 4;
    require_equal(fixture.snapshot(second), expected_second,
                  "first release preserves the second descriptor, tables, content and active root");
    require_equal(query_mapping(second.root, USER_ADDRESS), second_mapping,
                  "surviving space retains its original translation");
    auto* second_bytes = reinterpret_cast<unsigned char*>(second_mapping.physical_address);
    require_equal(second_bytes[0], static_cast<unsigned char>(0xA5),
                  "surviving content retains its initial value");
    second_bytes[0] = 0x56;
    require_equal(second_bytes[0], static_cast<unsigned char>(0x56),
                  "surviving translation remains writable");

    host_set_satp(active_satp(fixture.kernel_root));
    require_equal(user_address_space_release(&second), SV39_OK,
                  "release second space after selecting the kernel root");
    require_empty(second);
    require_equal(pmm_available_pages(), fixture.pages_after_template,
                  "both private spaces return all eight owned pages");
    require_equal(fixture.shared_snapshot(), shared,
                  "both releases preserve every shared table and content page");
    fixture.require_no_shared_frees(event_start);
}

void test_active_root_rejected_without_changes() {
    AddressSpaceFixture fixture;
    UserAddressSpace space{};
    fixture.create(space);
    host_set_satp(active_satp(space.root));
    expect_no_change([&] { return fixture.snapshot(space); },
        [&] { return user_address_space_release(&space); },
        SV39_ERR_INVALID_ARGUMENT);

    host_set_satp(0);
    fixture.load_one_page(space);
    host_set_satp(active_satp(space.root));
    expect_no_change([&] { return fixture.snapshot(space); },
        [&] { return user_address_space_release(&space); },
        SV39_ERR_INVALID_ARGUMENT);
    host_set_satp(active_satp(fixture.kernel_root));
    require_equal(user_address_space_release(&space), SV39_OK,
                  "release succeeds after another root becomes active");
}

void test_root_free_failure_preserves_retry_state() {
    AddressSpaceFixture fixture;
    UserAddressSpace space{};
    fixture.create(space);
    fixture.load_one_page(space);
    Sv39PageTable* owned_root = space.root;
    const auto shared = fixture.shared_snapshot();
    host_set_free_failure(owned_root);
    require_equal(user_address_space_release(&space), SV39_ERR_PAGE_FREE_FAILED,
                  "private root free failure is reported");
    host_set_free_failure(nullptr);
    require_equal(space.root, owned_root, "failed root free preserves private ownership");
    require_equal(space.kernel_root, fixture.kernel_root,
                  "failed root free preserves borrowed template");
    require_equal(describe(space.user_memory), MemoryDescriptor{},
                  "root free failure retains already completed inner release");
    require_equal(owned_root->entries[1], UINT64_C(0),
                  "root-only retry has no remaining private branch");
    require_equal(pmm_available_pages(), fixture.pages_after_template - 1,
                  "only private root remains allocated after failed free");

    const CallCounts before = host_call_counts();
    require_equal(user_address_space_release(&space), SV39_OK,
                  "root-only retry succeeds");
    require_equal(host_call_counts(), CallCounts{before.unmap, before.flush, before.free + 1},
                  "root-only retry only frees the remaining root");
    require_equal(pmm_available_pages(), fixture.pages_after_template,
                  "retry restores the last owned page");
    require_equal(fixture.shared_snapshot(), shared, "retry preserves kernel sharing");
    require_empty(space);
}

void test_invalid_outer_ownership_is_rejected() {
    AddressSpaceFixture fixture;
    UserAddressSpace space{};
    fixture.create(space);
    fixture.load_one_page(space);

    const UserMemory original = space.user_memory;
    space.user_memory.root = fixture.kernel_root;
    expect_no_change([&] { return fixture.snapshot(space); },
        [&] { return user_address_space_release(&space); },
        SV39_ERR_INVALID_ARGUMENT);
    space.user_memory = original;

    space.user_memory.pages[0].virtual_address = UINT64_C(0x10000000);
    expect_no_change([&] { return fixture.snapshot(space); },
        [&] { return user_address_space_release(&space); },
        SV39_ERR_INVALID_ARGUMENT);
    space.user_memory = original;

    for (Sv39PageTable* root : {space.root, fixture.kernel_root}) {
        space.user_memory.pages[0].physical_page = root;
        expect_no_change([&] { return fixture.snapshot(space); },
            [&] { return user_address_space_release(&space); },
            SV39_ERR_INVALID_ARGUMENT);
        space.user_memory = original;
    }

    require_equal(user_address_space_release(&space), SV39_OK,
                  "release succeeds after restoring original ownership");
}
} // namespace

int test_user_address_space() {
    return run_case("address space: NULL, repeated empty and root-only release",
                    test_empty_and_root_only_release) &&
        run_case("address space: invalid template and nonempty output reject before allocation",
                 test_create_rejects_before_allocation) &&
        run_case("address space: loaded release preserves borrowed kernel tables and bytes",
                 test_loaded_release_preserves_shared_pages) &&
        run_case("address space: simultaneous roots isolate the same VA through write and release",
                 test_private_roots_isolate_the_same_virtual_address) &&
        run_case("address space: active root with ASID rejects without mutation",
                 test_active_root_rejected_without_changes) &&
        run_case("address space: failed root free preserves root-only retry",
                 test_root_free_failure_preserves_retry_state) &&
        run_case("address space: wrong root, shared VA and root-page ownership reject",
                 test_invalid_outer_ownership_is_rejected) ? 0 : -1;
}
