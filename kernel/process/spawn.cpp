#include "task/sched.h"
#include "mm/address.hpp"
#include "drivers/uart.h"
#include "user/user_elf.h"
#include "process/spawn.hpp"
#include "mm/user_address_space.h"

namespace {
const Sv39PageTable* g_root{};
std::span<const unsigned char> g_image{};
UserElfLayout g_elf_layout{};
UserMemoryLayout g_memory_layout{};
uintptr_t g_stack_top{};
}

namespace tinyos {
int configure_builtin_spawn(
    const Sv39PageTable* kernel_root,
    std::span<const unsigned char> image,
    const UserElfLayout& elf_layout,
    const UserMemoryLayout& memory_layout
) {
    if (g_root != nullptr) {
        return -1;
    }
    if (kernel_root == nullptr ||
        image.empty() ||
        elf_layout.entry_address == 0 ||
        elf_layout.load_segment_count == 0 ||
        elf_layout.load_segment_count != USER_ELF_MAX_LOAD_SEGMENTS ||
        elf_layout.program_header_count == 0 ||
        memory_layout.code_address == 0 ||
        memory_layout.data_address == 0 ||
        memory_layout.stack_address == 0
        ) {
            return -1;
    }

    const PhysAddr root_address(kernel_root);

    if (!root_address.is_valid() ||
        !root_address.is_page_aligned()) {
            return -1;
    }
    
    const VirtAddr starts[] = {
        VirtAddr(memory_layout.code_address),
        VirtAddr(memory_layout.data_address),
        VirtAddr(memory_layout.stack_address)
    };

    for (const auto& address : starts) {
        if (!address.is_valid() ||
            !address.is_page_aligned()) {
                return -1;
        }
    }

    VirtAddr stack_top{};
    
    if (!VirtAddr(memory_layout.stack_address)
            .try_add(SV39_PAGE_SIZE, stack_top)) {
        return -1;
    }

    g_image = image;
    g_elf_layout = elf_layout;
    g_memory_layout = memory_layout;
    g_stack_top = stack_top.val();

    g_root = kernel_root; // 最后赋值，表示配置完成。
    return 0;
}

int prepare_builtin_user_space(
    UserAddressSpace& output,
    uintptr_t& entry_out,
    uintptr_t& stack_top_out
) {
    if (g_root == nullptr) {
        return -1;
    }

    // 建立私有根，借用共享内核映射。
    const int create_result =
        user_address_space_create(g_root, &output);
    if (create_result != SV39_OK) {
        return create_result;
    }

    // 分配用户内容页，装入原始 ELF，并建立用户映射。
    const int load_result = user_memory_load_elf(
        output.root,
        g_image.data(),
        g_image.size(),
        &g_elf_layout,
        &g_memory_layout,
        &output.user_memory);

    if (load_result == SV39_ERR_PAGE_FREE_FAILED) {
        uart_puts("ELF loader rollback failed\n");
        for (;;) {
            asm volatile("wfi");
        }
    }

    if (load_result != SV39_OK) {
        return load_result;
    }

    entry_out = g_elf_layout.entry_address;
    stack_top_out = g_stack_top;
    return SV39_OK;
}
}

extern "C" int process_spawn_builtin(uintptr_t user_argument) {

    UserAddressSpace candidate{};
    uintptr_t entry{};
    uintptr_t stack_top{};

    const int result = tinyos::prepare_builtin_user_space(
        candidate,
        entry,
        stack_top);

    if (result != SV39_OK) {
        int res = user_address_space_release(&candidate);
        if (res != SV39_OK) {
            uart_puts("release space candidate failed");
            for (;;) {
                asm volatile("wfi");
            }
        }
        return -1;
    }

    int new_id = task_create_user(
        entry,
        stack_top,
        &candidate,
        user_argument);

    if (new_id < 0) {
        int res = user_address_space_release(&candidate);
        if (res != SV39_OK) {
            uart_puts("release space candidate failed");
            for (;;) {
                asm volatile("wfi");
            }
        }
        return -1;
    }
    

    return new_id;
}