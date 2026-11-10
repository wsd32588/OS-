#include "test_user_access.hpp"
#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
using namespace tinyos_test;

using PageBytes = std::array<unsigned char, SV39_PAGE_SIZE>;
constexpr unsigned char BUFFER_GUARD = 0xcc;

struct UserAccessState {
    unsigned long available_pages;
    CallCounts calls;
    std::size_t event_count;
    Mapping first_mapping;
    Mapping second_mapping;
    std::array<PageBytes, 6> pages;
    bool operator==(const UserAccessState &) const = default;
};

PageBytes capture_page(const void *page) {
    PageBytes bytes{};
    std::memcpy(bytes.data(), page, bytes.size());
    return bytes;
}

UserAccessState capture_user_access(const UserAccessFixture &fixture) {
    const Sv39Pte root_entry = fixture.root->entries[
        sv39_vpn_index(UserAccessFixture::first_address, 2)];
    require(sv39_pte_is_valid(root_entry) && !sv39_pte_is_leaf(root_entry),
            "L1 table remains valid");
    const auto *l1 = reinterpret_cast<const Sv39PageTable *>(
        static_cast<uintptr_t>(sv39_pte_physical_address(root_entry)));
    const Sv39Pte l1_entry = l1->entries[
        sv39_vpn_index(UserAccessFixture::first_address, 1)];
    require(sv39_pte_is_valid(l1_entry) && !sv39_pte_is_leaf(l1_entry),
            "L0 table remains valid");
    const auto *l0 = reinterpret_cast<const Sv39PageTable *>(
        static_cast<uintptr_t>(sv39_pte_physical_address(l1_entry)));

    return {
        pmm_available_pages(), host_call_counts(), host_call_events().size(),
        query_mapping(fixture.root, UserAccessFixture::first_address),
        query_mapping(fixture.root, UserAccessFixture::second_address),
        {capture_page(fixture.first_page), capture_page(fixture.gap_page),
         capture_page(fixture.second_page), capture_page(fixture.root),
         capture_page(l1), capture_page(l0)}
    };
}

void expect_copy_result(const UserAccessFixture &fixture,
                        void *destination, uintptr_t address,
                        std::size_t length, int expected_result) {
    const auto observation = observe(
        [&] { return capture_user_access(fixture); },
        [&] { return user_copy_from(fixture.root, destination, address, length); }
    );
    require_equal(observation.result, expected_result, "copy return value");
    require_equal(observation.after, observation.before,
                  "copy preserves source pages, page tables, pages and calls");
}

void expect_guards(const std::array<unsigned char, 6> &buffer) {
    require_equal(buffer.front(), BUFFER_GUARD, "leading guard preserved");
    require_equal(buffer.back(), BUFFER_GUARD, "trailing guard preserved");
}

void test_begin_not_zero() {
    UserMemoryFixture fixture;
    auto* source =
        static_cast<unsigned char*>(fixture.memory.pages[0].physical_page);

    constexpr std::size_t offset = 17;
    const std::array<unsigned char,4> expected {
        'A', 0, 'C', 0xff
    };

    for (std::size_t i = 0; i < expected.size(); ++i) {
        source[offset + i] = expected[i];
    }

    std::array<unsigned char, SV39_PAGE_SIZE> code_page_backup{};
    std::memcpy(code_page_backup.data(), source, SV39_PAGE_SIZE);
    const auto stack_backup = capture_page(fixture.memory.pages[1].physical_page);

    std::array<unsigned char, 6> buffer{};
    buffer.fill(0xcc);

    const auto obversation = observe(
        [&] {return fixture.snapshot();},
        [&] {
            return user_copy_from(
                fixture.root,
                buffer.data() + 1,
                UserMemoryFixture::code_address + offset,
                expected.size());
        }
    );

    require_equal(obversation.result, SV39_OK, "copy succeeds");
    require_equal(obversation.after, obversation.before,
        "copy preverses memory state");

    for (std::size_t i = 0; i < expected.size(); ++i) {
        require_equal(buffer[i + 1], expected[i], "copied byte");
    }

    require_equal(buffer.front(), 0xcc, "leading guard preserved");
    require_equal(buffer.back(), 0xcc, "trailing guard preserved");

    const int page_diff = std::memcmp(source, code_page_backup.data(), SV39_PAGE_SIZE);
    require_equal(page_diff, 0, "source code page unmodified after copy");
    require_equal(capture_page(fixture.memory.pages[1].physical_page), stack_backup,
                  "other source page unchanged");
}

void test_consecutive_virtual_page() {
    UserAccessFixture fixture;

    require_equal(reinterpret_cast<uintptr_t>(fixture.second_page) -
                  reinterpret_cast<uintptr_t>(fixture.first_page),
                  2 * SV39_PAGE_SIZE, "physical pages have an allocated gap");
    require_equal(UserAccessFixture::second_address,
                  UserAccessFixture::first_address + SV39_PAGE_SIZE,
                  "virtual pages are adjacent");

    fixture.first_page[SV39_PAGE_SIZE - 2] = 'Y';
    fixture.first_page[SV39_PAGE_SIZE - 1] = 'X';
    fixture.second_page[0] = 'W';
    fixture.second_page[1] = 'Z';

    std::array<unsigned char, 6> buf{};
    buf.fill(BUFFER_GUARD);
    expect_copy_result(fixture, buf.data() + 1,
                       UserAccessFixture::first_address + SV39_PAGE_SIZE - 2,
                       4, SV39_OK);
    const std::array<unsigned char, 4> expected{'Y', 'X', 'W', 'Z'};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        require_equal(buf[i + 1], expected[i], "cross-page byte");
    }
    expect_guards(buf);
}

void test_unmapped_second_page() {
    UserAccessFixture fixture;

    require_equal(
        sv39_unmap_page(fixture.root, fixture.second_address),
        SV39_OK,
        "remove second virtual-page mapping");
    require_equal(
        query_mapping(fixture.root, fixture.second_address).result,
        SV39_ERR_NOT_MAPPED,
        "second virtual page is unmapped");
    require_equal(
        query_mapping(fixture.root, fixture.first_address).result,
        SV39_OK,
        "first virtual page still alive");
    fixture.first_page[SV39_PAGE_SIZE - 2] = 'A';
    fixture.first_page[SV39_PAGE_SIZE - 1] = 'B';

    std::array<unsigned char, 6> buf{};
    buf.fill(BUFFER_GUARD);
    expect_copy_result(fixture, buf.data() + 1,
                       UserAccessFixture::first_address + SV39_PAGE_SIZE - 2,
                       4, SV39_ERR_INVALID_ARGUMENT);
    // A failed cross-page copy may already have written a valid prefix.
    expect_guards(buf);
    require_equal(buf[3], BUFFER_GUARD, "unmapped-page output untouched");
    require_equal(buf[4], BUFFER_GUARD, "unmapped-page output untouched");
}

void test_second_page_permissions(uint64_t second_flags) {
    UserAccessFixture fixture;

    require_equal(
        sv39_unmap_page(fixture.root, fixture.second_address), SV39_OK,
        "remove second-page mapping");
    require_equal(query_mapping(fixture.root, fixture.second_address).result, SV39_ERR_NOT_MAPPED,
        "second page unmapped");
    require_equal(
        sv39_map_page(
            fixture.root,
            fixture.second_address,
            reinterpret_cast<uintptr_t>(fixture.second_page),
            second_flags),
        SV39_OK,
        "remap second page with test permissions");
    const auto mapping =
    query_mapping(fixture.root, fixture.second_address);

    require_equal(mapping.result, SV39_OK, "query succeeds");
    require_equal(mapping.physical_address,
                reinterpret_cast<uintptr_t>(fixture.second_page),
                "second-page PA");
    require_equal(mapping.flags, second_flags | SV39_PTE_V,
                "second-page permissions");

    fixture.first_page[SV39_PAGE_SIZE - 2] = 'A';
    fixture.first_page[SV39_PAGE_SIZE - 1] = 'B';

    fixture.second_page[0] = 'C';
    fixture.second_page[1] = 'D';
    std::array<unsigned char, 6> buf{};
    buf.fill(BUFFER_GUARD);
    expect_copy_result(fixture, buf.data() + 1,
                       UserAccessFixture::first_address + SV39_PAGE_SIZE - 2,
                       4, SV39_ERR_INVALID_ARGUMENT);
    expect_guards(buf);
    require_equal(buf[3], BUFFER_GUARD, "restricted-page output untouched");
    require_equal(buf[4], BUFFER_GUARD, "restricted-page output untouched");
}

void test_copy_from_unmapped_start() {
    UserAccessFixture fixture;

    constexpr uint64_t unmapped_address =
        UserAccessFixture::base_address + 2 * SV39_PAGE_SIZE;
    require_equal(query_mapping(fixture.root, unmapped_address).result,
                  SV39_ERR_NOT_MAPPED, "start VA is unmapped");

    std::array<unsigned char, 4> buffer{};
    buffer.fill(0xcc);

    expect_copy_result(fixture, buffer.data(), unmapped_address, 1,
                       SV39_ERR_INVALID_ARGUMENT);

    for (unsigned char byte : buffer) {
        require_equal(byte, static_cast<unsigned char>(0xcc),
                      "unmapped source is detected before writing");
    }
}

void test_copy_from_null_arguments() {
    UserAccessFixture fixture;

    std::array<unsigned char, 4> buffer{};
    buffer.fill(0xcc);

    expect_no_change(
        [&] { return capture_user_access(fixture); },
        [&] {
            return user_copy_from(nullptr, buffer.data(),
                UserAccessFixture::first_address, buffer.size());
        }, SV39_ERR_INVALID_ARGUMENT
    );

    expect_copy_result(fixture, nullptr, UserAccessFixture::first_address,
                       buffer.size(), SV39_ERR_INVALID_ARGUMENT);

    expect_no_change(
        [&] { return capture_user_access(fixture); },
        [&] {
            return user_copy_from(nullptr, nullptr,
                UserAccessFixture::first_address, buffer.size());
        }, SV39_ERR_INVALID_ARGUMENT
    );

    for (unsigned char byte : buffer) {
        require_equal(byte, static_cast<unsigned char>(0xcc),
                      "destination stays untouched");
    }
}

void test_copy_from_zero_length() {
    UserAccessFixture fixture;
    for (uintptr_t address : {uintptr_t{0}, uintptr_t{UINTPTR_MAX},
                             uintptr_t{UINT64_C(1) << 38}}) {
        expect_no_change(
            [&] { return capture_user_access(fixture); },
            [&] { return user_copy_from(nullptr, nullptr, address, 0); },
            SV39_OK
        );
    }
    std::array<unsigned char, 6> buffer{};
    buffer.fill(BUFFER_GUARD);
    const auto before = buffer;
    expect_copy_result(fixture, buffer.data() + 1, UINTPTR_MAX, 0, SV39_OK);
    require_equal(buffer, before, "zero length leaves destination untouched");
}

void test_copy_from_wraparound() {
    UserAccessFixture fixture;

    const uintptr_t last_page = UINTPTR_MAX & ~(SV39_PAGE_SIZE - 1U);
    require_equal(sv39_map_page(fixture.root, last_page,
        reinterpret_cast<uintptr_t>(fixture.first_page), UserAccessFixture::page_flags),
        SV39_OK, "map bytes before the address wrap");
    fixture.first_page[SV39_PAGE_SIZE - 2] = 'A';
    fixture.first_page[SV39_PAGE_SIZE - 1] = 'B';
    const auto boundary_mapping = query_mapping(fixture.root, UINTPTR_MAX - 1);
    require_equal(boundary_mapping.result, SV39_OK, "overflow start is mapped");

    std::array<unsigned char, 4> buffer{};
    buffer.fill(0xcc);

    expect_copy_result(fixture, buffer.data(), UINTPTR_MAX - 1,
                       buffer.size(), SV39_ERR_INVALID_ARGUMENT);

    for (unsigned char byte : buffer) {
        require_equal(byte, static_cast<unsigned char>(0xcc),
                      "wraparound is detected before writing");
    }
    require_equal(query_mapping(fixture.root, UINTPTR_MAX - 1), boundary_mapping,
                  "overflow-start mapping unchanged");
}

void test_copy_from_noncanonical() {
    UserAccessFixture fixture;

    std::array<unsigned char, 4> buffer{};
    buffer.fill(0xcc);

    /* 起点非 canonical：2^38，bit 38 = 1，但高位全 0 */
    expect_copy_result(fixture, buffer.data(), UINT64_C(0x0000004000000000),
                       1, SV39_ERR_INVALID_ARGUMENT);

    for (unsigned char byte : buffer) {
        require_equal(byte, BUFFER_GUARD, "non-canonical start leaves buffer untouched");
    }
}

void test_copy_from_noncanonical_last_byte() {
    UserAccessFixture fixture;
    constexpr uintptr_t last_address = UINT64_C(0x0000003fffffffff);
    const uintptr_t last_page = last_address & ~(SV39_PAGE_SIZE - 1U);
    // The starting byte must be readable to catch copying before checking the end.
    require_equal(sv39_map_page(fixture.root, last_page,
        reinterpret_cast<uintptr_t>(fixture.first_page), UserAccessFixture::page_flags),
        SV39_OK, "map last low canonical page");
    fixture.first_page[SV39_PAGE_SIZE - 1] = 'Q';
    const auto boundary_mapping = query_mapping(fixture.root, last_address);
    require_equal(boundary_mapping.result, SV39_OK, "starting byte is mapped");
    require_equal(boundary_mapping.physical_address,
        reinterpret_cast<uintptr_t>(fixture.first_page) + SV39_PAGE_SIZE - 1,
        "starting byte points to a real physical byte");
    require_equal(boundary_mapping.flags, UserAccessFixture::page_flags | SV39_PTE_V,
                  "starting byte permits user reads");

    std::array<unsigned char, 6> buffer{};
    buffer.fill(BUFFER_GUARD);
    const auto before = buffer;

    /*
     * 起点 canonical：0x0000003fffffffff = 2^38 - 1；
     * length = 2，末字节 = 2^38，变成非 canonical。
     */
    expect_copy_result(fixture, buffer.data() + 1, last_address,
                       2, SV39_ERR_INVALID_ARGUMENT);
    require_equal(buffer, before, "end canonical check happens before any copy");
    require_equal(query_mapping(fixture.root, last_address), boundary_mapping,
                  "boundary mapping unchanged");
}

void test_copy_from_last_address() {
    UserAccessFixture fixture;
    const uintptr_t last_page = UINTPTR_MAX & ~(SV39_PAGE_SIZE - 1U);
    require_equal(sv39_map_page(fixture.root, last_page,
        reinterpret_cast<uintptr_t>(fixture.first_page), UserAccessFixture::page_flags),
        SV39_OK, "map last high canonical page");
    fixture.first_page[SV39_PAGE_SIZE - 1] = 'Z';
    const auto mapping = query_mapping(fixture.root, UINTPTR_MAX);
    require_equal(mapping.result, SV39_OK, "last address is mapped");

    std::array<unsigned char, 6> buffer{};
    buffer.fill(BUFFER_GUARD);
    expect_copy_result(fixture, buffer.data() + 1, UINTPTR_MAX, 1, SV39_OK);
    require_equal(buffer[1], static_cast<unsigned char>('Z'), "last byte copied");
    expect_guards(buffer);
    for (std::size_t i = 2; i < buffer.size(); ++i) {
        require_equal(buffer[i], BUFFER_GUARD, "bytes beyond length untouched");
    }
    require_equal(query_mapping(fixture.root, UINTPTR_MAX), mapping,
                  "last-address mapping unchanged");
}

constexpr uint64_t WRITABLE_USER_FLAGS =
    SV39_PTE_R | SV39_PTE_W | SV39_PTE_U | SV39_PTE_A | SV39_PTE_D;

void remap_access_page(UserAccessFixture &fixture, uintptr_t address,
                       unsigned char *page, uint64_t flags) {
    require_equal(sv39_unmap_page(fixture.root, address), SV39_OK,
                  "remove access-test mapping before permission change");
    require_equal(sv39_map_page(fixture.root, address,
        reinterpret_cast<uintptr_t>(page), flags), SV39_OK,
        "remap access-test page with requested permissions");
}

void prepare_writable_access(UserAccessFixture &fixture) {
    remap_access_page(fixture, UserAccessFixture::first_address,
                      fixture.first_page, WRITABLE_USER_FLAGS);
    remap_access_page(fixture, UserAccessFixture::second_address,
                      fixture.second_page, WRITABLE_USER_FLAGS);
    std::memset(fixture.first_page, BUFFER_GUARD, SV39_PAGE_SIZE);
    std::memset(fixture.gap_page, BUFFER_GUARD, SV39_PAGE_SIZE);
    std::memset(fixture.second_page, BUFFER_GUARD, SV39_PAGE_SIZE);
}

void expect_copy_to_result(const UserAccessFixture &fixture, uintptr_t address,
                           const void *source, std::size_t length,
                           int expected_result, const UserAccessState &expected) {
    require_equal(user_copy_to(fixture.root, address, source, length),
                  expected_result, "user writeback return value");
    require_equal(capture_user_access(fixture), expected,
                  "writeback changes only expected content bytes, preserving all other state");
}

void test_copy_to_offsets_and_separated_pages() {
    UserAccessFixture fixture;
    prepare_writable_access(fixture);
    require_equal(reinterpret_cast<uintptr_t>(fixture.second_page) -
                  reinterpret_cast<uintptr_t>(fixture.first_page),
                  2 * SV39_PAGE_SIZE, "writeback target PAs have an allocated gap");

    const std::array<unsigned char, 10> source{
        0x7e, 0x11, 0, 0xff, 0x80, 0x22, 0x33, 0x44, 0x55, 0x7f};
    const auto source_before = source;
    auto expected = capture_user_access(fixture);
    for (std::size_t i = 0; i < 8; ++i) {
        expected.pages[0][17 + i] = source[1 + i];
    }
    expect_copy_to_result(fixture, UserAccessFixture::first_address + 17,
                          source.data() + 1, 8, SV39_OK, expected);

    for (std::size_t i = 0; i < 3; ++i) {
        expected.pages[0][SV39_PAGE_SIZE - 3 + i] = source[1 + i];
    }
    for (std::size_t i = 0; i < 5; ++i) {
        expected.pages[2][i] = source[4 + i];
    }
    expect_copy_to_result(fixture, UserAccessFixture::first_address + SV39_PAGE_SIZE - 3,
                          source.data() + 1, 8, SV39_OK, expected);
    require_equal(source, source_before, "writeback preserves source and source guards");
}

void test_copy_to_rejects_first_page_permissions() {
    const std::array<unsigned char, 8> source{1, 0, 0xff, 4, 5, 6, 7, 8};
    const auto source_before = source;
    for (uint64_t flags : {SV39_PTE_R | SV39_PTE_X | SV39_PTE_U | SV39_PTE_A,
                           SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D}) {
        UserAccessFixture fixture;
        prepare_writable_access(fixture);
        remap_access_page(fixture, UserAccessFixture::first_address,
                          fixture.first_page, flags);
        const auto expected = capture_user_access(fixture);
        expect_copy_to_result(fixture, UserAccessFixture::first_address + 17,
                              source.data(), source.size(),
                              SV39_ERR_INVALID_ARGUMENT, expected);
    }
    require_equal(source, source_before, "permission rejection preserves source");
}

void test_copy_to_preserves_valid_prefix() {
    UserAccessFixture fixture;
    prepare_writable_access(fixture);
    remap_access_page(fixture, UserAccessFixture::second_address,
                      fixture.second_page, UserAccessFixture::page_flags);
    const std::array<unsigned char, 8> source{1, 0, 0xff, 4, 5, 6, 7, 8};
    const auto source_before = source;
    auto expected = capture_user_access(fixture);
    for (std::size_t i = 0; i < 3; ++i) {
        expected.pages[0][SV39_PAGE_SIZE - 3 + i] = source[i];
    }
    expect_copy_to_result(fixture, UserAccessFixture::first_address + SV39_PAGE_SIZE - 3,
                          source.data(), source.size(), SV39_ERR_INVALID_ARGUMENT, expected);
    require_equal(source, source_before, "failed writeback preserves source");
}

void test_copy_to_zero_length_and_null_arguments() {
    UserAccessFixture fixture;
    prepare_writable_access(fixture);
    const std::array<unsigned char, 8> source{1, 0, 0xff, 4, 5, 6, 7, 8};
    const auto source_before = source;
    const auto expected = capture_user_access(fixture);
    for (uintptr_t address : {uintptr_t{0}, uintptr_t{UINTPTR_MAX},
                             uintptr_t{UINT64_C(1) << 38}}) {
        expect_no_change(
            [&] { return capture_user_access(fixture); },
            [&] { return user_copy_to(nullptr, address, nullptr, 0); }, SV39_OK);
    }
    expect_copy_to_result(fixture, UINTPTR_MAX, nullptr, 0, SV39_OK, expected);
    expect_no_change(
        [&] { return capture_user_access(fixture); },
        [&] {
            return user_copy_to(nullptr, UserAccessFixture::first_address,
                                source.data(), source.size());
        }, SV39_ERR_INVALID_ARGUMENT);
    expect_copy_to_result(fixture, UserAccessFixture::first_address, nullptr, 1,
                          SV39_ERR_INVALID_ARGUMENT, expected);
    require_equal(source, source_before, "entry checks preserve source");
}

void test_copy_to_rejects_invalid_ranges_before_writing() {
    struct Scenario {
        uintptr_t start;
        std::size_t length;
    };
    constexpr uintptr_t last_low = UINT64_C(0x0000003fffffffff);
    constexpr uintptr_t first_high = UINT64_C(0xffffffc000000000);
    const Scenario scenarios[] = {
        {UINTPTR_MAX - 1, 8},
        {last_low, 2},
        {last_low, first_high - last_low + 1},
    };
    const std::array<unsigned char, 8> source{1, 0, 0xff, 4, 5, 6, 7, 8};
    const auto source_before = source;
    for (const Scenario &scenario : scenarios) {
        UserAccessFixture fixture;
        prepare_writable_access(fixture);
        const uintptr_t page_address = scenario.start & ~(SV39_PAGE_SIZE - 1U);
        // A writable first byte catches accidentally copying before range validation.
        require_equal(sv39_map_page(fixture.root, page_address,
            reinterpret_cast<uintptr_t>(fixture.first_page), WRITABLE_USER_FLAGS),
            SV39_OK, "map writable start of invalid output range");
        const auto mapping = query_mapping(fixture.root, scenario.start);
        require_equal(mapping.result, SV39_OK, "invalid range begins at a writable byte");

        const auto *l1 = reinterpret_cast<const Sv39PageTable *>(
            static_cast<uintptr_t>(sv39_pte_physical_address(fixture.root->entries[
                sv39_vpn_index(scenario.start, 2)])));
        const auto *l0 = reinterpret_cast<const Sv39PageTable *>(
            static_cast<uintptr_t>(sv39_pte_physical_address(l1->entries[
                sv39_vpn_index(scenario.start, 1)])));
        const std::array<PageBytes, 2> boundary_tables{capture_page(l1), capture_page(l0)};
        const auto expected = capture_user_access(fixture);
        // The deliberately oversized request must fail before reading this small source.
        expect_copy_to_result(fixture, scenario.start, source.data(), scenario.length,
                              SV39_ERR_INVALID_ARGUMENT, expected);
        require_equal(query_mapping(fixture.root, scenario.start), mapping,
                      "range rejection preserves boundary mapping");
        require_equal(std::array<PageBytes, 2>{capture_page(l1), capture_page(l0)},
                      boundary_tables, "range rejection preserves complete boundary tables");
    }
    require_equal(source, source_before, "invalid output ranges never consume or modify source");
}
} // namespace

int test_user_access(void) {
    bool passed = true;
    // Put run_case first so later cases still run after an earlier failure.
    passed = run_case("user copy: single page with offset and NUL", test_begin_not_zero) && passed;
    passed = run_case("user copy: adjacent VA, separated PA", test_consecutive_virtual_page) && passed;
    passed = run_case("user copy: second page unmapped", test_unmapped_second_page) && passed;
    passed = run_case("user copy: second page missing U", [] {
        test_second_page_permissions(SV39_PTE_R | SV39_PTE_A);
    }) && passed;
    passed = run_case("user copy: second page missing R", [] {
        test_second_page_permissions(SV39_PTE_U | SV39_PTE_X | SV39_PTE_A);
    }) && passed;
    passed = run_case("user copy: unmapped start", test_copy_from_unmapped_start) && passed;
    passed = run_case("user copy: NULL arguments", test_copy_from_null_arguments) && passed;
    passed = run_case("user copy: zero length", test_copy_from_zero_length) && passed;
    passed = run_case("user copy: address overflow", test_copy_from_wraparound) && passed;
    passed = run_case("user copy: non-canonical start", test_copy_from_noncanonical) && passed;
    passed = run_case("user copy: non-canonical last byte", test_copy_from_noncanonical_last_byte) && passed;
    passed = run_case("user copy: last address without overflow", test_copy_from_last_address) && passed;
    passed = run_case("user writeback: offsets and separated PA across 3+5 bytes",
                      test_copy_to_offsets_and_separated_pages) && passed;
    passed = run_case("user writeback: first page requires W and U",
                      test_copy_to_rejects_first_page_permissions) && passed;
    passed = run_case("user writeback: read-only second page leaves valid prefix",
                      test_copy_to_preserves_valid_prefix) && passed;
    passed = run_case("user writeback: zero length and NULL arguments",
                      test_copy_to_zero_length_and_null_arguments) && passed;
    passed = run_case("user writeback: invalid complete ranges reject before writes",
                      test_copy_to_rejects_invalid_ranges_before_writing) && passed;
    return passed ? 0 : -1;
}
