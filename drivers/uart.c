#include "uart.h"

#define UART0_BASE 0x10000000UL
#define UART_THR_OFFSET 0
#define UART_LSR_OFFSET 5
#define UART_LSR_THRE (1U << 5)

void uart_putchar(char ch) {
    volatile unsigned char *uart =
        (volatile unsigned char *)UART0_BASE;

    while ((uart[UART_LSR_OFFSET] &
            UART_LSR_THRE) == 0) {
    }

    uart[UART_THR_OFFSET] = (unsigned char)ch;
}

void uart_puts (const char* str) {
    while (*str != '\0') {
        uart_putchar(*str++);
    }
}

void uart_put_hex(unsigned long value) {
    static const char hex[] = "0123456789abcdef";

    uart_puts("0x");

    for (int shift = 60; shift >= 0; shift -= 4) {
        unsigned long digit = (value >> shift) & 0xf;
        uart_putchar(hex[digit]);
    }
}
