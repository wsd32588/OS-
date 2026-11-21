#include "fs/file_descriptor_table.hpp"
#include "fs/open_file.hpp"
#include "runtime/panic.h"

namespace tinyos::fs {
int FileDescriptorTable::open(
    OpenFilePool& pool, const BuiltinFile& file
) noexcept {
    for (size_t i = 0; i < kCapacity; ++i) {
        if (entries_[i] != nullptr) {
            continue;
        }

        OpenFile* opened = pool.acquire(file);
        if (opened == nullptr) {
            return -1;
        }

        entries_[i] = opened;
        return static_cast<int>(i);
    }

    return -1;
}

int FileDescriptorTable::close(
    OpenFilePool& pool, int fd
) noexcept {
    if (fd < 0 || fd >= static_cast<int>(kCapacity)) {
        return -1;
    }
    if (entries_[fd] == nullptr) {
        return -1;
    }

    pool.release(entries_[fd]);

    entries_[fd] = nullptr;
    return 0;
}

void FileDescriptorTable::close_all(
    OpenFilePool& pool
) noexcept {
    for (size_t i = 0; i < kCapacity; ++i) {
        if (entries_[i] == nullptr) {
            continue;
        }

        const int result = close(pool, static_cast<int>(i));
        if (result != 0) {
            PANIC_KERNEL(
                "close_all: closing an occupied fd failed");
        }
    }
}

OpenFile* FileDescriptorTable::get(
    int fd
) noexcept {
    if (fd < 0 || fd >= static_cast<int>(kCapacity)) {
        return nullptr;
    }
    return entries_[fd];
}

const OpenFile* FileDescriptorTable::get(int fd) const noexcept {
    if (fd < 0 || fd >= static_cast<int>(kCapacity)) {
        return nullptr;
    }
    return entries_[fd];
}

bool FileDescriptorTable::empty() const noexcept {
    for (const OpenFile* entry : entries_) {
        if (entry != nullptr) {
            return false;
        }
    }

    return true;
}

int FileDescriptorTable::open_console(
    OpenFilePool& pool,
    OpenFileAccess access
) noexcept {
    for (size_t i = 0; i < kCapacity; ++i) {
        if (entries_[i] == nullptr) {
            OpenFile* file = pool.acquire_console(access);
            if (file == nullptr) {
                return -1;
            }
            entries_[i] = file;
            return static_cast<int>(i);
        }
    }

    return -1;
}

int FileDescriptorTable::initialize_standard_stream(
    OpenFilePool& pool
) noexcept {
    if (!empty()) {
        return -1;
    }

    FileDescriptorTable pending{};
    const int fd_read_only =
        pending.open_console(pool, OpenFileAccess::ReadOnly);
    const int fd_write_only =
        pending.open_console(pool, OpenFileAccess::WriteOnly);
    const int fd_read_write =
        pending.open_console(pool, OpenFileAccess::WriteOnly);
    if (fd_read_only != 0 ||
        fd_write_only != 1 ||
        fd_read_write != 2) {
        pending.close_all(pool);
        return -1;
    }

    adopt(pending);
    return 0;
}

void FileDescriptorTable::adopt(
    FileDescriptorTable& source
) noexcept{
    if (!empty() ||
        this == &source) {
            PANIC_KERNEL("target is not null");
    }
    for (size_t i = 0; i < kCapacity; ++i) {
        entries_[i] = source.entries_[i];
        source.entries_[i] = nullptr;
    }
}

int FileDescriptorTable::inherit_from(
    OpenFilePool& pool, const FileDescriptorTable& source
) noexcept {
    if (!empty() ||
        this == &source) {
        return -1;
    }
    FileDescriptorTable pending{};
    for (size_t i = 0; i < kCapacity; ++i) {
        OpenFile* entry = source.entries_[i];
        if (entry == nullptr) {
            continue;
        }
        if (!pool.retain(entry)) {
            pending.close_all(pool);
            return -1;
        }

        pending.entries_[i] = entry;
    }

    adopt(pending);
    return 0;
}

int FileDescriptorTable::duplicate_to(
    OpenFilePool& pool,
    int source_fd, int target_fd
) noexcept {

    // 校验在Pool内，且不能为同一个文件
    const int capacity = static_cast<int>(kCapacity);
    if (source_fd < 0 || source_fd >= capacity ||
        target_fd < 0 || target_fd >= capacity) {
        return -1;
    }
    
    // 获取文件
    OpenFile* source_file = entries_[source_fd];
    OpenFile* target_file = entries_[target_fd];

    // 源文件非空且目标文件与源文件指向相同，可以直接返回目标fd
    if (source_file != nullptr &&
        source_file == target_file) {
        return target_fd;
    }

    // 为目标槽取得一条源对象引用
    if (!pool.retain(source_file)) {
        return -1;
    }

    entries_[target_fd] = source_file;

    // 目标槽原先为空，可以直接返回
    if (target_file == nullptr) {
        return target_fd;
    }

    pool.release(target_file);

    return target_fd;
}

}