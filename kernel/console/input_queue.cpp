#include "console/input_queue.hpp"

namespace tinyos::console {
size_t InputQueue::size() const noexcept {
    return size_;
}

bool InputQueue::empty() const noexcept {
    return size_ == 0;
}

bool InputQueue::full() const noexcept {
    return size_ == kCapacity;
}

bool InputQueue::try_push(
    unsigned char byte    
) noexcept {
    if (full()) {
        return false;
    }

    size_t position = (head_ + size_) % kCapacity;

    bytes_[position] = byte;
    ++size_;
    return true;
}

size_t InputQueue::read_current(
    std::span<unsigned char> destination
) const noexcept {
    if (destination.empty()) {
        return 0;
    }

    const size_t count = size_ > destination.size()
        ? destination.size()
        : size_;

    for (size_t i = 0; i < count; ++i) {
        const size_t position = (head_ + i) % kCapacity;
        destination[i] = bytes_[position];
    }

    return count;
}

int InputQueue::commit_read(
    size_t count
) noexcept {
    if (count > size_) {
        return -1;
    }

    head_ = (head_ + count) % kCapacity;
    size_ -= count;
    return 0;
}
}