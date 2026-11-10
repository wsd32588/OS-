#include "task/syscall.h"

#include "task/task_exit_result.h"
#include "arch/riscv/trap.h"
#include "drivers/uart.h"
#include "task/sched.h"
#include "mm/user_access.h"
#include "task/timer.h"
#include "process/spawn.h"

static uint64_t syscall_write(
    uintptr_t source_address,
    uint64_t length
) {
    if (length == 0) {
        return 0;
    }
    else if (length > SYSCALL_WRITE_MAX) {
        return (uint64_t) - 1;
    }

    const Sv39PageTable* root =
        sched_current_user_root();
    if (root == NULL) {
        return (uint64_t) - 1;
    }

    unsigned char buffer[SYSCALL_WRITE_MAX];
    int res = user_copy_from(root, buffer, source_address, length);

    if (res != SV39_OK) {
        return (uint64_t)- 1;
    }

    for (size_t i = 0; i < length ;++i) {
        uart_putchar(buffer[i]);
    }

    return length;
}

static long syscall_get_time(
    uintptr_t destination_address
) {
    const Sv39PageTable* root =
        sched_current_user_root();

    if (root == NULL) {
        return -1;
    }

    const uint64_t ticks = timer_read_time();
    int result = user_copy_to(
        root,
        destination_address,
        &ticks,
        sizeof(ticks)
    );

    if (result != SV39_OK) {
        return -1;
    }

    return 0;
}

static long syscall_waitpid(
    long requested_id,
    uintptr_t result_address) {
    if (requested_id < -1 ||
        requested_id > __INT_MAX__) {
            return -1;
        }
    const int task_id = (int)requested_id;
    
    TaskExitInfo info = {0};
    int match_id = -1;
    int res = task_peek_exit(task_id, &info, &match_id);

    if (res == 0) {
        return -2;
    }
    if (res != 1) {
        return res;
    }

    TaskExitResult result = {0};

    switch (info.reason) {
        case TASK_EXIT_REQUESTED:
            result.reason = TASK_RESULT_REQUESTED;
            result.status = (int64_t)info.status;
            break;
        case TASK_EXIT_FAULT:
            result.reason = TASK_RESULT_FAULT;
            result.fault_scause = (uint64_t)info.fault_scause;
            break;
        default:
            return -1;
    }
    const Sv39PageTable* root = sched_current_user_root();

    if (root == NULL) {
        return -1;
    }

    if (user_copy_to(
        root, result_address,
        &result, sizeof(result)) != SV39_OK) {
            return -1;
        }
    
    if (task_reap(match_id) != 0) {
        uart_puts("[reap] commit failed\n");
        for (;;) {
            asm volatile("wfi");
        }
    }

    return match_id;
}

static long syscall_reap(
    long requested_id,
    uintptr_t result_address
) {
    if (requested_id < 0) {
        return -1;
    }

    const long result =
        syscall_waitpid(requested_id, result_address);

    if (result >= 0) {
        return 1;
    }
    if (result == -2) {
        return 0;
    }
    return -1;
}

struct trap_frame* syscall_handle(
    struct trap_frame *frame
) {
    if (frame == NULL) {
        return NULL;
    }

    frame->sepc += 4;

    switch (frame->a7) {
    case SYSCALL_PUTCHAR:
        uart_putchar(
            (char)(frame->a0 & 0xffUL)
        );
        frame->a0 = 0;
        break;

    case SYSCALL_YIELD:
        frame->a0 = 0;
        uart_puts("[syscall] yield\n");
        return sched_on_yield(frame);

    case SYSCALL_EXIT: {
        long status = (long)frame->a0;
        uart_puts("[syscall] exit\n");
        return sched_on_exit(frame, status);
    }
    case SYSCALL_SLEEP:{
        uint64_t ticks = frame->a0;
        frame->a0 = 0;
        uart_puts("[syscall] sleep\n");
        return sched_on_sleep(frame, ticks);
    }
    case SYSCALL_WRITE: {
        uintptr_t source_address = (uintptr_t)frame->a0;
        uint64_t length = (uint64_t)frame->a1;
        frame->a0 = syscall_write(source_address, length);
        break;
    }
    case SYSCALL_GET_TIME: {
        uintptr_t destination_address = (uintptr_t)frame->a0;
        frame->a0 = syscall_get_time(destination_address);
        break;
    }
    case SYSCALL_REAP: {
        long requested_id = (long)frame->a0;
        uintptr_t result_address = (uintptr_t)frame->a1;

        frame->a0 = (uint64_t)syscall_reap(
            requested_id,
            result_address
        );
        break;
    }
    case SYSCALL_SPAWN: {
        uintptr_t user_argument = (uintptr_t)frame->a0;
        int new_id = process_spawn_builtin(user_argument);
        frame->a0 = (uint64_t)(long)new_id;
        break;
    }
    case SYSCALL_FORK: {
        const int new_id = sched_fork_current(frame);
        frame->a0 = (uint64_t)(long)new_id;
        break;
    }
    case SYSCALL_EXEC: {
        const uintptr_t user_argument = (uintptr_t)frame->a0;
        if (sched_exec_current(frame, user_argument) != 0) {
            frame->a0 = (uint64_t)(long)-1;
        }
        break;
    }
    case SYSCALL_WAITPID: {
        const long requested_id = (long)frame->a0;
        const uintptr_t result_address = (uintptr_t)frame->a1;
        frame->a0 = (uint64_t)syscall_waitpid(
            requested_id, result_address);
        break;
    }
    default:
        frame->a0 = (uint64_t)-1;
        break;
    }

    /*
     * ecall 永远是 32 位指令，即使启用了 C 扩展。
     * 如果不推进 sepc，sret 后会再次执行同一个 ecall。
     */
    return frame;
}
