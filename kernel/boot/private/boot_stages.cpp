#include "boot_private.h"

#include "arch/riscv/trap.h"
#include "arch/riscv/vm_arch.h"
#include "drivers/uart.h"
#include "fs/builtin_fs.hpp"
#include "fs/file_service.hpp"
#include "mm/pmm.h"
#include "mm/vm.h"
#include "task/sched.h"
#include "task/timer.h"
#include "mm/user_loader.h"
#include "process/builtin_program.hpp"
#if TINYOS_BLOCK_PROBE
#include "drivers/virtio_block.hpp"
#endif


extern const unsigned char user_elf_start[];
extern const unsigned char user_elf_end[];
extern const unsigned char user_worker_elf_start[];
extern const unsigned char user_worker_elf_end[];

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

static constexpr char kDemoPath[] = "/bin/demo";
static constexpr char kWorkerPath[] = "/bin/worker";
static constexpr char kAboutPath[] = "/etc/about";
static constexpr unsigned char kAboutBytes[] = "TinyOS builtin fs\n";

namespace {
constinit tinyos::fs::BuiltinFileSystem g_builtin_fs{};
#if TINYOS_BLOCK_PROBE
constexpr uintptr_t kVirtioBlockMmioBase = UINT64_C(0x10001000);
#endif
}


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


void boot_init_pmm(const void* device_tree) {
    /*
     * PMM 管理 [__kernel_end, device_tree)：
     *   - [__kernel_start, __kernel_end) 已被内核镜像/启动栈/BSS 占用；
     *   - DTB 及其所在页保留给固件/设备树，不能被分配后覆盖；
     *   - pmm_init 会把 start 向上对齐、end 向下对齐，只管理完整页。
     */
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

    /*
     * 内核根页表建立恒等映射：内核虚拟地址 == 物理地址。
     * 分页开启后，C/C++ 指针值仍然可以直接作为物理地址使用。
     * VirtIO 设备 DMA 需要 guest physical address，因此
     * g_descriptors/g_available/g_used 的指针数值可以直接写给设备。
     * 如果以后内核迁到高半区，驱动必须显式做 virt_to_phys()。
     */
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

    /*
     * 把 PMM 可分配的普通物理内存也恒等映射。
     * 这样 pmm_alloc_page() 返回的页在开启分页后仍可直接解引用；
     * 页表页、用户页和 VirtIO 静态 DMA 区都依赖这个约定。
     */
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

    // 分页后，CPU 通过恒等映射读写 UART 设备寄存器。
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
        writable_flags)) {
        boot_fail("Kernel page table verification failed.\n");
    }

#if TINYOS_BLOCK_PROBE
    // 探针在分页后访问 VirtIO 寄存器，QEMU 则通过客体物理地址访问队列内存。
    mapping_result = map_identity_range(
        kernel_root,
        kVirtioBlockMmioBase,
        kVirtioBlockMmioBase + SV39_PAGE_SIZE,
        writable_flags);
    if (mapping_result != SV39_OK ||
        !identity_mapping_is_present(kernel_root, kVirtioBlockMmioBase, writable_flags)) {
        boot_fail("VirtIO MMIO identity mapping failed.\n");
    }
#endif

    uart_puts("Kernel page table root: ");
    uart_put_hex((unsigned long)kernel_root);
    uart_putchar('\n');

    uart_puts(
        "Kernel identity mappings ready; paging is still disabled.\n"
    );

    return kernel_root;
}

#if TINYOS_BLOCK_PROBE
static bool boot_probe_verify_sector(uint64_t sector, std::span<unsigned char> buffer) {
    if (tinyos::drivers::virtio_block_read(sector, buffer) != 0) {
        return false;
    }

    // 启动探针按测试镜像的生成公式算出每个位置的期望字节。
    for (size_t offset = 0; offset < buffer.size(); ++offset) {
        const auto expected = static_cast<unsigned char>((17 + sector * 53 + offset) & 0xff);
        if (buffer[offset] != expected) {
            return false;
        }
    }

    return true;
}

void boot_probe_block_read(void) {
    uint64_t sector_count = 0;
    if (tinyos::drivers::virtio_block_init(kVirtioBlockMmioBase, sector_count) != 0) {
        uart_puts("[block] init failed\n");
        return;
    }
    if (sector_count != 128) {
        uart_puts("[block] unexpected capacity\n");
        return;
    }

    // 探针持有一个扇区的目标数组。驱动在设备完成读取后才把数据复制到这里。
    unsigned char buffer[512]{};
    if (!boot_probe_verify_sector(0, buffer)) {
        uart_puts("[block] sector 0 failed\n");
        return;
    }
    uart_puts("[block] sector 0 ok\n");

    // sector_count 是第一个无效扇区号。探针用哨兵字节检查拒绝时整个目标仍有效。
    constexpr unsigned char kSentinel = 0xa5;
    for (unsigned char& byte : buffer) {
        byte = kSentinel;
    }
    if (tinyos::drivers::virtio_block_read(sector_count, buffer) != -1) {
        uart_puts("[block] out-of-range read was accepted\n");
        return;
    }
    for (const unsigned char byte : buffer) {
        if (byte != kSentinel) {
            uart_puts("[block] out-of-range read changed the buffer\n");
            return;
        }
    }
    uart_puts("[block] out-of-range rejected\n");

    if (!boot_probe_verify_sector(1, buffer)) {
        uart_puts("[block] sector 1 failed\n");
        return;
    }
    uart_puts("[block] sector 1 ok\n");
}
#endif

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
    uint64_t satp_value
) {
    const uintptr_t demo_start =
        reinterpret_cast<uintptr_t>(user_elf_start);
    const uintptr_t demo_end =
        reinterpret_cast<uintptr_t>(user_elf_end);
    const uintptr_t worker_start =
        reinterpret_cast<uintptr_t>(user_worker_elf_start);
    const uintptr_t worker_end =
        reinterpret_cast<uintptr_t>(user_worker_elf_end);
    if (demo_end <= demo_start || worker_end <= worker_start) {
        boot_fail("Invalid embedded ELF range.\n");
    }

    const tinyos::fs::BuiltinFile files[] = {
        {
            {kDemoPath, sizeof(kDemoPath) - 1},
            {user_elf_start, static_cast<size_t>(demo_end - demo_start)},
        },
        {
            {kWorkerPath, sizeof(kWorkerPath) - 1},
            {user_worker_elf_start, static_cast<size_t>(worker_end - worker_start)
            },
        },
        {
            {kAboutPath, sizeof(kAboutPath) - 1},
            {kAboutBytes, sizeof(kAboutBytes) - 1}
        }
    };

    if (g_builtin_fs.init(files) != 0) {
        boot_fail("Builtin filesystem initialization failed.\n");
    }

    if (tinyos::fs::configure_file_service(g_builtin_fs) != 0) {
        boot_fail("File service configuration failed.\n");
    }

    if (boot_configure_console() != 0) {
        boot_fail("Console configure failed\n");
    }

    const auto* demo_file = tinyos::fs::lookup_file(
        {kDemoPath, sizeof(kDemoPath) - 1});
    const auto* worker_file = tinyos::fs::lookup_file(
        {kWorkerPath, sizeof(kWorkerPath) - 1});
    if (demo_file == nullptr || worker_file == nullptr) {
        boot_fail("Builtin program lookup failed.\n");
    }

    const tinyos::BUiltinProgramImage images[] = {
        {demo_file},
        {worker_file}
    };
    const UserMemoryLayout memory_layout = {
        .code_address = DEMO_USER_CODE_ADDRESS,
        .stack_address = DEMO_USER_STACK_ADDRESS,
        .data_address = DEMO_USER_DATA_ADDRESS
    };
    if (tinyos::configure_builtin_programs(
            kernel_root, images, memory_layout) != 0) {
        boot_fail("Builtin program configuration failed.\n");
    }
    if (sched_init(kernel_root) != 0) {
        boot_fail("Scheduler initialization failed.\n");
    }
    if (boot_create_tasks(kernel_root) != 0) {
        boot_fail("Boot task creation failed.\n");
    }
    (void)satp_value;
}

void boot_start_scheduler(void) {
    timer_init();
    uart_puts("Starting preemptive scheduler...\n");
    if (scheduler_start() < 0) {
        boot_fail("Scheduler failed to start.\n");
    }
}
