#ifndef TINYOS_CONSOLE_INPUT_QUEUE_HPP
#define TINYOS_CONSOLE_INPUT_QUEUE_HPP

#include <stddef.h>
#include <span>

namespace tinyos::console {
class InputQueue {
public:
    static constexpr size_t kCapacity = 256;

    constexpr InputQueue() noexcept = default;

    bool try_push(unsigned char byte) noexcept;
    size_t read_current(std::span<unsigned char> destination) const noexcept;
    int commit_read(size_t count) noexcept;

    size_t size() const noexcept;
    bool empty() const noexcept;
    bool full() const noexcept;

private:
    unsigned char bytes_[kCapacity]{};
    size_t head_{}; // 队首下标
    size_t size_{}; // 尚未消费的字节数
};
}

#endif