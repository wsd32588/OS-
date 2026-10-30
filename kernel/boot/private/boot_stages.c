#include "boot_private.h"

#include "arch/riscv/trap.h"
#include "arch/riscv/vm_arch.h"
#include "drivers/uart.h"
#include "mm/pmm.h"
#include "mm/vm.h"
#include "task/sched.h"
#include "task/timer.h"
#include "user/user_elf.h"
#include "mm/user_loader.h"

extern char __kernel_start[];
extern char __kernel_end[];
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern const unsigned char user_elf_start[];
extern const unsigned char user_elf_end[];
extern const unsigned char user_image_start[];

static void boot_fail(const char *message) {
    uart_puts(message);

    for (;;) {
        asm volatile("wfi");
    }
}

void boot_console(unsigned long hart_id, const void* device_tree) {
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
}

void boot_load_user_elf(UserElfLayout* layout, size_t* elf_size) {
    uintptr_t elf_start = (uintptr_t)user_elf_start;
    uintptr_t elf_end = (uintptr_t)user_elf_end;

    if (elf_end <= elf_start) {
        boot_fail("Invalid embedded ELF size\n");
    }

    *elf_size = (size_t)(elf_end - elf_start);
    if (user_elf_parse(user_elf_start, *elf_size, layout)
        != USER_ELF_OK
    ) {
        boot_fail("User ELF parsing failed.\n");
    }

    uart_puts("User ELF entry: ");
    uart_put_hex((unsigned long)layout->entry_address);
    uart_putchar('\n');

    uart_puts("User ELF program header count: ");
    uart_put_hex((unsigned long)layout->program_header_count);
    uart_putchar('\n');

    uart_puts("User ELF LOAD count: ");
    uart_put_hex((unsigned long)layout->load_segment_count);
    uart_putchar('\n');

    for (size_t i = 0; i < layout->load_segment_count; ++i) {
        const UserElfSegment *segment = &layout->segments[i];

        uart_puts("User ELF LOAD ");
        uart_put_hex((unsigned long)i);
        uart_puts(": offset=");
        uart_put_hex((unsigned long)segment->file_offset);
        uart_puts(" va=");
        uart_put_hex((unsigned long)segment->virtual_address);
        uart_puts(" filesz=");
        uart_put_hex((unsigned long)segment->file_size);
        uart_puts(" memsz=");
        uart_put_hex((unsigned long)segment->memory_size);
        uart_puts(" flags=");
        uart_put_hex((unsigned long)segment->flags);
        uart_puts(" align=");
        uart_put_hex((unsigned long)segment->alignment);
        uart_putchar('\n');
    }
}

void boot_init_pmm(const void* device_tree) {
    if (pmm_init(__kernel_end, device_tree) < 0) {
        boot_fail("PMM initialization failed.\n");
    }

    uart_puts("PMM initialized. Available pages: ");
    uart_put_hex(pmm_available_pages());
    uart_putchar('\n');
}

Sv39PageTable* boot_init_kernel_vm(const void* device_tree) {
    Sv39PageTable* kernel_root = sv39_create_page_table();
    if (kernel_root == NULL) {
        boot_fail("Kernel root page table allocation failed. \n");
    }

    const uint64_t text_flags =
        SV39_PTE_R |
        SV39_PTE_X |
        SV39_PTE_A;

    const uint64_t rodata_flags =
        SV39_PTE_R |
        SV39_PTE_A;

    const uint64_t writable_flags =
        SV39_PTE_R |
        SV39_PTE_W |
        SV39_PTE_A |
        SV39_PTE_D;

    int mapping_result = map_identity_range(
        kernel_root,
        (uintptr_t)__text_start,
        (uintptr_t) __text_end,
        text_flags
    );

    if (mapping_result != SV39_OK) {
        boot_fail("Kernel text mapping failed");
    }

    mapping_result = map_identity_range(
        kernel_root,
        (uintptr_t) __rodata_start,
        (uintptr_t) __rodata_end,
        rodata_flags
    );

    if (mapping_result != SV39_OK) {
        boot_fail("Kernel rodata mapping failed.\n");
    }

    mapping_result = map_identity_range(
        kernel_root,
        (uintptr_t)__data_start,
        (uintptr_t) __data_end,
        writable_flags
    );

    if (mapping_result != SV39_OK) {
        boot_fail("Kernel data mapping failed");
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
        boot_fail("Physical memory identity mapping failed.\n");
    }

    const uintptr_t uart_page =
        UINT64_C(0x10000000);

    mapping_result = map_identity_range(
        kernel_root,
        uart_page,
        uart_page + SV39_PAGE_SIZE,
        writable_flags);

    if (mapping_result != SV39_OK) {
        boot_fail("UART identity mapping failed.\n");
    }

    if (!identity_mapping_is_present(
        kernel_root,
        (uintptr_t)__text_start,
        text_flags
    ) ||
    !identity_mapping_is_present(
        kernel_root,
        (uintptr_t)__rodata_start,
        rodata_flags
    ) ||
    !identity_mapping_is_present(
        kernel_root,
        (uint64_t)__data_start,
        writable_flags
    )||
    !identity_mapping_is_present(
        kernel_root,
        (uint64_t)kernel_root,
        writable_flags
    )||
    !identity_mapping_is_present(
        kernel_root,
        uart_page,
        writable_flags
    )) {
        boot_fail("Kernel page table verification failed.\n");
    }

    uart_puts("Kernel page table root: ");
    uart_put_hex((unsigned long)kernel_root);
    uart_putchar('\n');

    uart_puts(
        "Kernel identity mappings ready; paging is still disabled.\n"
    );

    return kernel_root;
}

uint64_t boot_enable_paging(Sv39PageTable* kernel_root) {
    trap_init();
    uart_puts("Trap handler initialized.\n");

    uart_puts("Activating Sv39 paging...\n");

    if (sv39_activate(kernel_root) != SV39_OK) {
        boot_fail("SV39 activation failed. \n");
    }

    uint64_t satp_value =
        sv39_read_satp();

    uart_puts("Paging enabled. satp: ");
    uart_put_hex((unsigned long)satp_value);
    uart_putchar('\n');

    return satp_value;
}

void boot_start_user(
    Sv39PageTable* kernel_root,
    uint64_t satp_value,
    size_t elf_size,
    const UserElfLayout* layout
) {
    /* kernel_root and satp_value are also read by the instrumented QEMU probe. */
    (void)satp_value;

    const UserMemoryLayout memory_layout = {
        .code_address = DEMO_USER_CODE_ADDRESS,
        .stack_address = DEMO_USER_STACK_ADDRESS,
        .data_address = DEMO_USER_DATA_ADDRESS
    };

    UserMemory user_memory = {0};

    if (user_memory_load_elf(
        kernel_root,
        user_elf_start,
        elf_size,
        layout,
        &memory_layout,
        &user_memory) != SV39_OK) {
            boot_fail("Demo user memory setup failed.\n");
        }
    sched_init();
    int task_a_id = task_create(task_a);
    int task_b_id = task_create(task_b);
    int user_task_id = task_create_user(
        layout->entry_address,
        memory_layout.stack_address + SV39_PAGE_SIZE,
        &user_memory
    );

    if (task_a_id < 0 ||
        task_b_id < 0 ||
        user_task_id < 0) {
        uart_puts("Task creation failed.\n");

        int clean_result = user_memory_release(&user_memory);
        if (clean_result != SV39_OK) {
            uart_puts("[FAIL] clean up memory failed");
        }
        for (;;) {
            asm volatile("wfi");
        }
    }
}

void boot_start_scheduler(void) {
    timer_init();
    uart_puts("Starting preemptive scheduler...\n");
    if (scheduler_start() < 0) {
        boot_fail("Scheduler failed to start.\n");
    }
}
