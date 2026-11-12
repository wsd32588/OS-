#ifndef TINYOS_FS_BUILTIN_FS_HPP
#define TINYOS_FS_BUILTIN_FS_HPP

#include <stddef.h>
#include <span>

namespace tinyos::fs {
inline constexpr size_t kMaxBuiltinPathLength = 127;

struct BuiltinFile {
    std::span<const char> path{};
    std::span<const unsigned char> bytes{};
    /* path 和 bytes 都只复制span中的指针/长度，
     *不深拷贝底层内容
     *底层字符和字节必须在文件表使用期间持续有效且只读
     */
    long read_at(size_t offset,
        std::span<unsigned char> destination) const noexcept;
};

class BuiltinFileSystem {
public:
    static constexpr size_t kCapacity = 8;

    BuiltinFileSystem() = default;
    BuiltinFileSystem(const BuiltinFileSystem&) =delete;
    BuiltinFileSystem& operator=(const BuiltinFileSystem&) = delete;
    BuiltinFileSystem(BuiltinFileSystem&&) = delete;
    BuiltinFileSystem& operator=(BuiltinFileSystem&&) = delete;

    int init(std::span<const BuiltinFile> files) noexcept;
    const BuiltinFile* lookup(std::span<const char> path) const noexcept;

private:
    BuiltinFile file_[kCapacity]{};
    size_t file_count{};
    bool initialized_{};
};
}

#endif
