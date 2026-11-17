#ifndef TINYOS_CONSOLE_CONSOLE_DEVICE_HPP
#define TINYOS_CONSOLE_CONSOLE_DEVICE_HPP
#include <span>
#include "console/input_queue.hpp"

namespace tinyos::console {   
struct ConsoleIo {
    void* context{};
    int (*try_read)(void*) noexcept{};
    void (*write) (
        void*, std::span<const unsigned char>
    ) noexcept{};
};

class ConsoleDevice {
public:
    constexpr ConsoleDevice() noexcept = default;
    ConsoleDevice (const ConsoleDevice&) = delete;
    ConsoleDevice& operator=(const ConsoleDevice&) = delete;
    ConsoleDevice (ConsoleDevice&&) = delete;
    ConsoleDevice& operator=(ConsoleDevice&&) = delete;

    int configure(ConsoleIo io) noexcept;
    bool ready() const noexcept;
    long poll_input() noexcept;
    long read_current(std::span<unsigned char> destination) const noexcept;
    int commit_read(size_t count) noexcept;
    long write(std::span<const unsigned char> source) const noexcept;
private:
    InputQueue input_{};
    ConsoleIo io_{};
};
}
#endif