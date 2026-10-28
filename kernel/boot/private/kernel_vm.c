#include "boot_private.h"

#include "mm/vm.h"

int map_identity_range(
    Sv39PageTable *root,
    uintptr_t start,
    uintptr_t end,
    uint64_t flags
) {
    if (root == NULL ||
        start >= end ||
        (start & (SV39_PAGE_SIZE - 1U)) != 0 ||
        (end & (SV39_PAGE_SIZE - 1U)) != 0) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    for (uintptr_t address = start;
         address < end;
         address += SV39_PAGE_SIZE) {
        int result = sv39_map_page(
            root,
            address,
            address,
            flags
        );

        if (result != SV39_OK) {
            return result;
        }
    }

    return SV39_OK;
}

int identity_mapping_is_present(
    const Sv39PageTable* root,
    uint64_t address,
    uint64_t expected_flags
) {
    uint64_t physical_address = 0;
    uint64_t actual_flags = 0;

    int result = sv39_query_page(
        root,
        address,
        &physical_address,
        &actual_flags
    );

    return result == SV39_OK &&
        physical_address == address &&
        actual_flags ==
            (expected_flags | SV39_PTE_V);
}
