#include "pmm.hpp"
#include "sv39.hpp"

// Emit code for the header-only routines during the freestanding compile check.
extern "C" std::uint64_t trial_pte(std::uint64_t address, std::uint64_t flags) noexcept {
    if (!tinyos_trial::sv39::is_canonical(address)) {
        return 0;
    }
    return tinyos_trial::sv39::make_pte(address, flags);
}
