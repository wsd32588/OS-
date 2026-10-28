#ifndef TINYOS_TRIAL_SV39_HPP
#define TINYOS_TRIAL_SV39_HPP

#include <cstdint>

namespace tinyos_trial::sv39 {

inline constexpr unsigned int PAGE_SHIFT = 12;
inline constexpr unsigned int LEVEL_COUNT = 3;
inline constexpr unsigned int INVALID_INDEX = 512;
inline constexpr std::uint64_t FLAG_MASK = 0xff;
inline constexpr std::uint64_t PPN_MASK = ((UINT64_C(1) << 44) - 1) << 10;
inline constexpr std::uint64_t VALID = 1;
inline constexpr std::uint64_t READ = 2;
inline constexpr std::uint64_t WRITE = 4;
inline constexpr std::uint64_t EXECUTE = 8;

constexpr bool is_canonical(std::uint64_t address) noexcept {
    const auto upper = address >> 39;
    const auto sign = (address >> 38) & 1;
    return upper == (sign == 0 ? 0 : ((UINT64_C(1) << 25) - 1));
}

constexpr unsigned int vpn_index(std::uint64_t address, unsigned int level) noexcept {
    if (level >= LEVEL_COUNT) {
        return INVALID_INDEX;
    }
    return static_cast<unsigned int>((address >> (PAGE_SHIFT + level * 9)) & 511);
}

constexpr std::uint64_t make_pte(std::uint64_t physical_address,
                               std::uint64_t flags) noexcept {
    return (((physical_address >> PAGE_SHIFT) << 10) & PPN_MASK) | (flags & FLAG_MASK);
}

constexpr std::uint64_t physical_address(std::uint64_t pte) noexcept {
    return ((pte & PPN_MASK) >> 10) << PAGE_SHIFT;
}

constexpr std::uint64_t flags(std::uint64_t pte) noexcept {
    return pte & FLAG_MASK;
}

constexpr bool is_valid(std::uint64_t pte) noexcept {
    return (pte & VALID) != 0 && !((pte & READ) == 0 && (pte & WRITE) != 0);
}

constexpr bool is_leaf(std::uint64_t pte) noexcept {
    return is_valid(pte) && (pte & (READ | EXECUTE)) != 0;
}

} // namespace tinyos_trial::sv39

#endif
