#include "task/sched.h"

#include <stdint.h>

#include "arch/riscv/task_context.h"
#include "arch/riscv/trap.h"
#include "arch/riscv/vm_arch.h"
#include "drivers/uart.h"
#include "task/private/task_table.hpp"
#include "task/private/user_task.hpp"

namespace {
constinit tinyos::TaskTable g_task_table{};
}
#define SSTATUS_SPIE (1UL << 5)
#define SSTATUS_SPP (1UL << 8)

#define IDLE_TASK_ID (-1)

static const Sv39PageTable* g_kernel_root;
static int g_user_reaper_id = tinyos::kNoParentId;

static int current_task;
static struct task idle_task;

static struct task* current_user_task_metadata(void);


extern "C" void task_enter(
    struct trap_frame* frame
);

static void idle_entry(void) {
    uart_puts("[idle] entered\n");

    for (;;) {
        asm volatile("wfi");
    }
}

void task_exit(void)
{
    if (current_task >= 0) {
        g_task_table.at(current_task)->state = TASK_DEAD;
    }

    /*
     * 等下一次 timer interrupt
     * 把这个 DEAD task 换出去。
     */
    for (;;) {
        asm volatile("wfi");
    }
}

// 调用前，关联的 Process 已完成安全重置。


static void change_page(const struct task* task) {
    const Sv39PageTable* root = g_kernel_root;

    if (task->kind == TASK_KIND_USER) {
        if (task->proc == nullptr) {
            uart_puts("[Error] address of process is nullptr");
            
            for (;;) {
                asm volatile("wfi");
            }
        }

        root = task->proc->root();

        if (root == nullptr) {
            uart_puts("[Error] user task has valid root");
            // 内部状态错误：用户任务缺少私有根。
        }
    }

    int res = sv39_activate(root);
    if (res != SV39_OK) {
        uart_puts(" change page failed");

        for (;;) {
            asm volatile("wfi");
        }
    }
}

static void wake_sleeping_tasks(void) {
    for (int i = 0; i < g_task_table.high_water(); ++i) {
        if (g_task_table.at(i)->state == TASK_BLOCKED &&
            g_task_table.at(i)->sleep_ticks > 0) {
            --g_task_table.at(i)->sleep_ticks;

            if (g_task_table.at(i)->sleep_ticks == 0) {
                g_task_table.at(i)->state = TASK_READY;
            }
        }
    }
}

int sched_init(const Sv39PageTable* kernel_root) {
    if (kernel_root == nullptr) {
        return -1;
    }

    const uintptr_t root_address = (uintptr_t)kernel_root;
    if ((root_address >> 56U) != 0 || //检查物理地址编码范围
        (root_address & (SV39_PAGE_SIZE - 1U)) != 0) { // 检查对齐情况
        return -1;
    }

    if (g_task_table.reset() != 0) {
        return -1;
    }

    for (int i = 0; i < g_task_table.high_water(); ++i) {
        struct task* task = g_task_table.at(i);
        task->clear_metadata();
        task->state = TASK_UNUSED;
    }



    g_kernel_root = kernel_root;
    g_user_reaper_id = -1;
    current_task = IDLE_TASK_ID;

    idle_task.clear_metadata();
    idle_task.state = TASK_READY;
    riscv_task_context_init(&idle_task.context);

    idle_task.context.frame->sepc = (unsigned long)idle_entry;
    idle_task.context.frame->sstatus = SSTATUS_SPP | SSTATUS_SPIE;

    return 0;
}


static void task_trampoline(void)
{
    void (*entry)(void) =
        g_task_table.at(current_task)->entry;

    entry();

    uart_puts("\n[task] entry returned\n");

    task_exit();
}

int task_create(void (*entry)(void)) {
    if (entry == nullptr) {
        return -1;
    }

    struct task* task = nullptr;

    int task_id = g_task_table.allocate(&task);

    if (task_id < 0) {
        return -1;
    }
    /*
     *设置函数为entry,等待状态为READY
     */
    task->entry = entry;

    /*
     *sret后从task_trampoine 开始执行
     */
    task->context.frame->sepc =
        (unsigned long)task_trampoline;
    /*
     *SPP = 1
     *sret后回到S-mode
     *
     *SPIE = 1
     *sret 后SIE恢复到1
     */
    task->context.frame->sstatus =
        SSTATUS_SPP |
        SSTATUS_SPIE;

    return task_id;
}

static bool supervisor_interrupts_disabled(void) {
    unsigned long sstatus_value{};
    asm volatile(
        "csrr %0, sstatus"
        : "=r"(sstatus_value)
        :
        : "memory"
    );
    return (sstatus_value & (1UL << 1)) == 0;
}

static bool is_boot_task_context(void) {
    if (!supervisor_interrupts_disabled() ||
        current_task != IDLE_TASK_ID ||
        g_task_table.high_water() > tinyos::TaskTable::kCapacity) {
        return false;
    }

    for (int i = 0; i < g_task_table.high_water(); ++i) {
        if (g_task_table.at(i)->state == TASK_RUNNING) {
            return false;
        }
    }
    return true;
}

static int user_creation_parent_id(int& parent_id_out) {
    if (g_user_reaper_id == tinyos::kNoParentId) {
        if (!is_boot_task_context()) {
            return -1;
        }
        parent_id_out = tinyos::kNoParentId;
        return 0;
    }

    if (!supervisor_interrupts_disabled()) {
        return -1;
    }

    const struct task* parent = current_user_task_metadata();
    const int reaper_slot =
        tinyos::user_task::find_reaper(g_task_table, g_user_reaper_id);

    if (parent == nullptr || reaper_slot < 0) {
        return -1;
    }

    parent_id_out = parent->task_id;
    return 0;
}

int task_create_user(uintptr_t entry, uintptr_t stack_top,
                     UserAddressSpace* space, uintptr_t argument) {
    if (space == nullptr) {
        return -1;
    }
    int parent_id = tinyos::kNoParentId;
    if (user_creation_parent_id(parent_id) != 0) {
        return -1;
    }
    return tinyos::user_task::create(
        g_task_table, g_kernel_root, entry, stack_top,
        *space, argument, parent_id);
}

static int find_next_runnable(void) {
    if (g_task_table.high_water() == 0) {
        return -1;
    }

    for (int offset = 1;
        offset <= g_task_table.high_water();
        ++offset) {
        int index =
            (current_task + offset) % g_task_table.high_water();

        if (g_task_table.at(index)->state == TASK_READY) {
            return index;
        }
    }
    /*
    *找到下一个可运行的task
    */

    return -1;
}

static struct trap_frame* sched_reschedule(
    struct trap_frame* frame
) {

    if (current_task >= g_task_table.high_water()) {
        return frame;
    }

    if (current_task >= 0) {
        g_task_table.at(current_task)->context.frame = frame;

        if (g_task_table.at(current_task)->state == TASK_RUNNING) {
            g_task_table.at(current_task)->state = TASK_READY;
        }
    }
    else {
        idle_task.context.frame = frame;
    }

    int next = find_next_runnable();

    if (next >= 0) {
        current_task = next;
        g_task_table.at(next)->state = TASK_RUNNING;
        change_page(g_task_table.at(next));
        return g_task_table.at(next)->context.frame;
    }

    /*
     *切换到下一个任务
    *返回值是下一个任务的TrapFrame
    *trap_entry操作会:
    *sp = 返回值(returned_value)
    *restore registers
    *sret
    */
    change_page(&idle_task);

    current_task = IDLE_TASK_ID;
    return idle_task.context.frame;
}

static struct task* current_user_task_metadata(void) {
    if (g_task_table.high_water() <= 0 || g_task_table.high_water() > tinyos::TaskTable::kCapacity ||
        current_task < 0 || current_task >= g_task_table.high_water()) {
        return nullptr;
    }

    struct task* current = g_task_table.at(current_task);
    if (current->state != TASK_RUNNING ||
        current->kind != TASK_KIND_USER ||
        current->task_id < 0 ||
        current->proc == nullptr ||
        current->proc->root() == nullptr) {
        return nullptr;
    }

    return current;
}

static struct task* current_user_task_for_collection(void) {
    struct task* current = current_user_task_metadata();
    if (current == nullptr) {
        return nullptr;
    }

    if (!supervisor_interrupts_disabled()) {
        return nullptr;
    }

    return current;
}

int task_peek_exit(
    int requested_id,
    TaskExitInfo* result_out,
    int* matched_id_out
) {
    if (requested_id < -1 ||
        result_out == nullptr || matched_id_out == nullptr) {
        return -1;
    }

    const struct task* parent = current_user_task_for_collection();
    if (parent == nullptr) {
        return -1;
    }

    bool has_matching_child = false;
    for (int i = 0; i < g_task_table.high_water(); ++i) {
        const struct task& child = *g_task_table.at(i);
        if (i == current_task ||
            child.state == TASK_UNUSED ||
            child.kind != TASK_KIND_USER ||
            child.proc == nullptr ||
            child.proc->parent_id() != parent->task_id ||
            (requested_id >= 0 && child.task_id != requested_id)) {
            continue;
        }

        has_matching_child = true;
        // A later child may already be DEAD even when this one is still alive.
        if (child.state != TASK_DEAD) {
            continue;
        }

        const TaskExitInfo& info = child.proc->exit_info();
        if (!child.proc->resources_empty() ||
            (info.reason != TASK_EXIT_REQUESTED && info.reason != TASK_EXIT_FAULT)) {
            return -1;
        }

        *matched_id_out = child.task_id;
        *result_out = info;
        return 1;
    }

    return has_matching_child ? 0 : -1;
}


int task_reap(int task_id) {
    if (task_id < 0) {
        return -1;
    }

    const struct task* parent = current_user_task_for_collection();
    if (parent == nullptr) {
        return -1;
    }

    const int target = g_task_table.find_slot(task_id);
    if (target < 0 ||
        target == current_task) {
        return -1;
    }
    struct task* target_task = g_task_table.at(target);

    if (target_task->kind != TASK_KIND_USER ||
        target_task->proc == nullptr ||
        target_task->proc->parent_id() != parent->task_id ||
        target_task->state != TASK_DEAD) {
        return -1;
    }

    const TaskExitReason reason = target_task->proc->exit_info().reason;
    if (reason != TASK_EXIT_REQUESTED && reason != TASK_EXIT_FAULT) {
        return -1;
    }

    return g_task_table.recycle_dead_slot(target);

}
struct trap_frame* sched_on_timer(
    struct trap_frame* frame
) {

    if (frame == nullptr) {
        return nullptr;
    }

    wake_sleeping_tasks();

    return sched_reschedule(frame);
}

struct trap_frame* sched_on_yield(
    struct trap_frame* frame
) {
    if (g_task_table.high_water() == 0 ||
        current_task < 0 ||
        frame == nullptr
    ) {
        return frame;
    }

    return sched_reschedule(frame);
}

static struct task* current_user_task(struct trap_frame* frame) {
    if (frame == nullptr ||
        (frame->sstatus & SSTATUS_SPP) != 0) {
        return nullptr;
    }

    return current_user_task_metadata();
}


int sched_fork_current(trap_frame* frame) {
    task* parent = current_user_task(frame);
    if (parent == nullptr || !supervisor_interrupts_disabled()) {
        return -1;
    }

    return tinyos::user_task::fork(
        g_task_table, *parent, *frame);
}


int sched_exec_current(
    struct trap_frame* frame,
    uint64_t program_id,
    uintptr_t user_argument
) {
    task* current = current_user_task(frame);
    if (current == nullptr) {
        return -1;
    }

    if (!supervisor_interrupts_disabled()) {
        return -1;
    }

    return tinyos::user_task::exec(*current,
        *frame, g_kernel_root, program_id, user_argument);
}

int sched_configure_user_reaper(int task_id) {

    if (g_user_reaper_id != tinyos::kNoParentId ||
        !is_boot_task_context()) {
        return -1;
    }

    for (int i = 0; i < g_task_table.high_water(); ++i) {
        const struct task& target = *g_task_table.at(i);
        if (target.state == TASK_UNUSED ||
            target.kind != TASK_KIND_USER) {
            continue;
        }

        if (target.proc == nullptr ||
            target.proc->parent_id() != tinyos::kNoParentId) {
            return -1;
        }
    }

    const int slot_index = g_task_table.find_slot(task_id);
    if (slot_index < 0) {
        return -1;
    }

    struct task* reaper = g_task_table.at(slot_index);

    if (reaper->kind != TASK_KIND_USER ||
        reaper->proc == nullptr ||
        reaper->proc->root() == nullptr) {
        return -1;
    }

    for (int i = 0; i < tinyos::TaskTable::kCapacity; ++i) {
        if (g_task_table.at(i)->state != TASK_UNUSED &&
            g_task_table.at(i)->state != TASK_READY) {
            return -1;
        }
    }
    for (int i = 0; i < g_task_table.high_water(); ++i) {
        struct task& target = *g_task_table.at(i);

        if (target.state == TASK_UNUSED ||
            target.kind != TASK_KIND_USER) {
                continue;
        }

        target.proc->set_parent_id(
            target.task_id == task_id
            ? tinyos::kNoParentId
            : task_id);
    }

    g_user_reaper_id = task_id;
    return 0;
}


static struct trap_frame* finish_user_task(
    struct trap_frame* frame,
    TaskExitReason reason,
    long status
) {
    struct task* task = current_user_task(frame);
    if (task == nullptr) {
        return frame;
    }

    if (!supervisor_interrupts_disabled()) {
        return frame;
    }
    tinyos::user_task::finish(
        g_task_table, *task, *frame, g_kernel_root,
        g_user_reaper_id, reason, status);
    return sched_reschedule(frame);
}

struct trap_frame* sched_on_exit(
    struct trap_frame* frame,
    long status
) {
    return finish_user_task(frame, TASK_EXIT_REQUESTED, status);
}

struct trap_frame* sched_on_user_fault(
    struct trap_frame* frame
) {
    return finish_user_task(frame, TASK_EXIT_FAULT, 0);
}

struct trap_frame* sched_on_sleep(
    struct trap_frame* frame,
    unsigned long ticks
) {
    if (g_task_table.high_water() == 0 ||
        current_task < 0 ||
        frame == nullptr ||
        ticks == 0) {
            return frame;
        }

    g_task_table.at(current_task)->state = TASK_BLOCKED;
    g_task_table.at(current_task)->sleep_ticks = ticks;

    return sched_reschedule(frame);
}

const Sv39PageTable* sched_current_user_root(void) {
    const struct task* current = current_user_task_metadata();
    return current == nullptr ? nullptr : current->proc->root();
}

int scheduler_start(void) {
    if (g_task_table.high_water() == 0) {
        return -1;
    }

    if (g_user_reaper_id == tinyos::kNoParentId) {
        for (int i = 0; i < g_task_table.high_water(); ++i) {
            if (g_task_table.at(i)->state != TASK_UNUSED &&
                g_task_table.at(i)->kind == TASK_KIND_USER) {
                return -1;
            }
        }
    }

    int first = -1;

    for (int i = 0; i < g_task_table.high_water(); ++i) {
        if (g_task_table.at(i)->state == TASK_READY) {
            first = i;
            break;
        }
    }

    if (first < 0) {
        return -1;
    }

    current_task = first;
    g_task_table.at(first)->state = TASK_RUNNING;

    change_page(g_task_table.at(current_task));
    task_enter(g_task_table.at(first)->context.frame);

    /*
    *task_enter最后返回执行sret
    *正常情况下永远不会返回
    */
    __builtin_unreachable();
}
