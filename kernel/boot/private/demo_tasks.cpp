#include "boot_private.h"

#include "drivers/uart.h"
#include "runtime/panic.h"
#include "boot_private.h"
#include "debug/console.hpp"
#include "console/console_device.hpp"
#include "console/console_service.hpp"
#include <span>

static void delay(void) {
    for (volatile unsigned long i = 0;
        i < 5000000UL;
    i = i + 1UL) {
        asm volatile("" ::: "memory");
    }
}

static void write_console(
    void*, std::span<const char> bytes
) noexcept {
    const std::span<const unsigned char> source{
        reinterpret_cast<const unsigned char*>(bytes.data()),
        bytes.size()
    };

    const long written = tinyos::console::write_console(source);
    if (written < 0 ||
        static_cast<size_t>(written) != bytes.size()) {
        PANIC_KERNEL("console output failed");
    }
}


static int read_uart(void*) noexcept {
    return uart_try_getchar();
}

static void write_uart(void*, std::span<const unsigned char> bytes) noexcept {
    for (unsigned char byte : bytes) {
        uart_putchar(static_cast<char>(byte));
    }
}

int boot_configure_console() {
#if TINYOS_CONSOLE_USER_IO
    return tinyos::console::configure_console(
        tinyos::console::ConsoleIo{
            nullptr, read_uart, write_uart
        }, 
        tinyos::console::ConsoleInputMode::UserProcesses
    );
#else
    return tinyos::console::configure_console(
        tinyos::console::ConsoleIo{
            nullptr, read_uart, write_uart});
#endif
}

void task_a(void){
    tinyos::debug::KernelConsole console{{nullptr, write_console}};
    unsigned char buffer[32]{};

    console.start();
    for (;;){
        if (tinyos::console::poll_console_input() < 0){
            PANIC_KERNEL("console input polling failed");
        }
        const long count =
            tinyos::console::read_console_current(buffer);

        if (count < 0) {
            PANIC_KERNEL("console input read failed");
        }
        if (count == 0) {
            delay();
            continue;
        }

        const size_t actual_count =
            static_cast<size_t>(count);

        for (size_t i = 0; i < actual_count; ++i) {
            console.feed(buffer[i]);
        }

        if (tinyos::console::commit_console_read(actual_count) != 0) {
            PANIC_KERNEL("console input commit failed");
        }
    }
}

void task_b(void){
    for (int i = 0; i < 10; ++i){
        uart_putchar('B');
        delay();
    }
}
