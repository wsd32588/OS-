#include "pmm.hpp"

namespace tinyos_trial {

Pmm::Pmm(Page* pages, std::size_t count) noexcept : pages_(pages), count_(count) {
    if (pages == nullptr || count > INVALID_PAGE / PAGE_SIZE ||
        (reinterpret_cast<std::uintptr_t>(pages) & (PAGE_SIZE - 1)) != 0) {
        pages_ = nullptr;
        count_ = 0;
    }
}

// Store links as index + 1 in the free page itself; zero terminates the list.
// Byte access avoids type-punning the backing memory into another object type.
std::size_t Pmm::read_next(std::size_t index) const noexcept {
    std::size_t encoded = 0;
    for (std::size_t byte = 0; byte < sizeof(encoded); ++byte) {
        encoded |= static_cast<std::size_t>(pages_[index].bytes[byte]) << (byte * 8);
    }
    return encoded == 0 ? INVALID_PAGE : encoded - 1;
}

void Pmm::write_next(std::size_t index, std::size_t next) noexcept {
    const std::size_t encoded = next == INVALID_PAGE ? 0 : next + 1;
    for (std::size_t byte = 0; byte < sizeof(encoded); ++byte) {
        pages_[index].bytes[byte] = static_cast<std::uint8_t>(encoded >> (byte * 8));
    }
}

bool Pmm::is_recycled(std::size_t index) const noexcept {
    for (std::size_t current = free_head_; current != INVALID_PAGE;
         current = read_next(current)) {
        if (current == index) {
            return true;
        }
    }
    return false;
}

std::size_t Pmm::allocate() noexcept {
    std::size_t index;
    if (free_head_ != INVALID_PAGE) {
        index = free_head_;
        free_head_ = read_next(index);
        --recycled_count_;
    } else {
        if (next_unused_ == count_) {
            return INVALID_PAGE;
        }
        index = next_unused_++;
    }
    for (auto& byte : pages_[index].bytes) {
        byte = 0;
    }
    return index;
}

int Pmm::release(std::size_t index) noexcept {
    if (index >= next_unused_ || is_recycled(index)) {
        return -1;
    }
    write_next(index, free_head_);
    free_head_ = index;
    ++recycled_count_;
    return 0;
}

std::size_t Pmm::available_pages() const noexcept {
    return count_ - next_unused_ + recycled_count_;
}

Page* Pmm::page(std::size_t index) noexcept {
    if (index >= next_unused_ || is_recycled(index)) {
        return nullptr;
    }
    return &pages_[index];
}

} // namespace tinyos_trial
