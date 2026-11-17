#include "console/console_service.hpp"


namespace tinyos::console {
    
namespace {
constinit ConsoleDevice g_console_device{}; 
constinit ConsoleInputMode g_input_mode{ConsoleInputMode::KernelDebugger};
}
bool console_user_io_enabled() noexcept {
    return g_console_device.ready() &&
        g_input_mode == ConsoleInputMode::UserProcesses;
}

int configure_console(
    ConsoleIo io,
    ConsoleInputMode mode
) noexcept {
    if (mode != ConsoleInputMode::UserProcesses &&
        mode != ConsoleInputMode::KernelDebugger) {
        return -1;
    }

    int res = g_console_device.configure(io);
    if (res == -1) {
        return res;
    }

    g_input_mode = mode;
    return 0;
}

bool console_ready() noexcept {
    return g_console_device.ready();
}

long poll_console_input() noexcept {
    return g_console_device.poll_input();
}

long read_console_current(
    std::span<unsigned char> destination
) noexcept {
    return g_console_device.read_current(destination);
}

int commit_console_read(
    size_t count
) noexcept {
    return g_console_device.commit_read(count);
}

long write_console(
    std::span<const unsigned char> source
) noexcept {
    return g_console_device.write(source);
}
}
