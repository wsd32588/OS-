#include "private/boot_private.h"

/* The QEMU verifier scripts inject probe code that calls the UART helpers. */
#include "drivers/uart.h"

void kernel_main(unsigned long hart_id, const void* device_tree) {
    UserElfLayout layout = {0};
    size_t elf_size = 0;

    boot_console(hart_id, device_tree);
    boot_load_user_elf(&layout, &elf_size);
    boot_init_pmm(device_tree);

    Sv39PageTable* kernel_root = boot_init_kernel_vm(device_tree);
    uint64_t satp_value = boot_enable_paging(kernel_root);

    boot_start_user(kernel_root, satp_value, elf_size, &layout);
    boot_start_scheduler();

    __builtin_unreachable();
}
