#ifndef TINYOS_PROCESS_BUILTIN_PROGRAM_HPP
#define TINYOS_PROCESS_BUILTIN_PROGRAM_HPP

#include <span>
#include <stdint.h>

struct Sv39PageTable;
struct UserAddressSpace;
struct UserMemoryLayout;

namespace tinyos {
struct BUiltinProgramImage{
    std::span<const unsigned char> bytes{};
};

int configure_builtin_programs(
    const Sv39PageTable* kernel_root,
    std::span<const BUiltinProgramImage> images,
    const UserMemoryLayout& memory_layout
);

int prepare_builtin_user_space(
    uint64_t program_id,
    UserAddressSpace& output,
    uintptr_t& entry_out,
    uintptr_t& stack_top_out);
}

#endif