#include "sched.h"
#include "timer.h"
#include "trap.h"
#include "uart.h"

#define MAX_TASKS 2
#define STACK_SIZE 4096

#define SSTATUS_SPIE (1UL << 5)
#define SSTATUS_SPP (1UL << 8)

enum task_state {
    TASK_UNUSED,
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD,
};

struct task {
    struct trap_frame* frame;

    unsigned char stack[STACK_SIZE]
        __attribute__((aligned(16)));

    void (*entry)(void);
    enum task_state state;
};

static struct task tasks[MAX_TASKS];

static int task_count;
static int current_task;

void sched_init(void) {
    task_count = 0;
    current_task = -1;
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

static unsigned long read_gp(void) {
    unsigned long value;

    asm volatile(
        "mv %0, gp"
        : "=r"(value)
    );

    return value;
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
    if (!entry ||
        task_count >= MAX_TASKS) {
        return -1;
    }

    struct task* task =
         &tasks[task_count];

/*
*设置函数为entry,等待状态为READY
*/
    task->entry = entry;
    task->state = TASK_READY;

    unsigned long stack_top =
        (unsigned long)&task->stack[STACK_SIZE];

    /*
    *栈顶16-byte对齐
    */
    stack_top &= ~0xFUL;

    /*
    *任务栈顶人工放一个TrapFrame
    */

    unsigned long initial_sp = stack_top;

    stack_top -= sizeof(struct trap_frame);
    struct trap_frame* frame =
        (struct trap_frame*)stack_top;

    /*
    *清零frame
    */
    unsigned long *p =
        (unsigned long*)frame;
    for(unsigned long i = 0;
        i < sizeof(*frame) / sizeof(unsigned long);
        i++) {
            p[i] = 0;
        }

    frame->sp = initial_sp;
    /*
    *sret后从task_trampoine 开始执行
    */
    frame->sepc =
        (unsigned long)task_trampoline;
    /*
    *SPP = 1
    *sret后回到S-mode
    *
    *SPIE = 1
    *sret 后SIE恢复到1
    */
    frame->sstatus =
        SSTATUS_SPP |
        SSTATUS_SPIE;

    /*
    *RISC-V ABI要求栈保持16byte alignment
    */
    /*
     * 所有任务共享一个kernel空间
     * 所以 gp 继承当前 kernel gp
     */

    frame->gp = read_gp();
    task->frame = frame;
    ++task_count;

    return task_count - 1;
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

struct trap_frame* sched_on_timer(
    struct trap_frame* frame
) {
    if (task_count == 0 ||
        current_task < 0
    ) {
        return frame;
    }
    /*
     * 当前任务被 timer 打断，
     * 保存完整 CPU 状态。
     */

    tasks[current_task].frame = frame;
     /*
     * 如果它仍然正常运行，
     * 那么时间片结束后重新回到 READY。
     */

    if (tasks[current_task].state == TASK_RUNNING) {
        tasks[current_task].state = TASK_READY;
    }

    int next = find_next_runnable();

    if (next < 0) {
        /*
         * 当前任务已经结束，并且没有其他 READY 任务。
         * 关闭 timer interrupt，进入当前阶段的终止状态。
         */
        uart_puts(
            "\n[scheduler] no runnable tasks\n"
        );
        timer_stop();

        for (;;) {
            asm volatile("wfi");
        }
    }

    current_task = next;
    tasks[next].state = TASK_RUNNING;
    /*
    *返回另一个任务的TrapFrame
    *trap_entry操作会:
    *sp = 返回值(returned_value)
    *restore registers
    *sret
    */
    return tasks[next].frame;
}

extern void task_enter(
    struct trap_frame* frame
);

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

    task_enter(tasks[first].frame);

    /*
    *task_enter最后返回执行sret
    *正常情况下永远不会返回
    */
    __builtin_unreachable();
}
