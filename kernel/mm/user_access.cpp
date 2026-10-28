#include "mm/user_access.h"
#include "mm/user_access.hpp"

namespace tinyos {

int copy_readable_user (
    const Sv39PageTable& root,
    VirtAddr source,
    std::span<unsigned char> destination
) {
    if (destination.empty()) {
        return SV39_OK;
    }

    const size_t length = destination.size();

    VirtAddr last_address;
    if (!source.try_add(length - 1, last_address)) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
    constexpr uint64_t required_flags =
        SV39_PTE_V | SV39_PTE_R | SV39_PTE_U;
    size_t copied = 0;
    while (copied < length) {
        const VirtAddr current_address(source.val() + copied);
        uint64_t physical_address = 0;
        uint64_t flags = 0;

        const int result = root.query(
            current_address.val(), &physical_address, &flags);
        if (
            result != SV39_OK ||
            (flags & required_flags) != required_flags
        ) {
            return SV39_ERR_INVALID_ARGUMENT;
        }

        const size_t page_remaining =
            SV39_PAGE_SIZE - current_address.page_offset();

        size_t chunk = length - copied;
        if (chunk > page_remaining) {
            chunk = page_remaining;
        }

        const PhysAddr physical(physical_address);
        const unsigned char* src = physical.as_ptr<const unsigned char>();
        for (size_t i = 0; i < chunk; ++i) {
            destination[copied + i] = src[i];
        }

        copied += chunk;
    }

    return SV39_OK;
}
}

int user_copy_from(
    const Sv39PageTable* root,
    void* destination,
    uintptr_t source_address,
    size_t length
) {
    if (length == 0) {
        return SV39_OK;
    }

    if (root == nullptr || destination == nullptr) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    return tinyos::copy_readable_user(
        *root,
        VirtAddr(source_address),
        std::span<unsigned char>(
            static_cast<unsigned char*>(destination),
            length
        )
    );
}
