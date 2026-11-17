#ifndef TINYOS_FS_FILE_SERVICE_HPP
#define TINYOS_FS_FILE_SERVICE_HPP

#include <span>
#include <stddef.h>
#include "fs/builtin_fs.hpp"
#include "fs/file_descriptor_table.hpp"

namespace tinyos::fs {
int configure_file_service(const BuiltinFileSystem& filesystem) noexcept;
bool file_service_ready() noexcept;
const BuiltinFile* lookup_file(std::span<const char> path) noexcept;
int open_file(
    FileDescriptorTable& table, std::span<const char> path) noexcept;
int close_file(FileDescriptorTable& table, int fd) noexcept;
void close_all_files(FileDescriptorTable& table) noexcept;
int inherit_files(
    const FileDescriptorTable& source, FileDescriptorTable& output) noexcept;
int prepare_standard_streams(FileDescriptorTable& output) noexcept;
}

#endif
