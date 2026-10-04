#include "test_user_memory.h"

#include "user_memory_fixture.hpp"

namespace {
using namespace tinyos_test;

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

enum class UserPage{ Code, Stack };
enum class MappingProblem { WrongPa, MissingU};

void test_invalid_ownership(UserPage target, MappingProblem problem) {
    UserMemoryFixture fixture;

    const bool stack = target == UserPage::Stack;
    const uint64_t address = stack ?
        fixture.stack_address : fixture.code_address;
    void* owned_page = stack ?
        fixture.memory.stack_page : fixture.memory.code_page;
    void* other_page = stack ?
        fixture.memory.code_page : fixture.memory.stack_page;
    const uint64_t flags = stack ?
        fixture.stack_flags : fixture.code_flags;

    void* mapped_page = problem == MappingProblem::WrongPa ?
        other_page : owned_page;
    const uint64_t mapped_flags = problem == MappingProblem::MissingU ?
        flags & ~SV39_PTE_U : flags;
    /*
     *先解除当前已有映射
     */
    require_equal(sv39_unmap_page(fixture.root, address),
     SV39_OK, "remove mapping before praparing defect");
    /* 新建一个错误的映射 */
     require_equal(
        sv39_map_page(fixture.root, address,
            reinterpret_cast<uintptr_t>(mapped_page), mapped_flags),
            SV39_OK, "prepare defective ownership mapping"
        );

    require_equal(
        query_mapping(fixture.root, address),
        Mapping{SV39_OK, reinterpret_cast<uintptr_t>(mapped_page),
                mapped_flags | SV39_PTE_V},
        "verify prepared mapping"
    );

    expect_no_change(
        [&]{return fixture.snapshot();},
        [&] {return user_memory_release(&fixture.memory);},
        SV39_ERR_INVALID_ARGUMENT
    );

    require_equal(sv39_unmap_page(fixture.root, address),
                  SV39_OK, "remove defective mapping");
    require_equal(
        sv39_map_page(fixture.root, address,
                    reinterpret_cast<uintptr_t>(owned_page),
        flags), SV39_OK, "restore owned mapping"
    );
    require_equal(user_memory_release(&fixture.memory),
    SV39_OK, "release succeeds after restoring mapping");

    fixture.expect_released();
}

void test_normal_release() {
    UserMemoryFixture fixture;
    const std::size_t event_start = host_call_events().size();
    const auto release = observe([&] { return fixture.snapshot(); },
                                [&] { return user_memory_release(&fixture.memory); });
    require_equal(release.result, SV39_OK, "normal release succeeds");
    require_equal(release.after.available_pages, release.before.available_pages + 4,
                  "normal release restores four pages");
    require_equal(release.after.calls.unmap, release.before.calls.unmap + 2,
                  "normal release performs two unmaps");
    require_equal(release.after.calls.flush, release.before.calls.flush + 2,
                  "normal release performs two flushes");
    require_equal(release.after.calls.free, release.before.calls.free + 4,
                  "normal release performs four physical frees");
    fixture.expect_released();

    unsigned int successful_unmaps = 0;
    unsigned int successful_flushes = 0;
    for (std::size_t index = event_start; index < host_call_events().size(); ++index) {
        const auto &event = host_call_events()[index];
        require_equal(event.result, SV39_OK, "normal release hook succeeds");
        if (event.kind == CallKind::Unmap) {
            ++successful_unmaps;
        } else if (event.kind == CallKind::Flush) {
            require_equal(successful_unmaps, 2U, "unmap both pages before flushing");
            ++successful_flushes;
        } else {
            require_equal(successful_flushes, 2U, "flush both pages before freeing");
            require(event.address != reinterpret_cast<uintptr_t>(fixture.root),
                    "borrowed root must not be freed");
        }
    }
}

void test_repeated_release() {
    UserMemoryFixture fixture;
    require_equal(user_memory_release(&fixture.memory), SV39_OK, "first release succeeds");
    fixture.expect_released();
    expect_no_change([&] { return fixture.snapshot(); },
                     [&] { return user_memory_release(&fixture.memory); }, SV39_OK);
}

void test_initially_empty_release() {
    PagePool<8> pool;
    UserMemory empty{};
    expect_no_change([&] { return capture_user_memory(empty); },
                     [&] { return user_memory_release(&empty); }, SV39_OK);
}

void test_observation_helpers() {
    require(!values_equal(-1, UINT64_MAX), "negative errors differ from unsigned maxima");
    require(values_equal(4, 4UL), "equal signed and unsigned page counts compare equal");
    int state = 0;
    int captures = 0;
    int executions = 0;
    const auto observed = observe([&] { ++captures; return state; },
                                 [&] { ++executions; state = 1; return 7; });
    require_equal(captures, 2, "observe captures before and after");
    require_equal(executions, 1, "observe executes the operation once");
    require_equal(observed.before, 0, "observe keeps original state");
    require_equal(observed.after, 1, "observe captures final state");
    require_equal(observed.result, 7, "observe preserves return value");
}

void test_stack_free_failure_retry() {
    UserMemoryFixture fixture;

    const uintptr_t code_va = fixture.code_address;
    const uintptr_t stack_va = fixture.stack_address;
    const uintptr_t code_pa = reinterpret_cast<uintptr_t>(fixture.memory.code_page);
    const uintptr_t stack_pa = reinterpret_cast<uintptr_t>(fixture.memory.stack_page);

    const unsigned int vpn2 = sv39_vpn_index(fixture.code_address, 2);
    const unsigned int vpn1 = sv39_vpn_index(fixture.code_address, 1);
    const uintptr_t l1_pa = (uintptr_t)sv39_pte_physical_address(
        fixture.root->entries[vpn2]);
    Sv39PageTable *l1 = (Sv39PageTable *)(uintptr_t)l1_pa;
    const uintptr_t l0_pa = (uintptr_t)sv39_pte_physical_address(
    l1->entries[vpn1]);

    /*
     * 通过Observe创建一个新的内容过程
     * 将一个lambda传进去作为一等公民并允许
     * 分别保留开始执行前后的快照和运行结果
     */

    /*
     * 第一次尝试，在栈页失败
     */

    /*
     * 建立索引
    */
    const std::size_t begin = host_call_events().size();

    const auto first = observe(
        [&] { return fixture.snapshot(); },
        [&] {
            host_set_free_failure(fixture.memory.stack_page);
            const int result = user_memory_release(&fixture.memory);
            host_set_free_failure(nullptr); // 表示结束失败调用，不然每次调用都返回 -1
            return result;
        }
    );
    /*
     * 确认结果失败并返回 SV39_ERR_PAGE_FREE_FAILED
     */
    require_equal(first.result, SV39_ERR_PAGE_FREE_FAILED,
                  "stack free failure is reported");

    /*
     * 这个 excepted_memory 不能动
     * 因为不可以直接把 first.before.memory.code_page 设置为nullptr
     */
    auto expected_memory = first.before.memory;
    expected_memory.code_page = nullptr;
    require_equal(first.after.memory, expected_memory,
                  "failure preserves remaining ownership");

    require_equal(first.after.available_pages,
                  first.before.available_pages + 1,
                  "only code page is restored");
    require_equal(first.after.code_mapping.result, SV39_ERR_NOT_MAPPED,
                  "code mapping is removed");
    require_equal(first.after.stack_mapping.result, SV39_ERR_NOT_MAPPED,
                  "stack mapping is removed");

    require_equal(
        first.after.calls,
        CallCounts{first.before.calls.unmap + 2,
                            first.before.calls.flush + 2,
                            first.before.calls.free + 2},
        "first release call counts"
    );

    const std::size_t middle = host_call_events().size();

    /*
     * 重试，本次结果直接返回成功没有干扰
     */
    const auto retry = observe(
        [&] { return fixture.snapshot(); },
        [&] { return user_memory_release(&fixture.memory); }
    );

    /*
     * 同上的流程
     */
    require_equal(retry.result, SV39_OK, "retry succeeds");
    require_equal(retry.after.available_pages,
                  retry.before.available_pages + 3,
                  "retry restores stack and intermediate tables");
    require_equal(
        retry.after.calls,
        CallCounts{retry.before.calls.unmap + 2,
                   retry.before.calls.flush + 2,
                   retry.before.calls.free + 3},
        "retry call counts"
    );

    const std::size_t end = host_call_events().size();
    fixture.expect_released();

    /*
        检查两次调用已记录的事件顺序和释放目标, 不再执行释放
    */

    require_events(begin, middle,
        {
            {CallKind::Unmap, code_va, SV39_OK},
            {CallKind::Unmap, stack_va, SV39_OK},
            {CallKind::Flush, code_va, SV39_OK},
            {CallKind::Flush, stack_va, SV39_OK},
            {CallKind::Free, code_pa, SV39_OK},
            {CallKind::Free, stack_pa, -1}
        },
        "firets failed release event order");

    require_events(middle, end, {
        {CallKind::Unmap, code_va,  SV39_ERR_NOT_MAPPED},
        {CallKind::Unmap, stack_va, SV39_ERR_NOT_MAPPED},
        {CallKind::Flush, code_va,  SV39_OK},
        {CallKind::Flush, stack_va, SV39_OK},
        {CallKind::Free,  stack_pa, SV39_OK},
        {CallKind::Free,  l0_pa,    SV39_OK},
        {CallKind::Free,  l1_pa,    SV39_OK},
        },
         "retry event order");

}

void test_release_after_manual_unmap() {
    UserMemoryFixture fixture;

    const uintptr_t va_code = fixture.memory.code_address;
    const uintptr_t va_stack = fixture.memory.stack_address;

    const uintptr_t pa_stack = reinterpret_cast<uintptr_t>(fixture.memory.stack_page);
    const uintptr_t pa_code = reinterpret_cast<uintptr_t>(fixture.memory.code_page);
    const unsigned int vpn2 = sv39_vpn_index(
        va_code, 2);
    const unsigned int vpn1 = sv39_vpn_index(
        va_code, 1);
    const uintptr_t l1_pa = (uintptr_t)sv39_pte_physical_address(
        fixture.root->entries[vpn2]);
    auto* l1 = reinterpret_cast<Sv39PageTable*>(l1_pa);
    const uintptr_t l0_pa = (uintptr_t)sv39_pte_physical_address(
        l1->entries[vpn1]);

    require_equal(
        sv39_unmap_page(fixture.root, fixture.code_address),
                SV39_OK, "manually unmap code leaf");
    require_equal(
        sv39_unmap_page(fixture.root, fixture.stack_address),
                  SV39_OK, "manually unmap stack leaf");

    require_equal(
        query_mapping(fixture.root, fixture.memory.code_address).result,
        SV39_ERR_NOT_MAPPED, "code leaf is already gone");
    require_equal(
        query_mapping(fixture.root, fixture.memory.stack_address).result,
        SV39_ERR_NOT_MAPPED, "stack leaf is already gone");
    require_equal(pmm_available_pages(), fixture.pages_after_root - 4);

    const std::size_t begin = host_call_events().size();
    const auto release = observe(
        [&] { return fixture.snapshot(); },
        [&] { return user_memory_release(&fixture.memory); }
    );
    const std::size_t end = host_call_events().size();

    require_equal(release.result, SV39_OK,"release accepts already-missing leaves");
    require_equal(release.after.available_pages, release.before.available_pages + 4,
        "physical pages and tables are relaimd");
    require_equal(release.after.calls,
        CallCounts{
            release.before.calls.unmap + 2,
            release.before.calls.flush + 2,
            release.before.calls.free + 4
        },
        "H5 Calls Count"
    );

    fixture.expect_released();

    require_events(begin, end,
        {
            {CallKind::Unmap, va_code, SV39_ERR_NOT_MAPPED},
            {CallKind::Unmap, va_stack, SV39_ERR_NOT_MAPPED},
            {CallKind::Flush, va_code, SV39_OK},
            {CallKind::Flush, va_stack, SV39_OK},
            {CallKind::Free, pa_code, SV39_OK},
            {CallKind::Free, pa_stack, SV39_OK},
            {CallKind::Free, l0_pa, SV39_OK},
            {CallKind::Free, l1_pa, SV39_OK}
        },
        "H5 release event order");
}

void test_null_release() {
    UserMemoryFixture fixture;

    expect_no_change(
        [&] {return fixture.snapshot();},
        [] {return user_memory_release(nullptr);},
        SV39_ERR_INVALID_ARGUMENT);

    require_equal(user_memory_release(&fixture.memory),
        SV39_OK, "valid release succeed after null argument");
    fixture.expect_released();
}

void test_release_without_root() {
    UserMemoryFixture fixture;
    fixture.memory.root = nullptr;

    const auto capture = [&] {
        auto state = fixture.snapshot();
        state.code_mapping =
            query_mapping(fixture.root, fixture.memory.code_address);
        state.stack_mapping =
            query_mapping(fixture.root, fixture.memory.stack_address);
        return state;
    };

    expect_no_change(
        capture,
        [&] {return user_memory_release(&fixture.memory);},
        SV39_ERR_INVALID_ARGUMENT
    );

    fixture.memory.root = fixture.root;
    require_equal(user_memory_release(&fixture.memory),
        SV39_OK, "release succeeds after restoring root");
    fixture.expect_released();
}

void test_flush_failure_retry(unsigned long failure_attempt) {
    UserMemoryFixture fixture;
    const unsigned int vpn2 = sv39_vpn_index(fixture.code_address, 2);
    const unsigned int vpn1 = sv39_vpn_index(fixture.code_address, 1);
    const Sv39Pte root_entry = fixture.root->entries[vpn2];
    const uintptr_t l1_pa = static_cast<uintptr_t>(sv39_pte_physical_address(root_entry));
    auto *l1 = reinterpret_cast<Sv39PageTable *>(l1_pa);
    const Sv39Pte l1_entry = l1->entries[vpn1];
    const uintptr_t l0_pa = static_cast<uintptr_t>(sv39_pte_physical_address(l1_entry));

    const std::size_t begin = host_call_events().size();
    const auto first = observe(
        [&] { return fixture.snapshot(); },
        [&] {
            host_set_flush_failure(failure_attempt);
            const int result = user_memory_release(&fixture.memory);
            host_set_flush_failure(0);
            return result;
        }
    );
    const std::size_t middle = host_call_events().size();

    require_equal(first.result, SV39_ERR_INVALID_ARGUMENT,
                  "flush failure is reported before freeing resources");
    auto expected = first.before;
    expected.code_mapping = Mapping{};
    expected.stack_mapping = Mapping{};
    expected.calls.unmap += 2;
    // The first failed flush short-circuits the second call in the kernel.
    expected.calls.flush += failure_attempt;
    require_equal(first.after, expected,
                  "flush failure removes leaves but preserves owned pages and descriptor");
    require_equal(fixture.root->entries[vpn2], root_entry,
                  "flush failure retains L1");
    require_equal(l1->entries[vpn1], l1_entry, "flush failure retains L0");

    if (failure_attempt == 1) {
        require_events(begin, middle, {
            {CallKind::Unmap, fixture.code_address, SV39_OK},
            {CallKind::Unmap, fixture.stack_address, SV39_OK},
            {CallKind::Flush, fixture.code_address, SV39_ERR_INVALID_ARGUMENT}
        }, "first flush failure event order");
    } else {
        require_events(begin, middle, {
            {CallKind::Unmap, fixture.code_address, SV39_OK},
            {CallKind::Unmap, fixture.stack_address, SV39_OK},
            {CallKind::Flush, fixture.code_address, SV39_OK},
            {CallKind::Flush, fixture.stack_address, SV39_ERR_INVALID_ARGUMENT}
        }, "second flush failure event order");
    }

    const auto retry = observe(
        [&] { return fixture.snapshot(); },
        [&] { return user_memory_release(&fixture.memory); }
    );
    const std::size_t end = host_call_events().size();
    require_equal(retry.result, SV39_OK, "release succeeds after disabling flush failure");
    require_equal(retry.after.available_pages, retry.before.available_pages + 4,
                  "retry restores both owned pages and both intermediate tables");
    require_equal(retry.after.calls,
                  CallCounts{retry.before.calls.unmap + 2,
                             retry.before.calls.flush + 2,
                             retry.before.calls.free + 4},
                  "flush failure retry call counts");
    fixture.expect_released();
    require_events(middle, end, {
        {CallKind::Unmap, fixture.code_address, SV39_ERR_NOT_MAPPED},
        {CallKind::Unmap, fixture.stack_address, SV39_ERR_NOT_MAPPED},
        {CallKind::Flush, fixture.code_address, SV39_OK},
        {CallKind::Flush, fixture.stack_address, SV39_OK},
        {CallKind::Free, reinterpret_cast<uintptr_t>(first.before.memory.code_page), SV39_OK},
        {CallKind::Free, reinterpret_cast<uintptr_t>(first.before.memory.stack_page), SV39_OK},
        {CallKind::Free, l0_pa, SV39_OK},
        {CallKind::Free, l1_pa, SV39_OK}
    }, "flush failure retry event order");
}

void test_release_preserves_other_mapping() {
    UserMemoryFixture fixture;

    const uint64_t other_va = fixture.stack_address + SV39_PAGE_SIZE;
    const uint64_t other_flags =
        SV39_PTE_R | SV39_PTE_W | SV39_PTE_A | SV39_PTE_D;

    void* other_page = pmm_alloc_page();
    require(other_page != nullptr, "allocate independently owned page");
    const uintptr_t other_pa = reinterpret_cast<uintptr_t>(other_page);

    const auto pages_before_map = pmm_available_pages();
    require_equal(
        sv39_map_page(fixture.root, other_va, other_pa, other_flags),
        SV39_OK, "map other page in shared L0");
    require_equal(pmm_available_pages(), pages_before_map,
    "other mapping reuses existing page tables");

    const Mapping other_before = query_mapping(fixture.root, other_va);
    require_equal(
        other_before,
        Mapping{SV39_OK, other_pa, other_flags | SV39_PTE_V},
    "verify independently owned mapping");

    const unsigned int vpn2 = sv39_vpn_index(fixture.code_address, 2);
    const unsigned int vpn1 = sv39_vpn_index(fixture.code_address, 1);
    const Sv39Pte root_enrty = fixture.root->entries[vpn2];
    auto l1 = reinterpret_cast<Sv39PageTable*>(
        static_cast<uintptr_t>(sv39_pte_physical_address(root_enrty))
    );
    const uintptr_t l1_entry = l1->entries[vpn1];

    const std::size_t begin = host_call_events().size();
    const auto release = observe(
        [&]{return fixture.snapshot();},
        [&] {return user_memory_release(&fixture.memory);}
    );
    const std::size_t end = host_call_events().size();

    require_equal(release.result, SV39_OK, "H7 release succeeds");
    require_equal(release.after.memory, MemoryDescriptor{},
        "H7 descriptor is empty");
    require_equal(release.after.available_pages,
        release.before.available_pages + 2,
        "H7 release only code and stack pages");

    require_equal(
        release.after.calls,
        CallCounts{
            release.before.calls.unmap + 2,
            release.before.calls.flush + 2,
            release.before.calls.free + 2
        },
        "H7 release call counts"
    );

    require_equal(query_mapping(fixture.root, other_va), other_before,
    "other mapping is preserved");
    require_equal(fixture.root->entries[vpn2], root_enrty,
        "shared L1 remains accached");
    require_equal(l1->entries[vpn1], l1_entry,
        "shared L0 remains attached");
    require_equal(query_mapping(fixture.root, fixture.code_address).result, SV39_ERR_NOT_MAPPED,
    "code address released");
    require_equal(query_mapping(fixture.root, fixture.stack_address).result, SV39_ERR_NOT_MAPPED,
    "stack address released");

    require_events(begin, end,
        {
            {CallKind::Unmap, fixture.code_address, SV39_OK},
            {CallKind::Unmap, fixture.stack_address, SV39_OK},
            {CallKind::Flush, fixture.code_address, SV39_OK},
            {CallKind::Flush, fixture.stack_address, SV39_OK},
            {CallKind::Free, reinterpret_cast<uintptr_t>(release.before.memory.code_page), SV39_OK},
            {CallKind::Free, reinterpret_cast<uintptr_t>(release.before.memory.stack_page), SV39_OK}
        },
        "H7 release event order");
    require_equal(sv39_unmap_page(fixture.root, other_va),
                SV39_OK, "remove other mapping");
    require_equal(sv39_flush_page(other_va),
                SV39_OK, "flush before reclaiming other resources");
    require_equal(pmm_free_page(other_page),
                0, "free independently owned page");
    require_equal(sv39_reclaim_empty_tables(fixture.root, other_va),
                SV39_OK, "reclaim now-empty shared tables");

    fixture.expect_released();
}
}

int test_user_memory(void) {
    return run_case("C++ observation helpers", test_observation_helpers) &&
           run_case("H1 normal user-memory release", test_normal_release) &&
           run_case("H2 repeated user-memory release", test_repeated_release) &&
           run_case("H2 initially empty user-memory release", test_initially_empty_release) &&
           run_case("H3 code PA mismatch", [] {
                test_invalid_ownership(UserPage::Code, MappingProblem::WrongPa);
           }) &&
           run_case("H3 stack PA mismatch", [] {
                test_invalid_ownership(UserPage::Stack, MappingProblem::WrongPa);
           }) &&
           run_case("H3 code missing U", [] {
                test_invalid_ownership(UserPage::Code, MappingProblem::MissingU);
           }) &&
           run_case("H3 stack missing U", [] {
                test_invalid_ownership(UserPage::Stack, MappingProblem::MissingU);
           }) &&
           run_case("H4 stack free failure and retry",
                    test_stack_free_failure_retry)
            && run_case("H5 release after manual unmap",
            test_release_after_manual_unmap)
            && run_case("H6 null user-memory argument",
            test_null_release)
            && run_case("H6 nonempty user-memory without root",
            test_release_without_root)
            && run_case("H7 release preserves other mapping",
            test_release_preserves_other_mapping)
            && run_case("H8 first flush failure and retry", [] {
                test_flush_failure_retry(1);
            })
            && run_case("H8 second flush failure and retry", [] {
                test_flush_failure_retry(2);
            })
           ? 0 : -1;
}
