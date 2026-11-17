#ifndef TINYOS_FS_OPEN_FILE_HPP
#define TINYOS_FS_OPEN_FILE_HPP

#include <stddef.h>
#include <span>

namespace tinyos::fs {
struct BuiltinFile;

enum class OpenFileKind {
    None,
    Builtin,
    Console,
};

enum class OpenFileAccess {
    ReadOnly,
    WriteOnly,
    ReadWrite,
};

class OpenFile {
public:
    constexpr OpenFile() = default;
    OpenFile(const OpenFile&) = delete;
    OpenFile& operator=(const OpenFile&) = delete;
    OpenFile(OpenFile&&) = delete;
    OpenFile& operator=(OpenFile&&) = delete;

    const BuiltinFile* file() const noexcept;
    size_t offset() const noexcept;
    size_t reference_count() const noexcept;
    long read_current(std::span<unsigned char> destination) const noexcept;
    int commit_read(size_t count)noexcept;

    OpenFileAccess access() const noexcept;
    bool can_read() const noexcept;
    bool can_write() const noexcept;
    OpenFileKind kind() const noexcept;
    long write(std::span<const unsigned char> source) const noexcept;

private:
    friend class OpenFilePool;
    OpenFileAccess access_{OpenFileAccess::ReadOnly};
    OpenFileKind kind_{OpenFileKind::None};
    const BuiltinFile* file_{};
    size_t offset_{};
    size_t reference_count_{};
};

class OpenFilePool {
public:
    static constexpr size_t kCapacity = 40;
    constexpr OpenFilePool() = default;
    OpenFilePool (const OpenFilePool&) = delete;
    OpenFilePool& operator=(const OpenFilePool&) = delete;
    OpenFilePool (OpenFilePool&&) = delete;
    OpenFilePool& operator=(OpenFilePool&&) = delete;

    OpenFile* acquire_console(
        OpenFileAccess access = OpenFileAccess::ReadWrite
    ) noexcept;
    OpenFile* acquire(const BuiltinFile& file) noexcept;
    bool retain(OpenFile* file) noexcept;
    void release(OpenFile* file) noexcept;
    size_t in_use() const noexcept;

private:
    OpenFile files_[kCapacity]{};
};
}
#endif