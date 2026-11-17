#include "console/console_device.hpp"
#include "runtime/panic.h"

namespace tinyos::console {
bool ConsoleDevice::ready() const noexcept {
    return io_.try_read != nullptr;
}

int ConsoleDevice::configure(
    ConsoleIo io
) noexcept {
    if (ready() ||
        io.try_read == nullptr ||
        io.write == nullptr) {
        return -1;
    }

    io_ = io;
    return 0;
}

long ConsoleDevice::poll_input() noexcept {
    if (!ready()) {
        return -1;
    }

    if (input_.full()) {
        return 0;
    }

    const int byte = io_.try_read(io_.context);
    if (byte == -1) {
        return 0;
    }

    if (byte < 0 || byte > 255) {
        PANIC_KERNEL("console input callback returned invalid byte");
    }

    if (!input_.try_push(static_cast<unsigned char>(byte))) {
        PANIC_KERNEL("console input enqueue failed");
    }
    return 1;
}

long ConsoleDevice::read_current(
    std::span<unsigned char> destination
) const noexcept {
    if (!ready()) {
        return -1;
    }

    return static_cast<long>(input_.read_current(destination));
}

int ConsoleDevice::commit_read(
    size_t count
) noexcept {
    if (!ready()) {
        return -1;
    }
    return input_.commit_read(count);
}

long ConsoleDevice::write(
    std::span<const unsigned char> source
) const noexcept {
    if (!ready() ||
        source.size() > __LONG_MAX__) {
        return -1;
    }

    if(source.empty()) {
        return 0;
    }

    io_.write(
        io_.context,
        source
    );
    return static_cast<long>(source.size());
}
}