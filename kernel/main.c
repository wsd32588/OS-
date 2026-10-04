#include "uart.h"
#include "trap.h"
#include "timer.h"
#include "sched.h"
#include "pmm.h"
#include "vm.h"
#include "vm_arch.h"

#define DEMO_USER_CODE_ADDRESS \
    UINT64_C(0x40000000)
#define DEMO_USER_STACK_ADDRESS \
    UINT64_C(0x40001000)
#define DEMO_USER_STACK_TOP \
    UINT64_C(0x40002000)

extern const unsigned char user_image_start[];
extern const unsigned char user_image_end[];
extern char __kernel_start[];
extern char __kernel_end[];
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];

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

static int prepare_demo_user_memory(
    Sv39PageTable* root,
    UserMemory* memory_out
) {

    if(root == NULL ||
        memory_out == NULL
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    uintptr_t image_start = (uintptr_t)user_image_start;
    uintptr_t image_end = (uintptr_t)user_image_end;

    if (image_end <= image_start ||
        image_end - image_start >
        SV39_PAGE_SIZE) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    void* code_page = NULL;
    void* stack_page = NULL;
    int code_mapping_attempted = 0;
    int stack_mapping_attempted = 0;
    int code_mapped = 0;
    int stack_mapped = 0;
    int result = SV39_OK;



    code_page = pmm_alloc_page();
    if (code_page == NULL) {
        result = SV39_ERR_NO_MEMORY;
        goto clean_up;
    }

    stack_page = pmm_alloc_page();
    if (stack_page == NULL) {
        result = SV39_ERR_NO_MEMORY;
        goto clean_up;
    }

    unsigned char* destination = (unsigned char*)code_page;

    /*
     * 将用户程序复制到新分配的物理页中
     */
    for (uintptr_t i = 0; i < image_end - image_start; ++i) {
        destination[i] = user_image_start[i];
    }

    /*
     * 刚刚把数据写成了即将执行的指令。
     * fence.i 让后续取指看到这些新内容。
     */
    asm volatile(
        "fence.i"
        :
        :
        : "memory"
    );

    const uint64_t user_code_flags =
        SV39_PTE_R |
        SV39_PTE_X |
        SV39_PTE_U |
        SV39_PTE_A;
    const uint64_t user_stack_flags =
        SV39_PTE_R |
        SV39_PTE_W |
        SV39_PTE_U |
        SV39_PTE_A |
        SV39_PTE_D;

    code_mapping_attempted = 1;
    result = sv39_map_page(
        root,
        DEMO_USER_CODE_ADDRESS,
        (uintptr_t)code_page,
        user_code_flags
    );
    if (result != SV39_OK) {
        goto clean_up;
    }
    code_mapped = 1;

    stack_mapping_attempted = 1;
    result = sv39_map_page(
        root,
        DEMO_USER_STACK_ADDRESS,
        (uint64_t)(uintptr_t)stack_page,
        user_stack_flags
    );
    if (result != SV39_OK) {
        goto clean_up;
    }
    stack_mapped = 1;

    result = sv39_flush_page(DEMO_USER_CODE_ADDRESS);
    if (result != SV39_OK) {
        goto clean_up;
    }

    result = sv39_flush_page(DEMO_USER_STACK_ADDRESS);
    if (result != SV39_OK) {
        goto clean_up;
    }

    *memory_out = (UserMemory){
        .root = root,
        .code_page = code_page,
        .stack_page = stack_page,
        .code_address = DEMO_USER_CODE_ADDRESS,
        .stack_address = DEMO_USER_STACK_ADDRESS
    };

    return SV39_OK;
    clean_up:
        if (stack_mapped == 1) {
            int err = sv39_unmap_page(root, DEMO_USER_STACK_ADDRESS);
            if (err != SV39_OK && err != SV39_ERR_NOT_MAPPED) {
                return err;
            }
        }

        if (code_mapped == 1) {
            int err = sv39_unmap_page(root, DEMO_USER_CODE_ADDRESS);
            if (err != SV39_OK && err != SV39_ERR_NOT_MAPPED) {
                return err;
            }
        }

        if (stack_mapping_attempted == 1) {
            int err = sv39_flush_page(DEMO_USER_STACK_ADDRESS);
            if (err != SV39_OK) {
                return err;
            }
        }

        if (code_mapping_attempted == 1) {
            int err = sv39_flush_page(DEMO_USER_CODE_ADDRESS);
            if (err != SV39_OK) {
                return err;
            }
        }

        if (code_page != NULL) {
            int err = pmm_free_page(code_page);
            if (err != 0) {
                return SV39_ERR_PAGE_FREE_FAILED;
            }
        }

        if (stack_page != NULL) {
            int err = pmm_free_page(stack_page);
            if (err != 0) {
                return SV39_ERR_PAGE_FREE_FAILED;
            }
        }

        if (stack_mapping_attempted) {
            int err = sv39_reclaim_empty_tables(root, DEMO_USER_STACK_ADDRESS);
            if (err != SV39_OK &&
                err != SV39_ERR_NOT_MAPPED) {
                return err;
            }
        }

        if (code_mapping_attempted) {
            int err = sv39_reclaim_empty_tables(root, DEMO_USER_CODE_ADDRESS);
            if (err != SV39_OK &&
                err != SV39_ERR_NOT_MAPPED) {
                return err;
            }
        }

    return result;
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
        uart_puts("Kernel text mapping failed");

        for(;;) {
            asm volatile("wfi");
        }
    }

    mapping_result = map_identity_range(
        kernel_root,
        (uintptr_t) __rodata_start,
        (uintptr_t) __rodata_end,
        rodata_flags
    );

    if (mapping_result != SV39_OK) {
        uart_puts("Kernel rodata mapping failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

        mapping_result = map_identity_range(
        kernel_root,
        (uintptr_t)__data_start,
        (uintptr_t) __data_end,
        writable_flags
    );

    if (mapping_result != SV39_OK) {
        uart_puts("Kernel data mapping failed");

        for(;;) {
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
    UserMemory user_memory = {0};
    uart_puts("Paging enabled. satp: ");
    uart_put_hex((unsigned long)satp_value);
    uart_putchar('\n');

    if (prepare_demo_user_memory(
        kernel_root,
        &user_memory) != SV39_OK
    ) {
        uart_puts("Demo user memory setup failed.\n");

        for (;;) {
            asm volatile("wfi");
        }
    }

    sched_init();
    int task_a_id = task_create(task_a);
    int task_b_id = task_create(task_b);
    int user_task_id = task_create_user(
        DEMO_USER_CODE_ADDRESS,
        DEMO_USER_STACK_TOP,
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
