#ifndef TINYOS_BOOT_PRIVATE_H
#define TINYOS_BOOT_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

#include "mm/vm.h"
#include "user/user_elf.h"

/* Demo user address layout; private to the boot stages. */
#define DEMO_USER_CODE_ADDRESS \
    UINT64_C(0x40000000)
#define DEMO_USER_STACK_ADDRESS \
    UINT64_C(0x40001000)
#define DEMO_USER_STACK_TOP \
    UINT64_C(0x40002000)
#define DEMO_USER_DATA_ADDRESS \
    UINT64_C(0x40003000)
#define DEMO_USER_IMAGE_OFFSET \
    (DEMO_USER_DATA_ADDRESS - DEMO_USER_CODE_ADDRESS)

/* Boot-private stages shared between main.c and the private stage files. */
#ifdef __cplusplus
extern "C" {
#endif

int map_identity_range(
    Sv39PageTable *root,
    uintptr_t start,
    uintptr_t end,
    uint64_t flags);

int identity_mapping_is_present(
    const Sv39PageTable *root,
    uint64_t address,
    uint64_t expected_flags);


void task_a(void);
void task_b(void);

/* Boot stages, in the order kernel_main runs them. */
void boot_console(unsigned long hart_id, const void *device_tree);
void boot_load_user_elf(UserElfLayout *layout, size_t *elf_size);
void boot_init_pmm(const void *device_tree);
Sv39PageTable *boot_init_kernel_vm(const void *device_tree);
uint64_t boot_enable_paging(Sv39PageTable *kernel_root);
void boot_start_user(
    Sv39PageTable *kernel_root,
    uint64_t satp_value,
    size_t elf_size,
    const UserElfLayout *layout);
void boot_start_scheduler(void);

#ifdef __cplusplus
}
#endif

#endif
