#include "task/private/user_task.hpp"

#include "arch/riscv/trap.h"
#include "arch/riscv/vm_arch.h"
#include "drivers/uart.h"
#include "process/process.hpp"
#include "task/private/task.hpp"
#include "task/private/task_table.hpp"

namespace tinyos::user_task {
namespace {
bool _is_have_child(int task_id, TaskTable& table) {
    //前置检查不由此处承担
    for (int i = 0; i < table.high_water(); ++i) {
        const struct task& candidate = *table.at(i);
        if (candidate.state == TASK_UNUSED || candidate.kind != TASK_KIND_USER ||
            candidate.proc == nullptr) {
            continue;
        }

        // DEAD children still belong to their parent until they are collected.
        if (candidate.proc->parent_id() == task_id) {
            return true;
        }
    }

    return false;
}

void reparent_children(int old_parent_id, int new_parent_id, TaskTable& table) {
    for (int i = 0; i < table.high_water(); ++i) {
        struct task& child = *table.at(i);

        if (child.state == TASK_UNUSED || child.kind != TASK_KIND_USER || child.proc == nullptr) {
            continue;
        }

        if (child.proc->parent_id() == old_parent_id) {
            child.proc->set_parent_id(new_parent_id);
        }
    }
}

void task_destroy(struct task* task, const Sv39PageTable* kernel_root) {
    task->state = TASK_DEAD;
    if (sv39_activate(kernel_root) != SV39_OK) {
        uart_puts("[sched] activating kernel root failed\n");
        for (;;) {
            asm volatile("wfi");
        }
    }
    const int res = task->proc->release_resources();

    if (res != SV39_OK) {
        uart_puts("[user memory] release failed: ");
        uart_put_hex((unsigned long)res);
        uart_puts("\n");
        for (;;) {
            asm volatile("wfi");
        }
    }
}
} // namespace

int find_reaper(const TaskTable& table, int reaper_id) noexcept {
    const int slot_index = table.find_slot(reaper_id);
    if (slot_index < 0) {
        return -1;
    }

    const struct task& reaper = *table.at(slot_index);
    if (reaper.kind != TASK_KIND_USER || reaper.state == TASK_DEAD || reaper.proc == nullptr ||
        reaper.proc->root() == nullptr || reaper.proc->parent_id() != tinyos::kNoParentId) {
        return -1;
    }

    return slot_index;
}

void finish(
    TaskTable& table,
    task& current,
    const trap_frame& frame,
    const Sv39PageTable* kernel_root,
    int reaper_id,
    TaskExitReason reason,
    long status
) {

    const bool is_reaper = current.task_id == reaper_id;

    if (is_reaper) {
        if (_is_have_child(current.task_id, table)) {
            uart_puts("[task] reaper cannot exit while children remain\n");
            for (;;) {
                asm volatile("wfi");
            }
        }
    } else {
        const int slot_index = find_reaper(table, reaper_id);

        if (slot_index < 0) {
            uart_puts("[task] invalid user reaper\n");
            for (;;) {
                asm volatile("wfi");
            }
        }

        reparent_children(current.task_id, table.at(slot_index)->task_id, table);
    }

    current.proc->record_exit(
        TaskExitInfo{
            .reason = reason,
            .status = reason == TASK_EXIT_REQUESTED ? status : 0,
            .fault_scause = reason == TASK_EXIT_FAULT ? frame.scause : 0
        }
    );

    if (reason == TASK_EXIT_FAULT) {
        uart_puts("\n[user fault]\n");
        uart_puts("scause = ");
        uart_put_hex(frame.scause);
        uart_putchar('\n');

        uart_puts("sepc = ");
        uart_put_hex(frame.sepc);
        uart_putchar('\n');

        uart_puts("stval = ");
        uart_put_hex(frame.stval);
        uart_putchar('\n');
    }

    const int task_id = current.task_id;
    task_destroy(&current, kernel_root);

    if (current.proc->exit_info().reason == TASK_EXIT_REQUESTED) {
        uart_puts("[task] user exit id=");
        uart_put_hex((unsigned long)task_id);
        uart_puts(" status=");
        uart_put_hex((unsigned long)current.proc->exit_info().status);
    } else {
        uart_puts("[task] user fault id=");
        uart_put_hex((unsigned long)task_id);
        uart_puts(" scause=");
        uart_put_hex(current.proc->exit_info().fault_scause);
    }
    uart_putchar('\n');
}

} // namespace tinyos::user_task
