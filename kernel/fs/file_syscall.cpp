#include "fs/file_syscall.hpp"

#include "fs/builtin_fs.hpp"
#include "runtime/panic.h"
#include "mm/user_access.h"
#include "process/process.hpp"
#include "task/syscall.h"
#include "fs/file_service.hpp"
#include "fs/open_file.hpp"
#include "task/syscall.h"
#include "console/console_service.hpp"

namespace tinyos::fs {

long syscall_open_file(
    Process& process,
    uintptr_t path_address,
    uint64_t path_length
) noexcept {
    const Sv39PageTable* root = process.root();

    if (root == nullptr ||
        !file_service_ready() ||
        path_length == 0 ||
        path_length > kMaxBuiltinPathLength) {
        return -1;
    }

    char path_buffer[kMaxBuiltinPathLength]{};
    const int result = user_copy_from(
        root,
        path_buffer,
        path_address,
        static_cast<size_t>(path_length));
    if (result != SV39_OK) {
        return -1;
    }

    return process.open_file(
        std::span<const char>(
            path_buffer, static_cast<size_t>(path_length)));
}

long syscall_read(
    Process& process,
    long requested_fd,
    uintptr_t destination_address,
    uint64_t length
) noexcept {
    const Sv39PageTable* root = process.root();

    if (!file_service_ready() ||
        root == nullptr ||
        requested_fd < 0 ||
        requested_fd >= static_cast<long>(FileDescriptorTable::kCapacity) ||
        length > SYSCALL_READ_FILE_MAX) {
        return -1;
    }

    OpenFile* file = process.find_open_file(
        static_cast<int>(requested_fd));
    if (file == nullptr ||
        !file->can_read()) {
        return -1;
    }

    const bool is_console = file->kind() == OpenFileKind::Console;
    if (is_console && !console::console_user_io_enabled()) {
        return -1;
    }

    if (length == 0) {
        return 0;
    }

    if (is_console && console::poll_console_input() < 0) {
        return -1;
    }

    unsigned char buffer[SYSCALL_READ_FILE_MAX]{};
    const long count = file->read_current(
        std::span<unsigned char>(buffer, static_cast<size_t>(length)));

    if (count < 0 || static_cast<uint64_t>(count) > length) {
        return -1;
    }
    if (count == 0) {
        if (file->kind() == OpenFileKind::Console) {
            return SYSCALL_ERR_WOULD_BLOCK;
        }

        return 0;
    }

    if (user_copy_to(
            root, destination_address,
            buffer, static_cast<size_t>(count)) != SV39_OK) {
        return -1;
    }

    if (file->commit_read(static_cast<size_t>(count)) != 0) {
        PANIC_KERNEL("read: committing copied bytes failed");
    }

    return count;
}

long syscall_close(
    Process& process,
    long requested_fd
) noexcept {
    if (!file_service_ready() ||
        requested_fd < 0 ||
        requested_fd >= static_cast<long>(FileDescriptorTable::kCapacity)) {
        return -1;
    }

    return process.close_file(static_cast<int>(requested_fd));
}

long syscall_write_fd(
    Process& process, long requested_fd,
    uintptr_t source_address, uint64_t length
) noexcept {
    const Sv39PageTable* root = process.root();
    if (!file_service_ready() ||
        root == nullptr ||
        requested_fd < 0 ||
        length > SYSCALL_WRITE_MAX ||
        requested_fd >= static_cast<long>(FileDescriptorTable::kCapacity)) {
        return -1;
    }
    OpenFile* file = process.find_open_file(requested_fd);
    if (file == nullptr ||
        file->kind() != OpenFileKind::Console ||
        !console::console_user_io_enabled() ||
        !file->can_write()) {
        return -1;
    }

    unsigned char buffer[SYSCALL_WRITE_MAX]{};

    if (user_copy_from(
        root, buffer, source_address, length) != SV39_OK) {
        return -1;
    }

    return file->write(
        std::span<const unsigned char>{
            buffer, static_cast<size_t>(length)
        }
    );
}

long syscall_dup2(
    Process &process,
    long source_fd, long target_fd
) noexcept {
    const long capacity = static_cast<long>(FileDescriptorTable::kCapacity);
    if (!file_service_ready() ||
        source_fd < 0 || source_fd >= capacity ||
        target_fd < 0 || target_fd >= capacity) {
        return -1;
    }

    return process.duplicate_file(
    static_cast<int>(source_fd), static_cast<int>(target_fd));
}

}

extern "C" long fs_syscall_read_file(
    const Sv39PageTable *root,
    uintptr_t path_address, uint64_t path_length,
    uint64_t offset, uintptr_t destination_address, uint64_t length
) {
    if (!tinyos::fs::file_service_ready() ||
        root == nullptr ||
        path_length == 0 ||
        path_length > tinyos::fs::kMaxBuiltinPathLength ||
        length > SYSCALL_READ_FILE_MAX) {
        return -1;
    }

    
    char path[tinyos::fs::kMaxBuiltinPathLength]{};

    int result = user_copy_from(root, &path,
        path_address, static_cast<size_t>(path_length));
    if (result != SV39_OK) {
        return -1;
    }

    const tinyos::fs::BuiltinFile* file = tinyos::fs::lookup_file(
        std::span<const char>(path, static_cast<size_t>(path_length))
    );

    if (file == nullptr) {
        return -1;
    }

    if (length == 0 || offset >= file->bytes.size()) {
        return 0;
    }

    unsigned char buffer[SYSCALL_READ_FILE_MAX]{};

    const long count = file->read_at(
        static_cast<size_t>(offset),
        std::span<unsigned char>(buffer, static_cast<size_t>(length)));

    if (count == 0) {
        return 0;
    } else if (count == -1) {
        return -1;
    } else {
        result = user_copy_to(
            root, destination_address, buffer, static_cast<size_t>(count));
        if (result != SV39_OK) {
            return -1;
        }       
    }

    return count;
}