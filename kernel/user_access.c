#include "user_access.h"

int user_copy_from (
    const Sv39PageTable* root,
    void* destination,
    uintptr_t source_address,
    size_t length
) {
    if (length == 0) {
        return SV39_OK;
    }

    if (root == NULL ||
        destination == NULL ||
        source_address > UINTPTR_MAX - (length - 1)
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    uintptr_t last_address = source_address + (length - 1);
    if (!sv39_virtual_address_is_canonical(source_address) ||
        !sv39_virtual_address_is_canonical(last_address)
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    unsigned char* output = destination;
    size_t copied = 0;
    while (copied < length) {
        uintptr_t current_address = source_address + copied;
        uint64_t physical_address = 0;
        uint64_t flags = 0;

        /*
         *查询 current_address: 失败或者缺少 U/R 就返回错误
         */
        const uint64_t required_flags =
            SV39_PTE_V | SV39_PTE_R | SV39_PTE_U;
        int result = sv39_query_page(root, current_address, &physical_address, &flags);
        if (
            result != SV39_OK ||
            (flags & required_flags) != required_flags
        ) {
            return SV39_ERR_INVALID_ARGUMENT;
        }

        size_t page_remaining = SV39_PAGE_SIZE -
            (current_address & (SV39_PAGE_SIZE - 1U));

        size_t chunk = length - copied;
        if (chunk > page_remaining) {
            chunk = page_remaining;
        }

        const unsigned char* src = (const unsigned char*)physical_address;
        for (size_t i = 0; i < chunk; ++i) {
            output[copied + i] = src[i];
        }

        copied += chunk;
    }

    return SV39_OK;
}
