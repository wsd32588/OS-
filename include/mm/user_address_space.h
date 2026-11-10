#ifndef TINYOS_USER_ADDRESS_SPACE_H
#define TINYOS_USER_ADDRESS_SPACE_H

#include "mm/user_memory.h"
#include "mm/vm.h"

#define USER_ADDRESS_SPACE_ROOT_INDEX 1U

typedef struct UserAddressSpace {
    Sv39PageTable* root;                 // Owned private root page.
    const Sv39PageTable* kernel_root;     // Borrowed permanent kernel template.
    UserMemory user_memory;
} UserAddressSpace;

#ifdef __cplusplus
extern "C" {
#endif

int user_address_space_create(
    const Sv39PageTable* kernel_root,
    UserAddressSpace* output
);

int user_address_space_clone(
    const UserAddressSpace* source,
    UserAddressSpace* output
);

int user_address_space_release(UserAddressSpace* space);

#ifdef __cplusplus
}
#endif
#endif
