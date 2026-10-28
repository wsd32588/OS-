#include "boot_private.h"

#include "drivers/uart.h"

static void delay(void) {
    for (volatile unsigned long i = 0;
        i < 5000000UL;
    ++i) {
        asm volatile("" ::: "memory");
    }
}

void task_a(void){
    for (;;){
        uart_putchar('A');
        delay();
    }
}

void task_b(void){
    for (int i = 0; i < 10; ++i){
        uart_putchar('B');
        delay();
    }
}
