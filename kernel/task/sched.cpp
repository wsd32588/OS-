#include "task/sched.h"

#include "mm/address.hpp"
#include "arch/riscv/task_context.h"
#include "arch/riscv/trap.h"
#include "arch/riscv/vm_arch.h"
#include "drivers/uart.h"
#include "mm/user_memory.h"
#include "process/process.hpp"
#include "process/spawn.hpp"

#define MAX_TASKS 4

#define SSTATUS_SPIE (1UL << 5)
#define SSTATUS_SPP (1UL << 8)

#define IDLE_TASK_ID (-1)

static const Sv39PageTable* g_kernel_root;
static uint64_t g_next_task_id;
static uint64_t g_user_reaper_id = -1;
enum task_state {
    TASK_UNUSED,
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD,
    TASK_BLOCKED,
};

enum task_kind {
    TASK_KIND_KERNEL,
    TASK_KIND_USER
};

struct task {
    RiscvTaskContext context;

    void (*entry)(void);
    int task_id;
    enum task_state state;
    enum task_kind kind;
    unsigned long sleep_ticks;
    tinyos::Process* proc;
};

static struct task tasks[MAX_TASKS];
static tinyos::Process g_process[MAX_TASKS];

static int task_count;
static int current_task;
static struct task idle_task;

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
        tasks[current_task].state = TASK_DEAD;
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
static void task_clear_metadata(struct task* target) {
    target->context.frame = nullptr;
    target->entry = nullptr;
    target->task_id = -1;
    target->kind = TASK_KIND_KERNEL;
    target->sleep_ticks = 0;
    target->proc = nullptr;
}

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
    for (int i = 0; i < task_count; ++i) {
        if (tasks[i].state == TASK_BLOCKED &&
            tasks[i].sleep_ticks > 0) {
            --tasks[i].sleep_ticks;

            if (tasks[i].sleep_ticks == 0) {
                tasks[i].state = TASK_READY;
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

    for (const tinyos::Process& proc : g_process) {
        if (!proc.resources_empty()) {
            return -1;
        }
    }

    for (tinyos::Process& proc : g_process) {
        if (proc.reset() != 0) {
            return -1;
        }
    }

    for (size_t i = 0; i < MAX_TASKS; ++i) {
        struct task* task = &tasks[i];
        task_clear_metadata(task);
        task->state = TASK_UNUSED;
    }



    g_kernel_root = kernel_root;
    g_next_task_id = 0;
    g_user_reaper_id = -1;
    task_count = 0;
    current_task = IDLE_TASK_ID;

    task_clear_metadata(&idle_task);
    idle_task.state = TASK_READY;
    riscv_task_context_init(&idle_task.context);

    idle_task.context.frame->sepc = (unsigned long)idle_entry;
    idle_task.context.frame->sstatus = SSTATUS_SPP | SSTATUS_SPIE;

    return 0;
}

static int task_allocate(
    struct task** task_out
) {
    /* The compiler limit avoids depending on hosted libc limits.h. */
    if (task_out == nullptr ||
        g_next_task_id > (uint64_t)__INT_MAX__) {
        return -1;
    }

    int slot_index = -1;

    for (int i = 0; i < task_count; ++i) {
        if (tasks[i].state == TASK_UNUSED) {
            slot_index = i;
            break;
        }
    }

    if (slot_index < 0) {
        if (task_count >= MAX_TASKS) {
            return -1;
        }
        slot_index = task_count;
    }

    if (g_process[slot_index].reset() != 0) {
        return -1;
    }

    struct task* task = &tasks[slot_index];

    task->proc = nullptr;
    task->entry = nullptr;
    task->state = TASK_READY;
    task->kind = TASK_KIND_KERNEL;
    task->sleep_ticks = 0;
    riscv_task_context_init(&task->context);

    const int task_id = (int)g_next_task_id;
    task->task_id = task_id;
    ++g_next_task_id;

    if (slot_index == task_count) {
        ++task_count;
    }

    *task_out = task;

    return task_id;
}

static void task_trampoline(void)
{
    void (*entry)(void) =
        tasks[current_task].entry;

    entry();

    uart_puts("\n[task] entry returned\n");

    task_exit();
}

int task_create(void (*entry)(void)) {
    if (entry == nullptr) {
        return -1;
    }

    struct task* task = nullptr;

    int task_id = task_allocate(&task);

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

int task_create_user(
    uintptr_t entry,
    uintptr_t user_stack_top,
    UserAddressSpace* space,
    uintptr_t user_argument
) {
    if (entry == 0 ||
        space == nullptr ||
        space->root == nullptr ||
        (entry & 1U) != 0 ||
        user_stack_top == 0 ||
        (user_stack_top & 0xFUL) != 0 ||
        space->root == space->kernel_root ||
        //有过error, 私有 root 本来就应该不同于内核 root，
        //所以用!=的话用户任务一定在这里返回 -1
        space->kernel_root != g_kernel_root ||
        space->user_memory.root != space->root
    ) {
        return -1;
    }

    const uintptr_t page_mask = ~((uintptr_t)SV39_PAGE_SIZE - 1U);
    const UserMemoryPage* code =
        user_memory_find_page(&space->user_memory, entry & page_mask);
    const UserMemoryPage* stack =
        user_memory_find_page(&space->user_memory, (user_stack_top - 1U) & page_mask);
    if (code == nullptr || stack == nullptr ||
        code->physical_page == nullptr || stack->physical_page == nullptr) {
        return -1;
    }

    struct task* task = nullptr;
    int task_id = task_allocate(&task);

    if (task_id < 0) {
        return -1;
    }

    task->context.frame->sepc = (unsigned long)entry;
    task->context.frame->sp = (unsigned long)user_stack_top;
    task->context.frame->a0 = (unsigned long)user_argument;

    /*
     *SPP = 0: sret后回到U-mode
     *SPIE = 1: sret后SIE恢复到1
     */
    task->context.frame->sstatus = SSTATUS_SPIE;

    /*
     *当前用户汇编不用gp,所以设置为0
     *不把内核 gp 暴露给用户初始上下文
     */
    task->context.frame->gp = 0;

    const auto slot_index = task - tasks;
    task->proc = &g_process[slot_index];
    task->proc->adopt(*space);
    task->kind = TASK_KIND_USER;
    *space = {};
    return task_id;
}

static int find_next_runnable(void) {
    if (task_count == 0) {
        return -1;
    }

    for (int offset = 1;
        offset <= task_count;
        ++offset) {
        int index =
            (current_task + offset) % task_count;

        if (tasks[index].state == TASK_READY) {
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

    if (current_task >= task_count) {
        return frame;
    }

    if (current_task >= 0) {
        tasks[current_task].context.frame = frame;

        if (tasks[current_task].state == TASK_RUNNING) {
            tasks[current_task].state = TASK_READY;
        }
    }
    else {
        idle_task.context.frame = frame;
    }

    int next = find_next_runnable();

    if (next >= 0) {
        current_task = next;
        tasks[next].state = TASK_RUNNING;
        change_page(&tasks[next]);
        return tasks[next].context.frame;
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

static int find_task_slot_by_id(int task_id) {
    if (task_id < 0) {
        return -1;
    }

    for (int i = 0; i < task_count; ++i) {
        if (tasks[i].state != TASK_UNUSED &&
            tasks[i].task_id == task_id) {
                return i;
            }
    }
    return -1;
}

int task_peek_exit(int task_id, TaskExitInfo *result_out) {
    if (result_out == nullptr) {
        return -1;
    }

    int target = find_task_slot_by_id(task_id);
    if (target == -1 ||
        target == current_task) {
        return -1;
    }

    task* task = &tasks[target];
    if (task->kind == TASK_KIND_KERNEL ||
        task->proc == nullptr) {
            return -1;
    }

    if (task->state == TASK_DEAD) {
        const TaskExitInfo& info = task->proc->exit_info();
        if (!task->proc->resources_empty() ||
            info.reason == TASK_EXIT_NONE) {
            return -1;
        }
        *result_out = info;
        return 1;
    }

    if (task->state != TASK_UNUSED) {
        return 0;
    }

    return 1;
}


int task_reap(int task_id) {
    int target = find_task_slot_by_id(task_id);

    if (target < 0 ||
        target == current_task) {
        return -1;
    }
    struct task* target_task = &tasks[target];

    if (target_task->kind != TASK_KIND_USER ||
        target_task->proc == nullptr ||
        target_task->state != TASK_DEAD) {
        return -1;
    }

    const TaskExitReason reason = target_task->proc->exit_info().reason;
    if (reason != TASK_EXIT_REQUESTED && reason != TASK_EXIT_FAULT) {
        return -1;
    }

    if (target_task->proc->reset() != 0) {
        return -1;
    }

    task_clear_metadata(target_task);
    target_task->state = TASK_UNUSED;
    return 0;

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
    if (task_count == 0 ||
        current_task < 0 ||
        frame == nullptr
    ) {
        return frame;
    }

    return sched_reschedule(frame);
}

static struct task* current_user_task(struct trap_frame* frame) {
    if (frame == nullptr ||
        task_count <= 0 || task_count > MAX_TASKS ||
        current_task < 0 ||
        current_task >= task_count) {
        return nullptr;
    }

    struct task* task = &tasks[current_task];
    if (task->state != TASK_RUNNING ||
        task->kind != TASK_KIND_USER ||
        task->proc == nullptr ||
        task->proc->root() == nullptr ||
        (frame->sstatus & SSTATUS_SPP) != 0) {
        return nullptr;
    }

    return task;
}

static int task_create_user_from_frame(
    const trap_frame& snapshot,
    UserAddressSpace& candidate
) {
    task* child = nullptr;
    const int task_id = task_allocate(&child);
    if (task_id < 0) {
        return -1;
    }

    *child->context.frame = snapshot;
    child->context.frame->a0 = 0;

    const auto slot_index = child - tasks;
    child->proc = &g_process[slot_index];
    child->proc->adopt(candidate);
    child->kind = TASK_KIND_USER;

    return task_id;
}

static int discard_fork_candidate(UserAddressSpace& candidate) {
    const int result = user_address_space_release(&candidate);
    if (result != SV39_OK) {
        uart_puts("fork candidate cleanup failed\n");
        for (;;) {
            asm volatile("wfi");
        }
    }
    return -1;
}

int sched_fork_current(struct trap_frame* frame) {
    struct task* parent = current_user_task(frame);
    if (parent == nullptr) {
        return -1;
    }

    unsigned long status = 0;
    asm volatile(
        "csrr %0, sstatus"
        : "=r"(status)
        :
        : "memory"
    );

    
    if ((status & (1UL << 1)) != 0) {
        return -1;
    }


    const VirtAddr pc(frame->sepc);
    const VirtAddr sp(frame->sp);

    if (!pc.is_valid() ||
        (pc.val() & 1UL) != 0 ||
        !sp.is_valid() ||
        sp.val() == 0 ||
        (sp.val() & 0xFUL) != 0) {
        return -1;
    }

    uint64_t physical_address{};
    uint64_t flags{};
    const Sv39PageTable* root = parent->proc->root();

    const uint64_t pc_flags = SV39_PTE_U | SV39_PTE_X;
    int res = root->query(pc.val(), &physical_address, &flags);

    if (res != SV39_OK ||
        (flags & pc_flags) != pc_flags) {
        return -1;
    }

    const uint64_t stack_flags =
        SV39_PTE_U | SV39_PTE_R | SV39_PTE_W;

    res = root->query(sp.val() - 1U, &physical_address, &flags);

    if (res != SV39_OK ||
        (flags & stack_flags) != stack_flags) {
        return -1;
    }

    const struct trap_frame snapshot = *frame;
    UserAddressSpace candidate{};

    const int result = parent->proc->clone_address_space(candidate);
    if (result != SV39_OK) {
        return discard_fork_candidate(candidate);
    }

    asm volatile("fence.i" ::: "memory");

    int new_id = task_create_user_from_frame(snapshot, candidate);

    if (new_id < 0) {
        return discard_fork_candidate(candidate);
    }
    return new_id;
}

[[noreturn]] static void exec_commit_fail(const char* reason) {
    uart_puts(reason);
    for (;;) {
        asm volatile("wfi");
    }
}

int sched_exec_current(
    struct trap_frame* frame,
    uintptr_t user_argument
) {
    task* current = current_user_task(frame);
    if (current == nullptr) {
        return -1;
    }

    unsigned long status = 0;
    asm volatile(
        "csrr %0, sstatus"
        : "=r"(status)
        :
        : "memory"
    );

    
    if ((status & (1UL << 1)) != 0) {
        return -1;
    }

    UserAddressSpace candidate{};
    uintptr_t entry{};
    uintptr_t stack_top{};

    int result = tinyos::prepare_builtin_user_space(
        candidate, entry, stack_top);
    
    if (result != SV39_OK) {
        if (user_address_space_release(&candidate) != SV39_OK) {
            uart_puts("exec: candidate cleanup failed\n");
            for (;;) {
                asm volatile("wfi");
            }
        }

        return -1;
    }

    const auto reject_candidate = [&candidate]() -> int {
        if (user_address_space_release(&candidate) != SV39_OK) {
            uart_puts("exec candidate cleanup failed\n");
            for (;;) {
                asm volatile("wfi");
            }
        }
        return -1;
    };

    if (candidate.kernel_root != g_kernel_root ||
        candidate.root == nullptr ||
        candidate.root == candidate.kernel_root ||
        candidate.root != candidate.user_memory.root ||
        candidate.user_memory.page_count == 0 ||
        candidate.user_memory.page_count > USER_MEMORY_MAX_PAGE) {
        return reject_candidate();
    }

    const VirtAddr pc(entry);
    const VirtAddr sp(stack_top);
    if (pc.val() == 0 || !pc.is_valid() ||
        (pc.val() & 1UL) != 0 ||
        sp.val() == 0 || !sp.is_valid() ||
        (sp.val() & 0xFUL) != 0) {
        return reject_candidate();
    }

    uint64_t physical_address{};
    uint64_t flags{};
    const Sv39PageTable* root = candidate.root;
    const uint64_t pc_flags = SV39_PTE_U | SV39_PTE_X;
    result = root->query(pc.val(), &physical_address, &flags);
    if (result != SV39_OK || (flags & pc_flags) != pc_flags) {
        return reject_candidate();
    }

    const uint64_t stack_flags =
        SV39_PTE_U | SV39_PTE_R | SV39_PTE_W;
    result = root->query(sp.val() - 1U, &physical_address, &flags);
    if (result != SV39_OK || (flags & stack_flags) != stack_flags) {
        return reject_candidate();
    }

    trap_frame fresh_frame{};
    fresh_frame.sepc = entry;
    fresh_frame.sp = stack_top;
    fresh_frame.a0 = user_argument;
    fresh_frame.sstatus = SSTATUS_SPIE;

    result = sv39_activate(g_kernel_root);
    if (result != SV39_OK) {
        exec_commit_fail("exec: activate kernel root failed\n");
    }

    result = current->proc->release_resources();
    if (result != SV39_OK) {
        exec_commit_fail("exec: release old space failed\n");
    }

    current->proc->adopt(candidate);
    *frame = fresh_frame;

    result = sv39_activate(current->proc->root());
    if (result != SV39_OK) {
        exec_commit_fail("exec: activate new root failed\n");
    }

    return 0;
}

int sched_configure_user_reaper(int task_id) {

}

/* Only used for the validated current user task; result and static stack survive. */
static void task_destroy(struct task* task) {
    task->state = TASK_DEAD;
    if (sv39_activate(g_kernel_root) != SV39_OK) {
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

static struct trap_frame* finish_user_task(
    struct trap_frame* frame,
    TaskExitReason reason,
    long status
) {
    struct task* task = current_user_task(frame);
    if (task == nullptr) {
        return frame;
    }

    task->proc->record_exit(TaskExitInfo{
        .reason = reason,
        .status = reason == TASK_EXIT_REQUESTED ? status : 0,
        .fault_scause = reason == TASK_EXIT_FAULT ? frame->scause : 0
    });

    if (reason == TASK_EXIT_FAULT) {
        uart_puts("\n[user fault]\n");
        uart_puts("scause = ");
        uart_put_hex(frame->scause);
        uart_putchar('\n');

        uart_puts("sepc = ");
        uart_put_hex(frame->sepc);
        uart_putchar('\n');

        uart_puts("stval = ");
        uart_put_hex(frame->stval);
        uart_putchar('\n');

    }

    const int task_id = task->task_id;
    task_destroy(task);

    if (task->proc->exit_info().reason == TASK_EXIT_REQUESTED) {
        uart_puts("[task] user exit id=");
        uart_put_hex((unsigned long)task_id);
        uart_puts(" status=");
        uart_put_hex((unsigned long)task->proc->exit_info().status);
    } else {
        uart_puts("[task] user fault id=");
        uart_put_hex((unsigned long)task_id);
        uart_puts(" scause=");
        uart_put_hex(task->proc->exit_info().fault_scause);
    }
    uart_putchar('\n');

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
    if (task_count == 0 ||
        current_task < 0 ||
        frame == nullptr ||
        ticks == 0) {
            return frame;
        }

    tasks[current_task].state = TASK_BLOCKED;
    tasks[current_task].sleep_ticks = ticks;

    return sched_reschedule(frame);
}

const Sv39PageTable* sched_current_user_root(void) {
    if (current_task < 0 || current_task >= task_count) {
        return nullptr;
    }

    const task& current = tasks[current_task];

    if (current.state != TASK_RUNNING ||
        current.kind != TASK_KIND_USER ||
        current.proc == nullptr) {
        return nullptr;
    }

    return current.proc->root();
}

int scheduler_start(void) {
    if (task_count == 0) {
        return -1;
    }

    int first = -1;

    for (int i = 0; i < task_count; ++i) {
        if (tasks[i].state == TASK_READY) {
            first = i;
            break;
        }
    }

    if (first < 0) {
        return -1;
    }

    current_task = first;
    tasks[first].state = TASK_RUNNING;

    change_page(&tasks[current_task]);
    task_enter(tasks[first].context.frame);

    /*
    *task_enter最后返回执行sret
    *正常情况下永远不会返回
    */
    __builtin_unreachable();
}
