#pragma once
#include <span>

inline bool paths_equal(
    std::span<const char> left,
    std::span<const char> right
) noexcept {
    if (left.size() != right.size()) {
        return false;
    }

    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }

    return true;
}