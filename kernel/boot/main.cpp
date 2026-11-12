#include "private/boot_private.h"

/* The QEMU verifier scripts inject probe code that calls the UART helpers. */
#include "drivers/uart.h"

class Test {
    public:
        Test(char v) : value(v) { count += 1; }
        char value;

        volatile size_t count = 0;
};
Test test('D');

extern "C" void kernel_main(unsigned long hart_id, const void* device_tree) {

    uart_putchar(test.value);
    uart_put_hex(static_cast<unsigned long>(test.count));
    uart_putchar('\n');

    boot_console(hart_id, device_tree);
    boot_init_pmm(device_tree);

    Sv39PageTable* kernel_root = boot_init_kernel_vm(device_tree);
    uint64_t satp_value = boot_enable_paging(kernel_root);

    boot_start_user(kernel_root, satp_value);
    boot_start_scheduler();

    __builtin_unreachable();
}
