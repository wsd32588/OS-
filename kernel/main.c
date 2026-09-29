#include "uart.h"
#include "trap.h"
#include "timer.h"
#include "sched.h"
#include "pmm.h"
#include "vm.h"
#include "vm_arch.h"

extern char __kernel_start[];
extern char __kernel_end[];

static void delay(void) {
    for (volatile unsigned long i = 0;
        i < 5000000UL;
    ++i) {
        asm volatile("" ::: "memory");
    }
}

static void task_a(void){
    for (;;){
        uart_putchar('A');
        delay();
    }
}

static void task_b(void){
    for (int i = 0; i < 10; ++i){
        uart_putchar('B');
        delay();
    }
}

static int map_identity_range(
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

static int identity_mapping_is_present(
    const Sv39PageTable* root,
    uint64_t address
) {
    uint64_t physical_address = 0;
    uint64_t flags = 0;

    int result = sv39_query_page(
        root,
        address,
        &physical_address,
        &flags
    );

    return result == SV39_OK &&
        physical_address == address;
}

void kernel_main(unsigned long hart_id, const void* device_tree) {
    uart_puts("\nHello TinyOS!\n");

    uart_puts("Boot hart: ");
    uart_put_hex(hart_id);
    uart_putchar('\n');

    uart_puts("Device tree: ");
    uart_put_hex((unsigned long)device_tree);
    uart_putchar('\n');

    uart_puts("Kernel range: ");
    uart_put_hex((unsigned long)__kernel_start);
    uart_puts(" - ");
    uart_put_hex((unsigned long)__kernel_end);
    uart_putchar('\n');

    if (pmm_init(__kernel_end, device_tree) < 0) {
        uart_puts("PMM initialization failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    uart_puts("PMM initialized. Available pages: ");
    uart_put_hex(pmm_available_pages());
    uart_putchar('\n');

    Sv39PageTable* kernel_root = sv39_create_page_table();
    if (kernel_root == NULL) {
        uart_puts("Kernel root page table allocation failed. \n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    const uint64_t kernel_flags =
        SV39_PTE_R |
        SV39_PTE_W |
        SV39_PTE_X |
        SV39_PTE_A |
        SV39_PTE_D;

    const uint64_t writable_flags =
        SV39_PTE_R |
        SV39_PTE_W |
        SV39_PTE_A |
        SV39_PTE_D;

    int mapping_result = map_identity_range(kernel_root,
        (uintptr_t)__kernel_start,
        (uintptr_t)__kernel_end,
        kernel_flags);

    if (mapping_result != SV39_OK) {
        uart_puts("Kernel identity mapping failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    uintptr_t physical_memory_end =
        (uintptr_t)device_tree &
        ~(SV39_PAGE_SIZE - 1U);

    mapping_result = map_identity_range(
        kernel_root,
        (uintptr_t)__kernel_end,
        physical_memory_end,
        writable_flags
    );

    if (mapping_result != SV39_OK) {
        uart_puts("Physical memory identity mapping failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    const uintptr_t uart_page =
        UINT64_C(0x10000000);

    mapping_result = map_identity_range(
        kernel_root,
        uart_page,
        uart_page + SV39_PAGE_SIZE,
        writable_flags);

    if (mapping_result != SV39_OK) {
        uart_puts("UART identity mapping failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    if (!identity_mapping_is_present(
        kernel_root,
        (uintptr_t)__kernel_start
    ) ||
    !identity_mapping_is_present(
        kernel_root,
        (uintptr_t)kernel_root
    ) ||
    !identity_mapping_is_present(
        kernel_root,
        uart_page
    )) {
        uart_puts("Kernel page table verification failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    uart_puts("Kernel page table root: ");
    uart_put_hex((unsigned long)kernel_root);
    uart_putchar('\n');

    uart_puts(
        "Kernel identity mappings ready; paging is still disabled.\n"
    );
    trap_init();
    uart_puts("Trap handler initialized.\n");

    uart_puts("Activating Sv39 paging...\n");

    if (sv39_activate(kernel_root) != SV39_OK) {
        uart_puts("SV39 activation failed. \n");

        for (;;){
            asm volatile("wfi");
        }
    }

    uint64_t satp_value =
        sv39_read_satp();

    uart_puts("Paging enabled. satp: ");
    uart_put_hex((unsigned long)satp_value);
    uart_putchar('\n');

    sched_init();
    int task_a_id = task_create(task_a);
    int task_b_id = task_create(task_b);

    if (task_a_id < 0 ||
        task_b_id < 0) {
        uart_puts("Task creation failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    timer_init();
    uart_puts("Starting preemptive scheduler...\n");
    if (scheduler_start() < 0) {
        uart_puts("Scheduler failed to start.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    __builtin_unreachable();
}
