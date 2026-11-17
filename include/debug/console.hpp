#ifndef TINYOS_DEBUG_CONSOLE_HPP
#define TINYOS_DEBUG_CONSOLE_HPP

#include "debug/console_output.hpp"
#include <stddef.h>
#include <span>

namespace tinyos::debug {
    
class KernelConsole {
public:
    static constexpr size_t kLineCapacity = 160;

    constexpr explicit KernelConsole(ConsoleOutput output) noexcept :
        output_(output) {};

    void start() noexcept;
    void feed(unsigned char byte) noexcept;

private:
    void finish_line() noexcept;
    void execute_line(std::span<const char> line) noexcept;
    void write_text(std::span<const char> bytes) const noexcept;

    ConsoleOutput output_{};
    char line_[kLineCapacity]{};
    size_t length_{};
    bool discard_line_{};
    bool skip_lf_{};
};
}

#endif