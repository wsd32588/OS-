#include "syscall.h"

#include "trap.h"
#include "uart.h"
#include "sched.h"

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
