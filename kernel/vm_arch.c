#include "vm_arch.h"

#include <stddef.h>

#define SATP_MODE_SV39 \
    (UINT64_C(8) << 60)

#define SATP_PPN_MASK \
    ((UINT64_C(1) << 44) - 1)

int sv39_activate(const Sv39PageTable *root) {
    if (root == NULL) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    uintptr_t root_address =
        (uintptr_t)root;

    if ((root_address & (SV39_PAGE_SIZE - 1U)) != 0 ||
        (root_address >> 56U) != 0) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    uint64_t root_ppn =
        (root_address >> SV39_PAGE_SHIFT) &
        SATP_PPN_MASK;
    uint64_t satp_value =
        SATP_MODE_SV39 | root_ppn;

    asm volatile (
        "sfence.vma\n"
        "csrw satp, %0\n"
        "sfence.vma\n"
        :
        : "r"(satp_value)
        : "memory"
    );

    return SV39_OK;
}

uint64_t sv39_read_satp(void) {
    uint64_t value;

    asm volatile(
        "csrr %0, satp"
        : "=r"(value)
    );

    return value;
}
