#ifndef TINYOS_DEBUG_CONSOLE_OUTPUT_HPP
#define TINYOS_DEBUG_CONSOLE_OUTPUT_HPP

#include <span>

namespace tinyos::debug {
struct ConsoleOutput {
    void* context{};
    void (*write)(void*, std::span<const char>) noexcept{};
};
}

#endif
