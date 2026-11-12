#pragma once

#include <span>

#include "mm/user_loader.h"
#include "mm/vm.h"
#include "user/user_elf.h"

namespace tinyos {
int configure_builtin_spawn(
    const Sv39PageTable* kernel_root,
    std::span<const unsigned char> image,
    const UserElfLayout& elf_layout,
    const UserMemoryLayout& memory_layout
);
}