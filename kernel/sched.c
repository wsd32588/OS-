#include "sched.h"
#include "trap.h"
#include "uart.h"
#include "user_memory.h"

#define MAX_TASKS 3
#define STACK_SIZE 4096

#define SSTATUS_SPIE (1UL << 5)
#define SSTATUS_SPP (1UL << 8)

#define IDLE_TASK_ID (-1)

enum task_state {
    TASK_UNUSED,
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD,
    TASK_BLOCKED,
};

struct task {
    struct trap_frame* frame;

    unsigned char stack[STACK_SIZE]
        __attribute__((aligned(16)));

    void (*entry)(void);
    enum task_state state;
    unsigned long sleep_ticks;
    UserMemory user_memory;
};

static struct task tasks[MAX_TASKS];

static int task_count;
static int current_task;
static struct task idle_task;

extern void task_enter(
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

static unsigned long read_gp(void) {
    unsigned long value;

    asm volatile(
        "mv %0, gp"
        : "=r"(value)
    );

    return value;
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

static void task_prepare(
    struct task* task
) {
    /*
     * 计算内核栈顶地址
     * 栈顶对齐16bytes
     */
    unsigned long kernel_stack_top =
        (unsigned long)&task->stack[STACK_SIZE]; //当前内核栈顶设置为task->stack[STACK_SIZE]的地址
    kernel_stack_top &= ~0xFUL; // 对齐16bytes

    unsigned long frame_address =
        kernel_stack_top - sizeof(struct trap_frame); //计算frame的地址

    struct trap_frame* frame =
        (struct trap_frame*)frame_address;

    unsigned long* words = (unsigned long*)frame; //将frame的地址转换为unsigned long*类型

    for (unsigned long i = 0; i < sizeof(*frame) / sizeof(unsigned long); ++i) {
        words[i] = 0; //清零frame
    }

    /*
    *RISC-V ABI要求栈保持16byte alignment
    */
    /*
     * 所有任务共享一个kernel空间
     * 所以 gp 继承当前 kernel gp
     */
    frame->sp = kernel_stack_top; //设置frame的sp为kernel_stack_top
    frame->gp = read_gp(); //设置frame的gp为当前的gp

    task->frame = frame; //将frame赋值给task->frame
}

void sched_init(void) {
    task_count = 0;
    current_task = -1;

    task_prepare(&idle_task);

    idle_task.frame->sepc =
        (unsigned long)idle_entry; //将frame赋值给idle_task.frame
    idle_task.frame->sstatus =
        SSTATUS_SPP |
        SSTATUS_SPIE; //设置idle_task.frame的sstatus为SSTATUS_SPP | SSTATUS_SPIE

}

static int task_allocate(
    struct task** task_out
) {
    if (task_out == NULL ||
        task_count >= MAX_TASKS) {
        return -1;
    }
    /*
     * 分配一个新的任务结构体
     */
    struct task* task =
        &tasks[task_count];

    task->entry = NULL;
    task->state = TASK_READY;
    task->sleep_ticks = 0;
    task->user_memory = (UserMemory){0};

    task_prepare(task);

    int task_id = task_count; //获取当前task的id
    ++task_count; //增加task_count

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
    if (!entry ||
        task_count >= MAX_TASKS) {
        return -1;
    }

    struct task* task = NULL;
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
    task->frame->sepc =
        (unsigned long)task_trampoline;
    /*
     *SPP = 1
     *sret后回到S-mode
     *
     *SPIE = 1
     *sret 后SIE恢复到1
     */
    task->frame->sstatus =
        SSTATUS_SPP |
        SSTATUS_SPIE;

    return task_id;
}

int task_create_user(
    uintptr_t entry,
    uintptr_t user_stack_top,
    UserMemory* memory
) {
    if (entry == 0 ||
        memory == NULL ||
        memory->root == NULL ||
        memory->code_page == NULL ||
        memory->stack_page == NULL ||
        (entry & 1U) != 0 ||
        user_stack_top == 0 ||
        (user_stack_top & 0xFUL) != 0) {
        return -1;
        }

    struct task* task = NULL;
    int task_id = task_allocate(&task);

    if (task_id < 0) {
        return -1;
    }

    task->frame->sepc = (unsigned long)entry;
    task->frame->sp = (unsigned long)user_stack_top;

    /*
     *SPP = 0: sret后回到U-mode
     *SPIE = 1: sret后SIE恢复到1
     */
    task->frame->sstatus = SSTATUS_SPIE;

    /*
     *当前用户汇编不用gp,所以设置为0
     *不把内核 gp 暴露给用户初始上下文
     */
    task->frame->gp = 0;
    task->user_memory = *memory;
    *memory = (UserMemory){0};
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
        tasks[current_task].frame = frame;

        if (tasks[current_task].state == TASK_RUNNING) {
            tasks[current_task].state = TASK_READY;
        }
    }
    else {
        idle_task.frame = frame;
    }

    int next = find_next_runnable();

    if (next >= 0) {
        current_task = next;
        tasks[next].state = TASK_RUNNING;
        return tasks[next].frame;
    }

    /*
     *切换到下一个任务
    *返回值是下一个任务的TrapFrame
    *trap_entry操作会:
    *sp = 返回值(returned_value)
    *restore registers
    *sret
    */
    current_task = IDLE_TASK_ID;
    return idle_task.frame;
}

struct trap_frame* sched_on_timer(
    struct trap_frame* frame
) {

    if (frame == NULL) {
        return NULL;
    }

    wake_sleeping_tasks();

    return sched_reschedule(frame);
}

struct trap_frame* sched_on_yield(
    struct trap_frame* frame
) {
    if (task_count == 0 ||
        current_task < 0 ||
        frame == NULL
    ) {
        return frame;
    }

    return sched_reschedule(frame);
}

struct trap_frame* sched_on_exit(
    struct trap_frame* frame
) {
    if (task_count == 0 ||
        current_task < 0 ||
        frame == NULL
    ) {
        return frame;
    }

    tasks[current_task].state = TASK_DEAD;
    int res = user_memory_release(
        &tasks[current_task].user_memory);

    if (res != SV39_OK) {
        uart_puts("[user memory] release failed: ");
        uart_put_hex((unsigned long)res);
        uart_puts("\n");
        for (;;) {
            asm volatile("wfi");
        }
    }
    return sched_reschedule(frame);
}

struct trap_frame* sched_on_sleep(
    struct trap_frame* frame,
    unsigned long ticks
) {
    if (task_count == 0 ||
        current_task < 0 ||
        frame == NULL ||
        ticks == 0) {
            return frame;
        }

    tasks[current_task].state = TASK_BLOCKED;
    tasks[current_task].sleep_ticks = ticks;

    return sched_reschedule(frame);
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

    task_enter(tasks[first].frame);

    /*
    *task_enter最后返回执行sret
    *正常情况下永远不会返回
    */
    __builtin_unreachable();
}
