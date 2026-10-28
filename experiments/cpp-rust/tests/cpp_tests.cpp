#include "../cpp/pmm.hpp"
#include "../cpp/sv39.hpp"
#include "reference.h"

#include <array>
#include <cstdio>
#include <cstdlib>

using tinyos_trial::INVALID_PAGE;
using tinyos_trial::PAGE_SIZE;
using tinyos_trial::Page;
using tinyos_trial::Pmm;
namespace sv39 = tinyos_trial::sv39;

static constexpr std::size_t PAGE_COUNT = 256;
static std::array<Page, PAGE_COUNT> g_reference_pages;
static std::array<Page, PAGE_COUNT> g_trial_pages;

static void check(bool condition, const char* scenario) {
    if (!condition) {
        std::fprintf(stderr, "[FAIL] C++: %s\n", scenario);
        std::exit(EXIT_FAILURE);
    }
}

static std::uint32_t next_random(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static void compare_sv39(std::uint64_t address, std::uint64_t flags) {
    check(sv39::is_canonical(address) == (sv39_virtual_address_is_canonical(address) != 0),
          "canonical address agrees with C");
    for (unsigned int level = 0; level < 5; ++level) {
        check(sv39::vpn_index(address, level) == sv39_vpn_index(address, level),
              "VPN index and invalid-level sentinel agree with C");
    }
    const auto pte = sv39::make_pte(address, flags);
    check(pte == sv39_make_pte(address, flags), "PTE encoding agrees with C");
    check(sv39::physical_address(pte) == sv39_pte_physical_address(pte),
          "PTE address decoding agrees with C");
    check(sv39::flags(address) == sv39_pte_flags(address), "PTE flags agree with C");
    check(sv39::is_valid(address) == (sv39_pte_is_valid(address) != 0),
          "raw PTE validity agrees with C");
    check(sv39::is_leaf(address) == (sv39_pte_is_leaf(address) != 0),
          "raw PTE leaf status agrees with C");
}

static void test_sv39() {
    // Fixed expectations prevent a shared implementation mistake passing unnoticed.
    check(sv39::is_canonical(0x3fffffffffULL), "last low canonical address");
    check(!sv39::is_canonical(0x4000000000ULL), "low canonical hole boundary");
    check(sv39::is_canonical(0xffffffc000000000ULL), "first high canonical address");
    check(!sv39::is_canonical(0xffffffbfffffffffULL), "high canonical hole boundary");
    check(sv39::make_pte(0x81234000, 0xff) == 0x2048d0ff, "known PTE encoding");
    check(!sv39::is_valid(5) && sv39::is_leaf(9) && !sv39::is_leaf(1),
          "write without read, execute-only leaf, and table pointer");
    check(sv39::vpn_index(0, UINT32_MAX) == 512, "very large VPN level is rejected");
    for (std::uint64_t flags = 0; flags < 1024; ++flags) {
        compare_sv39(flags, flags);
    }
    std::uint32_t state = 0x32588;
    for (std::size_t iteration = 0; iteration < 20000; ++iteration) {
        const auto high = static_cast<std::uint64_t>(next_random(state));
        const auto address = (high << 32) | next_random(state);
        compare_sv39(address, address ^ UINT64_MAX);
    }
    compare_sv39(UINT64_MAX, UINT64_MAX);
    std::puts("[PASS] C++ Sv39: boundaries + 20,000 C comparisons");
}

static void* reference_page(std::size_t index) {
    return &g_reference_pages[index];
}

static std::size_t allocate_and_compare(Pmm& allocator) {
    void* reference = pmm_alloc_page();
    const auto index = allocator.allocate();
    if (reference == nullptr) {
        check(index == INVALID_PAGE, "pool exhaustion agrees with C");
        return index;
    }
    const auto offset = reinterpret_cast<std::uintptr_t>(reference) -
        reinterpret_cast<std::uintptr_t>(g_reference_pages.data());
    check(index < PAGE_COUNT && index == offset / PAGE_SIZE,
          "sequential allocation and LIFO reuse agree with C");
    Page* page = allocator.page(index);
    check(page != nullptr, "allocated page is accessible");
    for (std::size_t byte = 0; byte < PAGE_SIZE; ++byte) {
        check(page->bytes[byte] == 0 && g_reference_pages[index].bytes[byte] == 0,
              "fresh and reused pages are zeroed");
        page->bytes[byte] = 0xa5;
        g_reference_pages[index].bytes[byte] = 0xa5;
    }
    return index;
}

static void release_and_compare(Pmm& allocator, std::size_t index) {
    check(allocator.release(index) == pmm_free_page(reference_page(index)),
          "valid release agrees with C");
    check(allocator.page(index) == nullptr, "freed page is inaccessible");
    check(allocator.release(index) == -1 && pmm_free_page(reference_page(index)) == -1,
          "double free is rejected");
}

static void test_pmm() {
    check(pmm_init(g_reference_pages.data(), g_reference_pages.data() + PAGE_COUNT) == 0,
          "C reference pool initialization");
    Pmm allocator(g_trial_pages.data(), PAGE_COUNT);
    check(allocator.release(0) == -1 && pmm_free_page(reference_page(0)) == -1,
          "never-allocated page is rejected");
    check(allocator.release(INVALID_PAGE) == -1 && pmm_free_page(nullptr) == -1,
          "invalid page is rejected");
    std::array<std::size_t, PAGE_COUNT> live{};
    std::size_t live_count = 0;
    // Exhaustion, full recycling, and reuse order before the random workload.
    for (std::size_t page = 0; page < PAGE_COUNT; ++page) {
        live[live_count++] = allocate_and_compare(allocator);
    }
    check(allocate_and_compare(allocator) == INVALID_PAGE, "full pool rejects allocation");
    while (live_count > 0) {
        release_and_compare(allocator, live[--live_count]);
    }
    std::uint32_t state = 0x32588;
    for (std::size_t operation = 0; operation < 20000; ++operation) {
        const auto value = next_random(state);
        if (live_count == 0 || (live_count < PAGE_COUNT && (value & 1) != 0)) {
            live[live_count++] = allocate_and_compare(allocator);
        } else {
            const auto slot = static_cast<std::size_t>(value) % live_count;
            release_and_compare(allocator, live[slot]);
            live[slot] = live[--live_count];
        }
        check(allocator.available_pages() == pmm_available_pages() &&
              allocator.available_pages() == PAGE_COUNT - live_count,
              "available-page count tracks every operation");
    }
    while (live_count > 0) {
        release_and_compare(allocator, live[--live_count]);
    }
    check(allocator.available_pages() == PAGE_COUNT, "all pages returned after stress");
    Pmm empty(nullptr, 0);
    check(empty.allocate() == INVALID_PAGE && empty.available_pages() == 0,
          "empty pool is safe");
    std::puts("[PASS] C++ PMM: exhaustion + reuse + 20,000 C comparisons");
}

int main() {
    test_sv39();
    test_pmm();
    return EXIT_SUCCESS;
}
