#include "test_user_data.hpp"
#include "test_support.hpp"
#include "user_memory_fixture.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace tinyos_test;

/*
 * 与 test_user_memory.cpp 相同的局部助手：比较一段事件区间。
 * 独立成 test_user_data.cpp 后需要自己留一份。
 */
void require_events(std::size_t begin, std::size_t end,
                    std::initializer_list<CallEvent> expected,
                    std::string_view message) {
    require_equal(end - begin, expected.size(), message);

    std::size_t index = 0;
    for (const CallEvent &want : expected) {
        const CallEvent &got = host_call_events()[begin + index];
        require_equal(got.kind, want.kind, message);
        require_equal(got.address, want.address, message);
        require_equal(got.result, want.result, message);
        ++index;
    }
}

/*
 * 释放前记下 L0/L1 的物理地址：release 会把根页表的空分支摘掉，
 * 事后再走表就取不到这两个地址了。
 */
struct TablePAs {
    uintptr_t l0;
    uintptr_t l1;
};

TablePAs capture_table_pas(const UserMemoryFixture &fixture) {
    const Sv39PageTable *l1 = reinterpret_cast<const Sv39PageTable *>(
        static_cast<uintptr_t>(sv39_pte_physical_address(
            fixture.root->entries[sv39_vpn_index(fixture.code_address, 2)])));
    require(l1 != nullptr, "user L1 table exists before release");
    const uintptr_t l0 = static_cast<uintptr_t>(sv39_pte_physical_address(
        l1->entries[sv39_vpn_index(fixture.code_address, 1)]));
    return {l0, reinterpret_cast<uintptr_t>(l1)};
}

/* D1：三页描述正常回收，归还五个物理页，描述清空，顺序为 unmap*3 -> flush*3 -> free*5。 */
void test_three_page_release() {
    UserMemoryFixture fixture(true);

    const uintptr_t code_va = fixture.code_address;
    const uintptr_t stack_va = fixture.stack_address;
    const uintptr_t data_va = fixture.data_address;
    const uintptr_t code_pa = reinterpret_cast<uintptr_t>(fixture.memory.code_page);
    const uintptr_t stack_pa = reinterpret_cast<uintptr_t>(fixture.memory.stack_page);
    const uintptr_t data_pa = reinterpret_cast<uintptr_t>(fixture.memory.data_page);
    const TablePAs tables = capture_table_pas(fixture);

    const std::size_t begin = host_call_events().size();

    const auto release = observe(
        [&] { return fixture.snapshot(); },
        [&] { return user_memory_release(&fixture.memory); });

    require_equal(release.result, SV39_OK, "three-page release succeeds");
    require_equal(release.after.available_pages,
                  release.before.available_pages + 5,
                  "code, stack, data, L0 and L1 are returned");
    require_equal(release.after.memory, MemoryDescriptor{},
                  "seven descriptor fields cleared");
    require_equal(release.after.code_mapping.result, SV39_ERR_NOT_MAPPED,
                  "code mapping is removed");
    require_equal(release.after.stack_mapping.result, SV39_ERR_NOT_MAPPED,
                  "stack mapping is removed");
    require_equal(release.after.data_mapping.result, SV39_ERR_NOT_MAPPED,
                  "data mapping is removed");
    require_equal(release.after.calls,
                  CallCounts{release.before.calls.unmap + 3,
                             release.before.calls.flush + 3,
                             release.before.calls.free + 5},
                  "3 unmap, 3 flush and 5 free calls");

    const std::size_t end = host_call_events().size();
    fixture.expect_released();

    require_events(begin, end,
        {
            {CallKind::Unmap, code_va, SV39_OK},
            {CallKind::Unmap, stack_va, SV39_OK},
            {CallKind::Unmap, data_va, SV39_OK},
            {CallKind::Flush, code_va, SV39_OK},
            {CallKind::Flush, stack_va, SV39_OK},
            {CallKind::Flush, data_va, SV39_OK},
            {CallKind::Free, code_pa, SV39_OK},
            {CallKind::Free, stack_pa, SV39_OK},
            {CallKind::Free, data_pa, SV39_OK},
            {CallKind::Free, tables.l0, SV39_OK},
            {CallKind::Free, tables.l1, SV39_OK},
        },
        "data release event order");
}

/* D2：释放完成后描述全空，再调一次必须成功且什么都不做。 */
void test_repeated_release() {
    UserMemoryFixture fixture(true);

    require_equal(user_memory_release(&fixture.memory), SV39_OK,
                  "first release succeeds");
    fixture.expect_released();

    expect_no_change(
        [&] { return std::pair{fixture.snapshot(), host_call_events().size()}; },
        [&] { return user_memory_release(&fixture.memory); },
        SV39_OK);

    fixture.expect_released();
}

/* D3：数据页映射的 PA 或 U 与描述不符，必须在撤销 code/stack 之前拒绝。 */
struct MappingDamage {
    const char *name;
    void (*apply)(const UserMemoryFixture &fixture, void *spare);
};

void damage_wrong_pa(const UserMemoryFixture &fixture, void *spare) {
    require_equal(sv39_unmap_page(fixture.root, fixture.data_address), SV39_OK,
                  "unmap the real data mapping");
    require_equal(sv39_map_page(fixture.root, fixture.data_address,
                                reinterpret_cast<uintptr_t>(spare),
                                UserMemoryFixture::data_falgs),
                  SV39_OK, "remap the data VA to a foreign page");
}

void damage_missing_u(const UserMemoryFixture &fixture, void *) {
    require_equal(sv39_unmap_page(fixture.root, fixture.data_address), SV39_OK,
                  "unmap the real data mapping");
    require_equal(sv39_map_page(fixture.root, fixture.data_address,
                                reinterpret_cast<uintptr_t>(fixture.memory.data_page),
                                SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D),
                  SV39_OK, "remap the data page without U");
}

const MappingDamage mapping_damages[] = {
    {"data PA mismatch", damage_wrong_pa},
    {"data mapping missing U", damage_missing_u},
};

void run_data_mapping_damage(const MappingDamage &scenario) {
    UserMemoryFixture fixture(true);

    /* 故障准备、辅助页申请和最后的恢复都在 observation 之外。 */
    void *spare = pmm_alloc_page();
    require(spare != nullptr, "allocate the spare page");
    scenario.apply(fixture, spare);
    const std::size_t prepared = host_call_events().size();

    const auto rejected = observe(
        [&] { return fixture.snapshot(); },
        [&] { return user_memory_release(&fixture.memory); });

    require_equal(rejected.result, SV39_ERR_INVALID_ARGUMENT,
                  "damaged data mapping is rejected");
    require_equal(rejected.after, rejected.before,
                  "rejection preserves descriptor, mappings, pages and calls");
    require_equal(host_call_events().size(), prepared,
                  "rejection performs no unmap, flush or free");

    require_equal(sv39_unmap_page(fixture.root, fixture.data_address), SV39_OK,
                  "remove the damaged mapping");
    require_equal(sv39_map_page(fixture.root, fixture.data_address,
                                reinterpret_cast<uintptr_t>(fixture.memory.data_page),
                                UserMemoryFixture::data_falgs),
                  SV39_OK, "restore the real data mapping");
    require_equal(pmm_free_page(spare), 0, "return the spare page");

    require_equal(user_memory_release(&fixture.memory), SV39_OK,
                  "release succeeds after the repair");
    fixture.expect_released();
}

/* D4：数据页 free 失败后只归还 code/stack，重试补齐 data 和两级中间表。 */
void test_data_free_failure_retry() {
    UserMemoryFixture fixture(true);

    const uintptr_t code_va = fixture.code_address;
    const uintptr_t stack_va = fixture.stack_address;
    const uintptr_t data_va = fixture.data_address;
    const uintptr_t code_pa = reinterpret_cast<uintptr_t>(fixture.memory.code_page);
    const uintptr_t stack_pa = reinterpret_cast<uintptr_t>(fixture.memory.stack_page);
    const uintptr_t data_pa = reinterpret_cast<uintptr_t>(fixture.memory.data_page);
    const TablePAs tables = capture_table_pas(fixture);

    const std::size_t begin = host_call_events().size();

    const auto first = observe(
        [&] { return fixture.snapshot(); },
        [&] {
            host_set_free_failure(fixture.memory.data_page);
            const int result = user_memory_release(&fixture.memory);
            host_set_free_failure(nullptr);
            return result;
        });

    require_equal(first.result, SV39_ERR_PAGE_FREE_FAILED,
                  "data free failure is reported");

    auto expected_memory = first.before.memory;
    expected_memory.code_page = nullptr;
    expected_memory.stack_page = nullptr;
    require_equal(first.after.memory, expected_memory,
                  "failed call keeps the data page, root and all three VAs");
    require_equal(first.after.available_pages,
                  first.before.available_pages + 2,
                  "only code and stack pages are returned");
    require_equal(first.after.code_mapping.result, SV39_ERR_NOT_MAPPED,
                  "code mapping is removed");
    require_equal(first.after.stack_mapping.result, SV39_ERR_NOT_MAPPED,
                  "stack mapping is removed");
    require_equal(first.after.data_mapping.result, SV39_ERR_NOT_MAPPED,
                  "data mapping is removed");
    require_equal(first.after.calls,
                  CallCounts{first.before.calls.unmap + 3,
                             first.before.calls.flush + 3,
                             first.before.calls.free + 3},
                  "3 unmap, 3 flush and 3 free attempts");

    const std::size_t middle = host_call_events().size();

    const auto retry = observe(
        [&] { return fixture.snapshot(); },
        [&] { return user_memory_release(&fixture.memory); });

    require_equal(retry.result, SV39_OK, "retry succeeds");
    require_equal(retry.after.available_pages,
                  retry.before.available_pages + 3,
                  "retry returns the data page, L0 and L1");
    require_equal(retry.after.calls,
                  CallCounts{retry.before.calls.unmap + 3,
                             retry.before.calls.flush + 3,
                             retry.before.calls.free + 3},
                  "retry call counts");

    const std::size_t end = host_call_events().size();
    fixture.expect_released();

    require_events(begin, middle,
        {
            {CallKind::Unmap, code_va, SV39_OK},
            {CallKind::Unmap, stack_va, SV39_OK},
            {CallKind::Unmap, data_va, SV39_OK},
            {CallKind::Flush, code_va, SV39_OK},
            {CallKind::Flush, stack_va, SV39_OK},
            {CallKind::Flush, data_va, SV39_OK},
            {CallKind::Free, code_pa, SV39_OK},
            {CallKind::Free, stack_pa, SV39_OK},
            {CallKind::Free, data_pa, -1},
        },
        "first data-free-failure event order");

    require_events(middle, end,
        {
            {CallKind::Unmap, code_va, SV39_ERR_NOT_MAPPED},
            {CallKind::Unmap, stack_va, SV39_ERR_NOT_MAPPED},
            {CallKind::Unmap, data_va, SV39_ERR_NOT_MAPPED},
            {CallKind::Flush, code_va, SV39_OK},
            {CallKind::Flush, stack_va, SV39_OK},
            {CallKind::Flush, data_va, SV39_OK},
            {CallKind::Free, data_pa, SV39_OK},
            {CallKind::Free, tables.l0, SV39_OK},
            {CallKind::Free, tables.l1, SV39_OK},
        },
        "data free retry event order");
}

/* D5：第三次 flush 失败时一个页都不能释放，重试才完成五个 free。 */
void test_data_flush_failure_retry() {
    UserMemoryFixture fixture(true);

    const uintptr_t code_va = fixture.code_address;
    const uintptr_t stack_va = fixture.stack_address;
    const uintptr_t data_va = fixture.data_address;
    const uintptr_t code_pa = reinterpret_cast<uintptr_t>(fixture.memory.code_page);
    const uintptr_t stack_pa = reinterpret_cast<uintptr_t>(fixture.memory.stack_page);
    const uintptr_t data_pa = reinterpret_cast<uintptr_t>(fixture.memory.data_page);
    const TablePAs tables = capture_table_pas(fixture);

    const std::size_t begin = host_call_events().size();

    const auto first = observe(
        [&] { return fixture.snapshot(); },
        [&] {
            host_set_flush_failure(3);
            const int result = user_memory_release(&fixture.memory);
            host_set_flush_failure(0);
            return result;
        });

    require_equal(first.result, SV39_ERR_INVALID_ARGUMENT,
                  "third flush failure is reported");
    require_equal(first.after.memory, first.before.memory,
                  "failed flush keeps all three pages, root and VAs");
    require_equal(first.after.available_pages, first.before.available_pages,
                  "no page is returned before the flush succeeds");
    require_equal(first.after.code_mapping.result, SV39_ERR_NOT_MAPPED,
                  "code mapping is removed");
    require_equal(first.after.stack_mapping.result, SV39_ERR_NOT_MAPPED,
                  "stack mapping is removed");
    require_equal(first.after.data_mapping.result, SV39_ERR_NOT_MAPPED,
                  "data mapping is removed");
    require_equal(first.after.calls,
                  CallCounts{first.before.calls.unmap + 3,
                             first.before.calls.flush + 3,
                             first.before.calls.free},
                  "3 unmap and 3 flush attempts, no free");

    const std::size_t middle = host_call_events().size();

    const auto retry = observe(
        [&] { return fixture.snapshot(); },
        [&] { return user_memory_release(&fixture.memory); });

    require_equal(retry.result, SV39_OK, "retry succeeds");
    require_equal(retry.after.available_pages,
                  retry.before.available_pages + 5,
                  "retry returns all five pages");
    require_equal(retry.after.calls,
                  CallCounts{retry.before.calls.unmap + 3,
                             retry.before.calls.flush + 3,
                             retry.before.calls.free + 5},
                  "retry call counts");

    const std::size_t end = host_call_events().size();
    fixture.expect_released();

    require_events(begin, middle,
        {
            {CallKind::Unmap, code_va, SV39_OK},
            {CallKind::Unmap, stack_va, SV39_OK},
            {CallKind::Unmap, data_va, SV39_OK},
            {CallKind::Flush, code_va, SV39_OK},
            {CallKind::Flush, stack_va, SV39_OK},
            {CallKind::Flush, data_va, -1},
        },
        "first data-flush-failure event order");

    require_events(middle, end,
        {
            {CallKind::Unmap, code_va, SV39_ERR_NOT_MAPPED},
            {CallKind::Unmap, stack_va, SV39_ERR_NOT_MAPPED},
            {CallKind::Unmap, data_va, SV39_ERR_NOT_MAPPED},
            {CallKind::Flush, code_va, SV39_OK},
            {CallKind::Flush, stack_va, SV39_OK},
            {CallKind::Flush, data_va, SV39_OK},
            {CallKind::Free, code_pa, SV39_OK},
            {CallKind::Free, stack_pa, SV39_OK},
            {CallKind::Free, data_pa, SV39_OK},
            {CallKind::Free, tables.l0, SV39_OK},
            {CallKind::Free, tables.l1, SV39_OK},
        },
        "data flush retry event order");
}

/* D6：同一张表里还有别人的映射时，只归还自己拥有的三个内容页，父表保留。 */
void test_release_preserves_other_mapping() {
    UserMemoryFixture fixture(true);

    const uintptr_t foreign_address =
        UserMemoryFixture::code_address + 2 * SV39_PAGE_SIZE;
    void *foreign_page = pmm_alloc_page();
    require(foreign_page != nullptr, "allocate the foreign page");
    require_equal(sv39_map_page(fixture.root, foreign_address,
                                reinterpret_cast<uintptr_t>(foreign_page),
                                UserMemoryFixture::data_falgs),
                  SV39_OK, "map the foreign page");
    const Mapping foreign_mapping = query_mapping(fixture.root, foreign_address);
    require_equal(foreign_mapping.result, SV39_OK, "foreign mapping exists");

    const uintptr_t code_va = fixture.code_address;
    const uintptr_t stack_va = fixture.stack_address;
    const uintptr_t data_va = fixture.data_address;
    const uintptr_t code_pa = reinterpret_cast<uintptr_t>(fixture.memory.code_page);
    const uintptr_t stack_pa = reinterpret_cast<uintptr_t>(fixture.memory.stack_page);
    const uintptr_t data_pa = reinterpret_cast<uintptr_t>(fixture.memory.data_page);
    const unsigned int vpn2 = sv39_vpn_index(fixture.code_address, 2);
    const uint64_t branch_before = fixture.root->entries[vpn2];

    const std::size_t begin = host_call_events().size();

    const auto release = observe(
        [&] { return fixture.snapshot(); },
        [&] { return user_memory_release(&fixture.memory); });

    require_equal(release.result, SV39_OK,
                  "release with a foreign mapping succeeds");
    require_equal(release.after.available_pages,
                  release.before.available_pages + 3,
                  "only the three owned content pages are returned");
    require_equal(release.after.memory, MemoryDescriptor{},
                  "seven descriptor fields cleared");
    require_equal(release.after.code_mapping.result, SV39_ERR_NOT_MAPPED,
                  "code mapping is removed");
    require_equal(release.after.stack_mapping.result, SV39_ERR_NOT_MAPPED,
                  "stack mapping is removed");
    require_equal(release.after.data_mapping.result, SV39_ERR_NOT_MAPPED,
                  "data mapping is removed");
    require_equal(query_mapping(fixture.root, foreign_address), foreign_mapping,
                  "foreign mapping keeps its PA and flags");
    require_equal(fixture.root->entries[vpn2], branch_before,
                  "non-empty page-table branch is kept");

    const std::size_t end = host_call_events().size();

    require_events(begin, end,
        {
            {CallKind::Unmap, code_va, SV39_OK},
            {CallKind::Unmap, stack_va, SV39_OK},
            {CallKind::Unmap, data_va, SV39_OK},
            {CallKind::Flush, code_va, SV39_OK},
            {CallKind::Flush, stack_va, SV39_OK},
            {CallKind::Flush, data_va, SV39_OK},
            {CallKind::Free, code_pa, SV39_OK},
            {CallKind::Free, stack_pa, SV39_OK},
            {CallKind::Free, data_pa, SV39_OK},
        },
        "foreign-mapping release event order");

    /* 收尾：外部页和只属于它的两级表由测试自己归还。 */
    require_equal(sv39_unmap_page(fixture.root, foreign_address), SV39_OK,
                  "unmap the foreign page");
    require_equal(sv39_reclaim_empty_tables(fixture.root, foreign_address),
                  SV39_OK, "reclaim the now empty tables");
    require_equal(pmm_free_page(foreign_page), 0, "return the foreign page");
    require_equal(pmm_available_pages(), fixture.pages_after_root,
                  "page pool is restored");
    require_equal(fixture.root->entries[vpn2], UINT64_C(0),
                  "empty branch is removed after the cleanup");
}

/* D7：非法数据描述必须在任何释放动作之前被拒绝，且不改变任何状态。 */
struct DataDescriptorState {
    UserMemoryState real;       // fixture.root 与固定 VA 的真实状态
    MemoryDescriptor damaged;   // 被改坏的描述副本
    std::size_t events;
    bool operator==(const DataDescriptorState &) const = default;
};

DataDescriptorState capture_descriptor(const UserMemoryFixture &fixture,
                                       const UserMemory &broken) {
    return {fixture.snapshot(), describe(broken), host_call_events().size()};
}

struct DescriptorDamage {
    const char *name;
    void (*apply)(const UserMemoryFixture &fixture, UserMemory &broken);
};

void damage_root_null(const UserMemoryFixture &fixture, UserMemory &broken) {
    broken = {};
    broken.data_page = fixture.memory.data_page;
    broken.data_address = fixture.memory.data_address;
}

void damage_data_va_zero(const UserMemoryFixture &, UserMemory &broken) {
    broken.data_address = 0;
}

void damage_data_va_unaligned(const UserMemoryFixture &, UserMemory &broken) {
    broken.data_address += 1;
}

void damage_data_va_noncanonical(const UserMemoryFixture &, UserMemory &broken) {
    broken.data_address = UINT64_C(1) << 39;
}

void damage_data_va_code(const UserMemoryFixture &fixture, UserMemory &broken) {
    broken.data_address = fixture.code_address;
}

void damage_data_va_stack(const UserMemoryFixture &fixture, UserMemory &broken) {
    broken.data_address = fixture.stack_address;
}

void damage_data_pa_code(const UserMemoryFixture &, UserMemory &broken) {
    broken.data_page = broken.code_page;
}

void damage_data_pa_stack(const UserMemoryFixture &, UserMemory &broken) {
    broken.data_page = broken.stack_page;
}

const DescriptorDamage descriptor_damages[] = {
    {"data only with NULL root", damage_root_null},
    {"data VA zero", damage_data_va_zero},
    {"data VA unaligned", damage_data_va_unaligned},
    {"data VA non-canonical", damage_data_va_noncanonical},
    {"data VA equals code", damage_data_va_code},
    {"data VA equals stack", damage_data_va_stack},
    {"data PA equals code PA", damage_data_pa_code},
    {"data PA equals stack PA", damage_data_pa_stack},
};

void run_descriptor_damage(DescriptorDamage scenario) {
    UserMemoryFixture fixture(true);

    /* 只改描述副本，真实描述留到最后做一次正常回收。 */
    UserMemory broken = fixture.memory;
    scenario.apply(fixture, broken);

    expect_no_change(
        [&] { return capture_descriptor(fixture, broken); },
        [&] { return user_memory_release(&broken); },
        SV39_ERR_INVALID_ARGUMENT);

    require_equal(user_memory_release(&fixture.memory), SV39_OK,
                  "release succeeds after the rejected descriptor");
    fixture.expect_released();
}

} // namespace

int test_user_data(void) {
    bool passed = true;
    // 先 run_case 再累加，前面的场景失败不跳过后面的场景。
    passed = run_case("user memory: D1 three-page release",
                      test_three_page_release) && passed;
    passed = run_case("user memory: D2 repeated release",
                      test_repeated_release) && passed;

    for (const MappingDamage &scenario : mapping_damages) {
        const std::string name = std::string("user memory: D3 ") + scenario.name;
        passed = run_case(name, [&] { run_data_mapping_damage(scenario); }) && passed;
    }

    passed = run_case("user memory: D4 data free failure and retry",
                      test_data_free_failure_retry) && passed;
    passed = run_case("user memory: D5 third flush failure and retry",
                      test_data_flush_failure_retry) && passed;
    passed = run_case("user memory: D6 release preserves foreign mapping",
                      test_release_preserves_other_mapping) && passed;

    for (const DescriptorDamage &scenario : descriptor_damages) {
        const std::string name = std::string("user memory: D7 ") + scenario.name;
        passed = run_case(name, [&] { run_descriptor_damage(scenario); }) && passed;
    }

    return passed ? 0 : -1;
}
