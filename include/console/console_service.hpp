#ifndef TINYOS_CONSOLE_CONSOLE_SERVICE_HPP
#define TINYOS_CONSOLE_CONSOLE_SERVICE_HPP
#include "console_device.hpp"

namespace tinyos::console {

enum class ConsoleInputMode {
    KernelDebugger,
    UserProcesses,
};

int configure_console(
    ConsoleIo io,
    ConsoleInputMode mode = ConsoleInputMode::KernelDebugger) noexcept;
bool console_user_io_enabled() noexcept;
bool console_ready() noexcept;
long poll_console_input() noexcept;
long read_console_current(std::span<unsigned char> destination) noexcept;
int commit_console_read(size_t count) noexcept;
long write_console(std::span<const unsigned char> source) noexcept;
}
#endif