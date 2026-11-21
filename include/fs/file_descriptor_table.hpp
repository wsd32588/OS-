#ifndef TINYOS_FS_FILE_DESCRIPTOR_TABLE_HPP
#define TINYOS_FS_FILE_DESCRIPTOR_TABLE_HPP

#include <stddef.h>
#include "fs/open_file.hpp"

namespace tinyos::fs {
struct BuiltinFile;
class OpenFile;
class OpenFilePool;

class FileDescriptorTable {
public:
    static constexpr size_t kCapacity = 8;
    constexpr FileDescriptorTable() = default;
    FileDescriptorTable(const FileDescriptorTable&) = delete;
    FileDescriptorTable& operator=(const FileDescriptorTable&) = delete;
    FileDescriptorTable (FileDescriptorTable&&) = delete;
    FileDescriptorTable& operator=(FileDescriptorTable&&) = delete;

    int open(OpenFilePool& pool, const BuiltinFile& file) noexcept;
    int close(OpenFilePool& pool, int fd) noexcept;
    void close_all(OpenFilePool& pool) noexcept;
    OpenFile* get(int fd) noexcept;
    const OpenFile* get(int fd) const noexcept;
    bool empty() const noexcept;

    int open_console(
        OpenFilePool& pool,
        OpenFileAccess access = OpenFileAccess::ReadWrite) noexcept;
    int initialize_standard_stream(OpenFilePool& pool) noexcept;
    int inherit_from(OpenFilePool& pool, const FileDescriptorTable& source) noexcept;

    int duplicate_to(OpenFilePool& pool, int source_fd, int target_fd) noexcept;
    void adopt(FileDescriptorTable& source) noexcept;
private:
    OpenFile* entries_[kCapacity]{};
};
}


#endif