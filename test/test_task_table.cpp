#include "task_factory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "host_support.hpp"

namespace {
using namespace tinyos_test;

static_assert(!std::is_copy_constructible_v<task>);
static_assert(!std::is_move_constructible_v<task>);
static_assert(!std::is_copy_constructible_v<tinyos::TaskTable>);
static_assert(!std::is_move_constructible_v<tinyos::TaskTable>);

void check_empty_record(const TestTask& item) {
    require_equal(item.execution->state, TASK_UNUSED, "reaped slot is UNUSED");
    require_equal(item.execution->task_id, -1, "reaped identity is cleared");
    require(item.execution->proc == nullptr && item.execution->context.frame == nullptr,
            "reaped record drops borrowed Process and frame pointers");
    require(item.process->resources_empty(), "reaped Process keeps an empty space");
    require_equal(item.process->parent_id(), tinyos::kNoParentId,
                  "reaped Process clears its parent");
    const auto& info = item.process->exit_info();
    require(info.reason == TASK_EXIT_NONE && info.status == 0 && info.fault_scause == 0,
            "reaped Process clears all exit-result fields");
}

void test_five_slots_rebuild_500_rounds() {
    constexpr int rounds = 500;
    constexpr int capacity = tinyos::TaskTable::kCapacity;
    TaskFactory factory;
    auto& table = factory.table();
    std::array<task*, capacity> addresses{};
    std::array<tinyos::Process*, capacity> process_addresses{};
    std::array<unsigned char*, capacity> stack_addresses{};
    std::array<trap_frame*, capacity> frame_addresses{};
    std::array<int, capacity> previous_ids{};
    previous_ids.fill(-1);
    int next_expected_id = 0;
    const auto allocations = host_allocation_calls();
    const auto free_calls = host_call_counts().free;

    for (int round = 0; round < rounds; ++round) {
        std::array<TestTask, capacity> batch{};
        for (int slot = 0; slot < capacity; ++slot) {
            const auto argument = static_cast<unsigned long>(round * capacity + slot + 1);
            batch[slot] = factory.create(77, argument);
            const auto& item = batch[slot];
            require_equal(item.id, next_expected_id++, "public IDs remain monotonic across rounds");
            require_equal(item.slot, slot, "allocation chooses the smallest available slot");
            require_equal(table.find_slot(item.id), slot, "new public ID resolves to its slot");
            if (previous_ids[slot] >= 0) {
                require_equal(table.find_slot(previous_ids[slot]), -1,
                              "old ID cannot resolve to a reused slot");
            }
            if (round == 0) {
                addresses[slot] = item.execution;
                process_addresses[slot] = item.process;
                stack_addresses[slot] = item.execution->context.stack;
                frame_addresses[slot] = item.execution->context.frame;
            }
            require(item.execution == addresses[slot] && item.process == process_addresses[slot] &&
                    item.execution->context.stack == stack_addresses[slot] &&
                    item.execution->context.frame == frame_addresses[slot],
                    "Task, Process, stack and initial frame addresses remain stable");
            const auto stack_top = reinterpret_cast<uintptr_t>(
                item.execution->context.stack + TINYOS_KERNEL_STACK_SIZE);
            trap_frame expected{};
            expected.sp = stack_top;
            expected.a0 = argument;
            require_equal(std::memcmp(item.execution->context.frame, &expected, sizeof(expected)), 0,
                          "allocation rebuilds all initial frame fields");
            require_equal(stack_top % 16, uintptr_t{0}, "stack top remains ABI aligned");
            require_equal(reinterpret_cast<uintptr_t>(item.execution->context.frame) % 16,
                          uintptr_t{0}, "frame remains ABI aligned");
            const auto marker = static_cast<unsigned char>((round + slot) % 251 + 1);
            item.execution->context.stack[0] = marker;
            item.execution->context.stack[TINYOS_KERNEL_STACK_SIZE - sizeof(trap_frame) - 1] = marker;
            item.execution->context.frame->sepc = 0xdead0000UL + argument;
        }
        require_equal(table.high_water(), capacity, "full table retains the five-slot high water");
        task* unchanged = batch[0].execution;
        require_equal(table.allocate(&unchanged), -1, "sixth allocation rejects a full table");
        require(unchanged == batch[0].execution, "failed allocation preserves its output");

        for (int slot = 0; slot < capacity; ++slot) {
            const auto& item = batch[slot];
            factory.finish(item, static_cast<long>(round * capacity + slot));
            require_equal(item.process->exit_info().status,
                          static_cast<long>(round * capacity + slot), "finish records the requested result");
            factory.reap(item);
            check_empty_record(item);
            const auto marker = static_cast<unsigned char>((round + slot) % 251 + 1);
            require(item.execution->context.stack[0] == marker &&
                    item.execution->context.stack[TINYOS_KERNEL_STACK_SIZE - sizeof(trap_frame) - 1] == marker,
                    "recycling preserves stack bytes outside the saved frame");
            require_equal(table.find_slot(item.id), -1, "completed public ID becomes unresolvable");
            require_equal(table.high_water(), capacity, "recycling the last slot does not shrink high water");
            previous_ids[slot] = item.id;
        }
    }
    require_equal(next_expected_id, rounds * capacity, "500 cohorts publish 2500 distinct task IDs");
    require_equal(host_allocation_calls(), allocations, "table stress performs no PMM allocation");
    require_equal(host_call_counts().free, free_calls, "table stress performs no PMM free");
}

void test_reuse_smallest_interior_hole() {
    TaskFactory factory;
    std::array<TestTask, tinyos::TaskTable::kCapacity> batch{};
    for (auto& item : batch) {
        item = factory.create(77, 1);
    }
    factory.finish(batch[3], 3);
    factory.reap(batch[3]);
    factory.finish(batch[1], 1);
    factory.reap(batch[1]);
    const TestTask first = factory.create(88, 11);
    const TestTask second = factory.create(88, 22);
    require_equal(first.slot, 1, "smaller interior hole wins despite recycling order");
    require_equal(second.slot, 3, "next allocation uses the remaining interior hole");
    require_equal(first.id, 5, "first hole receives a new identity");
    require_equal(second.id, 6, "second hole receives the next identity");
    for (const int slot : {0, 2, 4}) {
        require_equal(factory.table().find_slot(batch[slot].id), slot,
                      "other live identities survive neighboring recycling");
    }
}
} // namespace

int test_task_table() {
    if (!run_case("task table: five slots die and rebuild for 500 rounds (2500 IDs)",
                  test_five_slots_rebuild_500_rounds)) {
        return -1;
    }
    if (!run_case("task table: interior holes reuse the smallest slot with fresh IDs",
                  test_reuse_smallest_interior_hole)) {
        return -1;
    }
    return 0;
}
