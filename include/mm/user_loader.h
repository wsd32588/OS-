#ifndef TINYOS_USER_LOADER_H
#define TINYOS_USER_LOADER_H

#include <stdint.h>
#include <stddef.h>

#include "mm/user_memory.h"
#include "user/user_elf.h"

typedef struct UserMemoryLayout {
    uintptr_t code_address;
    uintptr_t stack_address;
    uintptr_t data_address;
}UserMemoryLayout;

#ifdef __cplusplus
extern "C"{
#endif
int user_memory_load_elf(
    Sv39PageTable* root,
    const void* image,
    size_t image_size,
    const UserElfLayout* elf_layout,
    const UserMemoryLayout* memory_layout,
    UserMemory* memory_out
);
#ifdef __cplusplus
}
#endif

#endif
