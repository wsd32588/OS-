#include "syscall.h"

#include "trap.h"
#include "uart.h"

void syscall_handle(
    struct trap_frame *frame
) {
    if (frame == NULL) {
        return;
    }

    switch (frame->a7) {
    case SYSCALL_PUTCHAR:
        uart_putchar(
            (char)(frame->a0 & 0xffUL)
        );
        frame->a0 = 0;
        break;

    default:
        frame->a0 = (u64)-1;
        break;
    }

    /*
     * ecall 永远是 32 位指令，即使启用了 C 扩展。
     * 如果不推进 sepc，sret 后会再次执行同一个 ecall。
     */
    frame->sepc += 4;
}
