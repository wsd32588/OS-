#ifndef TINYOS_FS_FILE_SYSCALL_H
#define TINYOS_FS_FILE_SYSCALL_H

#include "mm/vm.h"

#ifdef __cplusplus
extern "C" {
#endif

long fs_syscall_read_file(
    const Sv39PageTable* root,
    uintptr_t path_address,
    uint64_t path_length,
    uint64_t offset,
    uintptr_t destination_address,
    uint64_t length
);

#ifdef __cplusplus
}
#endif

#endif