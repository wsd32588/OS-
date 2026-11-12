#include "process/builtin_program.hpp"

#include "drivers/uart.h"
#include "mm/address.hpp"
#include "mm/user_loader.h"
#include "mm/user_address_space.h"
namespace tinyos {
namespace {
constexpr size_t kBuiltinProgramCount = 2;

struct BuiltinProgramRecord {
    std::span<const unsigned char> bytes{};
    UserElfLayout layout{};
};

BuiltinProgramRecord g_programs[kBuiltinProgramCount]{};
UserMemoryLayout g_memory_layout{};
uintptr_t g_stack_top{};
const Sv39PageTable* g_root{};
}

int configure_builtin_programs(
    const Sv39PageTable *kernel_root,
    std::span<const BUiltinProgramImage> images,
    const UserMemoryLayout &memory_layout
) {
    if (g_root != nullptr) {
        return -1;
    }
    if (kernel_root == nullptr ||
        images.size() != 2 ||
        memory_layout.code_address == 0 ||
        memory_layout.data_address == 0 ||
        memory_layout.stack_address == 0
        ) {
        return -1;
    }

    const PhysAddr root{kernel_root};
    if (!root.is_valid() || !root.is_page_aligned()) {
        return -1;
    }

    const VirtAddr starts[] = {
        VirtAddr{memory_layout.code_address},
        VirtAddr{memory_layout.data_address},
        VirtAddr{memory_layout.stack_address}
    };

    for (const VirtAddr& address : starts) {
        if (!address.is_valid() || !address.is_page_aligned()) {
            return -1;
        }
    }

    VirtAddr stack_top{};
    if (!starts[2].try_add(SV39_PAGE_SIZE, stack_top)) {
        return -1;
    }

    UserElfLayout parsed_layouts[2]{};

    for (size_t i = 0; i < images.size(); ++i) {
        const auto& image = images[i];

        if (image.bytes.empty()) {
            return -1;
        }

        const int result = user_elf_parse(
            image.bytes.data(),
            image.bytes.size(),
            &parsed_layouts[i]);

        if (result != USER_ELF_OK) {
            return -1;
        }
        if (parsed_layouts[i].entry_address == 0 ||
            parsed_layouts[i].program_header_count == 0 ||
            parsed_layouts[i].load_segment_count != USER_ELF_MAX_LOAD_SEGMENTS) {
            return -1;
        }
    }

    for (size_t i = 0; i < images.size(); ++i) {
        g_programs[i] = BuiltinProgramRecord{
            .bytes = images[i].bytes,
            .layout = parsed_layouts[i]
        };
    }

    g_memory_layout = memory_layout;
    g_stack_top = stack_top.val();


    g_root = kernel_root;
    return 0;
}

int prepare_builtin_user_space(
    uint64_t program_id,
    UserAddressSpace &output,
    uintptr_t &entry_out,
    uintptr_t &stack_top_out
) {
    if (g_root == nullptr || program_id >= kBuiltinProgramCount) {
        return -1;
    }

    const BuiltinProgramRecord& program = g_programs[program_id];

    const int create_result = user_address_space_create(g_root, &output);
    if (create_result != SV39_OK) {
        return create_result;
    }

    const int load_result = user_memory_load_elf(
        output.root,
        program.bytes.data(),
        program.bytes.size(),
        &program.layout,
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

    entry_out = program.layout.entry_address;
    stack_top_out = g_stack_top;

    return SV39_OK;
}
}
