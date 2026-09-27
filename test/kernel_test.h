#ifndef TINYOS_KERNEL_TEST_H
#define TINYOS_KERNEL_TEST_H
#include <stdint.h>

#include "uart.h"

#define QEMU_TEST_DEV  0x100000UL
#define QEMU_EXIT_PASS 0x5555
#define QEMU_EXIT_FAIL 0x3333
static inline void qemu_exit(int pass) {
    volatile uint32_t *exit_dev = (volatile uint32_t *)QEMU_TEST_DEV;
    *exit_dev = pass ? QEMU_EXIT_PASS : QEMU_EXIT_FAIL;
}

// 打印简易行号（将整数转为十六进制或简易十进制输出）
static inline void test_print_location(const char* file, unsigned long line) {
    uart_puts(" at ");
    uart_puts(file);
    uart_puts(":");
    uart_put_hex(line); // 直接用现成的十六进制输出行号
    uart_putchar(' ');
}

#define TEST_HALT() do { \
    /* 关中断并永久挂起 CPU */ \
    for (;;) { \
        asm volatile("csrci sstatus, 2; wfi"); \
    } \
} while (0)

#define TEST_PANIC(msg) do { \
    uart_puts("[PANIC] "); \
    uart_puts(msg); \
    test_print_location(__FILE__, __LINE__); \
    uart_putchar('\n'); \
    qemu_exit(0); \
    TEST_HALT(); \
} while (0)

#define TEST_SUITE_PASS() do { \
    uart_puts("[PASS] test suite\n"); \
    qemu_exit(1); \
    TEST_HALT(); \
} while (0)

#define TEST_CHECK(cond, msg) do { \
    if (!(cond)) { \
        TEST_PANIC(msg); \
    } \
} while (0)

#define TEST_CHECK_EQ(a, b, msg) do { \
    unsigned long actual_ = (unsigned long)(a); \
    unsigned long expected_ = (unsigned long)(b); \
    if (actual_ != expected_) { \
        uart_puts("[FAIL] "); \
        uart_puts(msg); \
        test_print_location(__FILE__, __LINE__); \
        uart_puts(" (got: "); \
        uart_put_hex(actual_); \
        uart_puts(", expected: "); \
        uart_put_hex(expected_); \
        uart_puts(")\n"); \
        TEST_HALT(); \
    } \
} while (0)

#define TEST_PASS(name) do { \
    uart_puts("[PASS] "); \
    uart_puts(name); \
    uart_putchar('\n'); \
} while (0)

#endif