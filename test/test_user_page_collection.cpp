#include "user_memory_fixture.hpp"

#include <array>
#include <string_view>

namespace {
using namespace tinyos_test;

void test_multi_page_release() {
    UserMemoryFixture fixture(true, 2);
    const auto before = fixture.snapshot();
    const std::size_t event_start = host_call_events().size();
    require_equal(fixture.memory.page_count, 4U, "four content pages are owned");
    require_equal(user_memory_release(&fixture.memory), SV39_OK,
                  "release all content pages");
    require_equal(pmm_available_pages(), before.available_pages + 6,
                  "four content pages and two tables are restored");
    require_equal(host_call_counts(),
                  CallCounts{before.calls.unmap + 4, before.calls.flush + 4,
                             before.calls.free + 6},
                  "release visits all four records");
    unsigned int unmaps = 0;
    unsigned int flushes = 0;
    for (std::size_t i = event_start; i < host_call_events().size(); ++i) {
        const CallEvent& event = host_call_events()[i];
        if (event.kind == CallKind::Unmap) {
            ++unmaps;
        } else if (event.kind == CallKind::Flush) {
            require_equal(unmaps, 4U, "all unmaps precede every flush");
            ++flushes;
        } else {
            require_equal(flushes, 4U, "all flushes precede every free");
            require(event.address != reinterpret_cast<uintptr_t>(fixture.root),
                    "borrowed root is never freed");
        }
    }
    fixture.expect_released();
}

void test_second_data_free_failure_retry() {
    UserMemoryFixture fixture(true, 2);
    const auto original = fixture.snapshot();
    const std::size_t event_start = host_call_events().size();
    void* failed_page = fixture.memory.pages[3].physical_page;
    host_set_free_failure(failed_page);
    const int first_result = user_memory_release(&fixture.memory);
    host_set_free_failure(nullptr);
    require_equal(first_result, SV39_ERR_PAGE_FREE_FAILED,
                  "second data page free failure is reported");

    auto expected = original.memory;
    for (std::size_t i = 0; i < 3; ++i) {
        expected.pages[i].physical_page = nullptr;
    }
    require_equal(describe(fixture.memory), expected,
                  "partial release retains count, all VAs and remaining PA");
    require_equal(pmm_available_pages(), original.available_pages + 3,
                  "first three content pages were freed once");
    require_equal(host_call_counts(),
                  CallCounts{original.calls.unmap + 4, original.calls.flush + 4,
                             original.calls.free + 4},
                  "failure occurs after all mappings were revoked and flushed");
    for (std::size_t i = 0; i < original.memory.page_count; ++i) {
        require_equal(query_mapping(fixture.root,
                      original.memory.pages[i].virtual_address).result,
                      SV39_ERR_NOT_MAPPED, "every owned leaf is absent");
    }

    require_equal(user_memory_release(&fixture.memory), SV39_OK,
                  "retry releases second data page and empty tables");
    fixture.expect_released();
    require_equal(host_call_counts(),
                  CallCounts{original.calls.unmap + 8, original.calls.flush + 8,
                             original.calls.free + 7},
                  "retry does not free the first three content pages again");
    for (std::size_t page_index = 0; page_index < original.memory.page_count;
         ++page_index) {
        unsigned int successful_frees = 0;
        unsigned int failed_frees = 0;
        const uintptr_t address = reinterpret_cast<uintptr_t>(
            original.memory.pages[page_index].physical_page);
        for (std::size_t i = event_start; i < host_call_events().size(); ++i) {
            const CallEvent& event = host_call_events()[i];
            if (event.kind == CallKind::Free && event.address == address) {
                if (event.result == 0) {
                    ++successful_frees;
                } else {
                    ++failed_frees;
                }
            }
        }
        require_equal(successful_frees, 1U, "each owned PA is freed exactly once");
        require_equal(failed_frees, page_index == 3 ? 1U : 0U,
                      "only second data PA has a failed free attempt");
    }
}

void test_duplicate_physical_ownership() {
    UserMemoryFixture fixture;
    void* original_stack = fixture.memory.pages[1].physical_page;
    void* code = fixture.memory.pages[0].physical_page;
    require_equal(sv39_unmap_page(fixture.root, fixture.stack_address), SV39_OK,
                  "remove stack mapping before aliasing its PA");
    require_equal(sv39_map_page(fixture.root, fixture.stack_address,
                  reinterpret_cast<uintptr_t>(code), fixture.stack_flags), SV39_OK,
                  "both VAs map the same PA with matching descriptors");
    fixture.memory.pages[1].physical_page = code;
    const std::size_t events = host_call_events().size();
    expect_no_change([&] { return fixture.snapshot(); },
                     [&] { return user_memory_release(&fixture.memory); },
                     SV39_ERR_INVALID_ARGUMENT);
    require_equal(host_call_events().size(), events,
                  "duplicate PA is rejected before any mutation");

    require_equal(sv39_unmap_page(fixture.root, fixture.stack_address), SV39_OK,
                  "remove duplicate mapping");
    require_equal(sv39_map_page(fixture.root, fixture.stack_address,
                  reinterpret_cast<uintptr_t>(original_stack), fixture.stack_flags),
                  SV39_OK, "restore independent stack ownership");
    fixture.memory.pages[1].physical_page = original_stack;
    require_equal(user_memory_release(&fixture.memory), SV39_OK,
                  "release repaired descriptor");
    fixture.expect_released();
}

void test_invalid_counts() {
    UserMemoryFixture fixture;
    const UserMemory original = fixture.memory;
    for (const std::size_t count : {std::size_t{0},
                                   std::size_t{USER_MEMORY_MAX_PAGE + 1}}) {
        fixture.memory.page_count = count;
        const std::size_t events = host_call_events().size();
        expect_no_change([&] { return fixture.snapshot(); },
                         [&] { return user_memory_release(&fixture.memory); },
                         SV39_ERR_INVALID_ARGUMENT);
        require_equal(host_call_events().size(), events,
                      "invalid count is rejected before unmap, flush or free");
    }
    fixture.memory = original;
    // Garbage outside the valid prefix must not become another owned record.
    fixture.memory.pages[USER_MEMORY_MAX_PAGE - 1] = fixture.memory.pages[0];
    require_equal(user_memory_release(&fixture.memory), SV39_OK,
                  "unused tail is ignored during release");
    fixture.expect_released();
}

void test_find_owned_record() {
    UserMemoryFixture fixture(true, 2);
    const uintptr_t alias_va = fixture.data_address + 8 * SV39_PAGE_SIZE;
    require_equal(sv39_map_page(fixture.root, alias_va,
                  reinterpret_cast<uintptr_t>(fixture.memory.pages[0].physical_page),
                  fixture.code_flags), SV39_OK, "map an unowned VA alias of code PA");
    const auto alias_mapping = query_mapping(fixture.root, alias_va);
    const auto initial = fixture.snapshot();
    const std::size_t events = host_call_events().size();
    for (std::size_t i = 0; i < fixture.memory.page_count; ++i) {
        require(user_memory_find_page(&fixture.memory,
                    fixture.memory.pages[i].virtual_address) == &fixture.memory.pages[i],
                "find returns the matching borrowed descriptor record");
    }
    require(user_memory_find_page(&fixture.memory, alias_va) == nullptr,
            "same physical page does not make a foreign VA an owned record");
    require_equal(query_mapping(fixture.root, alias_va), alias_mapping,
                  "foreign alias mapping is unchanged by lookup");
    require_equal(fixture.snapshot(), initial, "record lookup preserves all state");
    require_equal(host_call_events().size(), events,
                  "record lookup performs no tracked mutation");

    require_equal(sv39_unmap_page(fixture.root, alias_va), SV39_OK,
                  "remove borrowed alias before releasing the owned content");
    const uintptr_t last_va = fixture.memory.pages[3].virtual_address;
    require_equal(sv39_unmap_page(fixture.root, last_va), SV39_OK,
                  "remove leaf before testing description-only lookup");
    require(user_memory_find_page(&fixture.memory, last_va) == &fixture.memory.pages[3],
            "unmapped owned record remains findable");
    void* saved_pa = fixture.memory.pages[3].physical_page;
    fixture.memory.pages[3].physical_page = nullptr;
    require(user_memory_find_page(&fixture.memory, last_va) == &fixture.memory.pages[3],
            "cleared PA record remains findable during partial release");
    fixture.memory.pages[3].physical_page = saved_pa;
    require_equal(user_memory_release(&fixture.memory), SV39_OK,
                  "release after description-only lookup");
    fixture.expect_released();
}

void test_find_rejects_invalid_input() {
    UserMemoryFixture fixture;
    const auto initial = fixture.snapshot();
    require(user_memory_find_page(nullptr, fixture.code_address) == nullptr,
            "null descriptor is rejected");
    UserMemory copy = fixture.memory;
    copy.root = nullptr;
    require(user_memory_find_page(&copy, fixture.code_address) == nullptr,
            "missing root is rejected");
    for (const std::size_t count : {std::size_t{0},
                                   std::size_t{USER_MEMORY_MAX_PAGE + 1}}) {
        copy = fixture.memory;
        copy.page_count = count;
        require(user_memory_find_page(&copy, fixture.code_address) == nullptr,
                "invalid descriptor count is rejected");
    }
    for (const uintptr_t address : {uintptr_t{0}, fixture.code_address + 1,
                                    uintptr_t{0x4000000000}}) {
        copy = fixture.memory;
        copy.pages[0].virtual_address = address;
        require(user_memory_find_page(&copy, address) == nullptr,
                "even a matching invalid record VA is rejected");
    }
    copy = fixture.memory;
    copy.pages[USER_MEMORY_MAX_PAGE - 1] = {fixture.data_address,
                                          fixture.memory.pages[0].physical_page};
    require(user_memory_find_page(&copy, fixture.data_address) == nullptr,
            "lookup ignores records outside the valid prefix");
    require_equal(fixture.snapshot(), initial, "rejected lookup preserves real ownership");
    require_equal(user_memory_release(&fixture.memory), SV39_OK,
                  "release after rejected lookups");
    fixture.expect_released();
}
} // namespace

int test_user_page_collection() {
    bool passed = true;
    passed = run_case("page collection: four content pages and phase ordering",
                      test_multi_page_release) && passed;
    passed = run_case("page collection: second data free failure and retry",
                      test_second_data_free_failure_retry) && passed;
    passed = run_case("page collection: reject duplicate physical ownership",
                      test_duplicate_physical_ownership) && passed;
    passed = run_case("page collection: invalid counts and ignored tail",
                      test_invalid_counts) && passed;
    passed = run_case("page collection: lookup unmapped and cleared records",
                      test_find_owned_record) && passed;
    passed = run_case("page collection: lookup invalid inputs and ignored tail",
                      test_find_rejects_invalid_input) && passed;
    return passed ? 0 : -1;
}
