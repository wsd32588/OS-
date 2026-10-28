#ifndef TINYOS_TRIAL_PMM_HPP
#define TINYOS_TRIAL_PMM_HPP

#include <cstddef>
#include <cstdint>

namespace tinyos_trial {

inline constexpr std::size_t PAGE_SIZE = 4096;
inline constexpr std::size_t INVALID_PAGE = static_cast<std::size_t>(-1);

struct alignas(PAGE_SIZE) Page {
    std::uint8_t bytes[PAGE_SIZE];
};

static_assert(sizeof(Page) == PAGE_SIZE);
static_assert(alignof(Page) == PAGE_SIZE);

// The caller owns the pool and must keep it alive for the allocator's lifetime.
class Pmm {
public:
    Pmm(Page* pages, std::size_t count) noexcept;
    Pmm(const Pmm&) = delete;
    Pmm& operator=(const Pmm&) = delete;

    std::size_t allocate() noexcept;
    int release(std::size_t index) noexcept;
    std::size_t available_pages() const noexcept;
    Page* page(std::size_t index) noexcept;

private:
    bool is_recycled(std::size_t index) const noexcept;
    std::size_t read_next(std::size_t index) const noexcept;
    void write_next(std::size_t index, std::size_t next) noexcept;

    Page* pages_;
    std::size_t count_;
    std::size_t next_unused_ = 0;
    std::size_t free_head_ = INVALID_PAGE;
    std::size_t recycled_count_ = 0;
};

} // namespace tinyos_trial

#endif
