#include "uart.h"
#include "trap.h"
#include "timer.h"
#include "sched.h"
#include "pmm.h"

extern char __kernel_start[];
extern char __kernel_end[];

static void delay(void) {
    for (volatile unsigned long i = 0;
        i < 5000000UL;
    ++i) {
        asm volatile("" ::: "memory");
    }
}

static void task_a(void){
    for (;;){
        uart_putchar('A');
        delay();
    }
}

static void task_b(void){
    for (int i = 0; i < 10; ++i){
        uart_putchar('B');
        delay();
    }
}


void kernel_main(unsigned long hart_id, const void* device_tree) {
    uart_puts("\nHello TinyOS!\n");

    uart_puts("Boot hart: ");
    uart_put_hex(hart_id);
    uart_putchar('\n');

    uart_puts("Device tree: ");
    uart_put_hex((unsigned long)device_tree);
    uart_putchar('\n');

    uart_puts("Kernel range: ");
    uart_put_hex((unsigned long)__kernel_start);
    uart_puts(" - ");
    uart_put_hex((unsigned long)__kernel_end);
    uart_putchar('\n');

    if (pmm_init(__kernel_end, device_tree) < 0) {
        uart_puts("PMM initialization failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    uart_puts("PMM initialized. Available pages: ");
    uart_put_hex(pmm_available_pages());
    uart_putchar('\n');

    trap_init();
    uart_puts("Trap handler initialized.\n");

    sched_init();
    int task_a_id = task_create(task_a);
    int task_b_id = task_create(task_b);

    if (task_a_id < 0 ||
        task_b_id < 0) {
        uart_puts("Task creation failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    timer_init();
    uart_puts("Starting preemptive scheduler...\n");
    if (scheduler_start() < 0) {
        uart_puts("Scheduler failed to start.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    __builtin_unreachable();
}
