#ifndef TINYOS_FS_FILE_SYSCALL_HPP
#define TINYOS_FS_FILE_SYSCALL_HPP

#include <stdint.h>

namespace tinyos {
class Process;
}

namespace tinyos::fs {

long syscall_open_file(
    Process& process, uintptr_t path_address,
    uint64_t path_length) noexcept;

long syscall_read(
    Process& process, long requested_fd,
    uintptr_t destination_address, uint64_t length) noexcept;

long syscall_close(
    Process& process, long requested_fd) noexcept;

long syscall_write_fd(
    Process& process, long requested_fd,
    uintptr_t source_address, uint64_t length
) noexcept;

}

#endif