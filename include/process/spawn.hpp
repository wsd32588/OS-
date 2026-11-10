#pragma once

#include <span>
#include "mm/vm.h"
#include "mm/user_loader.h"
#include "mm/user_address_space.h"
#include "user/user_elf.h"

namespace tinyos {
int configure_builtin_spawn(
    const Sv39PageTable* kernel_root,
    std::span<const unsigned char> image,
    const UserElfLayout& elf_layout,
    const UserMemoryLayout& memory_layout
);

int prepare_builtin_user_space(
    UserAddressSpace& output,
    uintptr_t& entry_out,
    uintptr_t& stack_top_out
);
}