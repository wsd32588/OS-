#ifndef TINYOS_PAGE_TABLE_ENTRY_HPP
#define TINYOS_PAGE_TABLE_ENTRY_HPP

#include "mm/address.hpp"
#include "mm/vm.h"

class PageTableEntry {
public:
    constexpr PageTableEntry() noexcept = default;

    // Preserve invalid and reserved bits so queries can diagnose existing entries.
    constexpr explicit PageTableEntry(Sv39Pte value) noexcept : value_(value) {}

    [[nodiscard]] static constexpr PageTableEntry from_page(
        PhysPageNum page, uint64_t flags
    ) noexcept {
        return PageTableEntry(
            ((page.val() & PhysPageNum::VALUE_MASK) << SV39_PTE_PPN_SHIFT) |
            (flags & SV39_PTE_FLAG_MASK));
    }

    // Keep the original C codec's truncation semantics; map validates its inputs.
    [[nodiscard]] static constexpr PageTableEntry from_address(
        PhysAddr address, uint64_t flags
    ) noexcept {
        return from_page(PhysPageNum::from_floor(address), flags);
    }

    [[nodiscard]] constexpr Sv39Pte val() const noexcept { return value_; }

    [[nodiscard]] constexpr PhysPageNum ppn() const noexcept {
        return PhysPageNum((value_ >> SV39_PTE_PPN_SHIFT) & PhysPageNum::VALUE_MASK);
    }

    [[nodiscard]] constexpr PhysAddr physical_address() const noexcept {
        return ppn().to_addr();
    }

    [[nodiscard]] constexpr uint64_t flags() const noexcept {
        return value_ & SV39_PTE_FLAG_MASK;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return (value_ & SV39_PTE_V) != 0 &&
            !((value_ & SV39_PTE_W) != 0 && (value_ & SV39_PTE_R) == 0);
    }

    [[nodiscard]] constexpr bool is_leaf() const noexcept {
        return is_valid() && (value_ & (SV39_PTE_R | SV39_PTE_X)) != 0;
    }

private:
    Sv39Pte value_ = 0;
};

static_assert(sizeof(PageTableEntry) == sizeof(Sv39Pte));

#endif
