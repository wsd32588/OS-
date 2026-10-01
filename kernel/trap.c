#include "trap.h"
#include "uart.h"
#include "timer.h"
#include "sched.h"
extern void trap_entry(void);

#define SCAUSE_INTERRUPT_BIT (1UL << 63)
#define SCAUSE_CODE_MASK (~SCAUSE_INTERRUPT_BIT)
#define SSTATUS_SPP (1UL << 8)

#define IRQ_S_TIMER 5

void trap_init(void) {
    unsigned long addr = (unsigned long)trap_entry;

    asm volatile(
        "csrw sscratch, zero\n"
        "csrw stvec, %0"
        :
        : "r"(addr)
        : "memory"
    );
}

struct trap_frame* trap_handler(struct trap_frame* frame) {
    unsigned long scause =
        frame->scause;

    unsigned long is_interrupt =
        scause & SCAUSE_INTERRUPT_BIT;

    unsigned long code =
        scause & SCAUSE_CODE_MASK;

    if (is_interrupt &&
        code == IRQ_S_TIMER) {
        timer_set_next();

        if ((frame->sstatus & SSTATUS_SPP) == 0) {
            uart_puts("[timer] tick in U-mode\n");
        } else {
            uart_puts("[timer] tick in S-mode\n");
        }

        return sched_on_timer(frame);
    }

    uart_puts("\n=== TRAP ===\n");

    uart_puts("scause = ");
    uart_put_hex(frame->scause);
    uart_putchar('\n');

    uart_puts("sepc = ");
    uart_put_hex(frame->sepc);
    uart_putchar('\n');

    uart_puts("stval = ");
    uart_put_hex(frame->stval);
    uart_putchar('\n');

    if (!is_interrupt &&
        code == 2) {
        uart_puts("Illegal instruction\n");
    }

    uart_puts("kernel halt.\n");

    for (;;) {
        asm volatile("wfi");
    }
}
