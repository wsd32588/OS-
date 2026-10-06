#include "syscall.h"

#include "trap.h"
#include "uart.h"
#include "sched.h"
#include "user_access.h"

static u64 syscall_write(
    uintptr_t source_address,
    u64 length
) {
    if (length == 0) {
        return 0;
    }
    else if (length > SYSCALL_WRITE_MAX) {
        return (u64) - 1;
    }

    const Sv39PageTable* root =
        sched_current_user_root();
    if (root == NULL) {
        return (u64) - 1;
    }

    unsigned char buffer[SYSCALL_WRITE_MAX];
    int res = user_copy_from(root, buffer, source_address, length);

    if (res != SV39_OK) {
        return (u64)- 1;
    }

    for (size_t i = 0; i < length ;++i) {
        uart_putchar(buffer[i]);
    }

    return length;
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

    case SYSCALL_EXIT:
        uart_puts("[syscall] exit\n");
        return sched_on_exit(frame);

    case SYSCALL_SLEEP:
        unsigned long ticks = frame->a0;
        frame->a0 = 0;
        uart_puts("[syscall] sleep\n");
        return sched_on_sleep(frame, ticks);
    case SYSCALL_WRITE:
        uintptr_t source_address = (uintptr_t)frame->a0;
        u64 length = (u64)frame->a1;
        frame->a0 = syscall_write(source_address, length);
        break;
    default:
        frame->a0 = (u64)-1;
        break;
    }

    /*
     * ecall 永远是 32 位指令，即使启用了 C 扩展。
     * 如果不推进 sepc，sret 后会再次执行同一个 ecall。
     */
    return frame;
}
