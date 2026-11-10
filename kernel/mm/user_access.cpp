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

int copy_writable_user(
    const Sv39PageTable &root,
    VirtAddr destination,
    std::span<const unsigned char> source) {
    if (source.empty()) {
        return SV39_OK;
    }

    const size_t length = source.size();

    VirtAddr address(destination.val());
    if (!destination.try_add(length - 1, address)) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    if (((destination.val() ^ address.val()) &
        (UINT64_C(1) << 38U)) != 0) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    constexpr uint64_t required_flags = 
        SV39_PTE_R | SV39_PTE_W | SV39_PTE_V | SV39_PTE_U;
    
    size_t writed = 0;
    while (writed < length) {
        const VirtAddr curr_addr(destination.val() + writed);

        uint64_t phy_addr_out = 0;
        uint64_t flags_out = 0;

        int result = root.query(curr_addr.val(), &phy_addr_out, &flags_out);
        
        if (result != SV39_OK ||
            (required_flags & flags_out) != required_flags    
        ) {
            return SV39_ERR_INVALID_ARGUMENT;
        }

        const size_t page_remaining = SV39_PAGE_SIZE - curr_addr.page_offset();
        size_t chunk = length - writed;
        if (chunk > page_remaining) {
            chunk = page_remaining;
        }

        const PhysAddr physical(phy_addr_out);
        unsigned char* target = physical.as_ptr<unsigned char>();
        
        for (size_t i = 0; i < chunk; ++i) {
            target[i] = source[writed + i];
        }

        writed += chunk;
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

int user_copy_to(
    const Sv39PageTable *root,
    uintptr_t destinatiom_address,
    const void *source,
    size_t length
) {
    if (length == 0) {
        return SV39_OK;
    }

    if (root == nullptr || source == nullptr
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    return tinyos::copy_writable_user(
        *root,
        VirtAddr(destinatiom_address),
        std::span<const unsigned char>(
            static_cast<const unsigned char*>(source),
            length
        )
    );
}
