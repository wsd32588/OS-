#pragma once
#include "mm/user_loader.h"
#include <span>

namespace tinyos {
int load_user_elf(
    Sv39PageTable& root,
    std::span<const unsigned char> image,
    const UserElfLayout& elf_layout,
    const UserMemoryLayout& memory_layout,
    UserMemory& memory_out
);
}