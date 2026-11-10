#include "drivers/uart.h"

#include <stddef.h>

__attribute__((noreturn))
void kernel_panic(const char* file, int line, const char* message) {
    uart_puts("\n[panic] ");
    uart_puts(message != NULL ? message : "(no message)");
    uart_puts(" at ");
    uart_puts(file != NULL ? file : "(unknown)");
    uart_puts(":");
    uart_put_hex((unsigned long)line);
    uart_putchar('\n');

    for (;;) {
        asm volatile("wfi");
    }
}
