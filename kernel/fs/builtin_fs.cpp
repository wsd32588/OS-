#include "fs/builtin_fs.hpp"

namespace tinyos::fs {
long BuiltinFile::read_at(
    size_t offset,
    std::span<unsigned char> destination
) const noexcept {
    if (destination.empty() ||
        offset >= bytes.size()) {
        return 0;
    }

    size_t remaining = bytes.size() - offset;
    size_t count =
        (remaining > destination.size()) ? destination.size() : remaining;

    if (count > static_cast<size_t>(__LONG_MAX__)) {
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        destination[i] = bytes[offset + i];
    }

    return static_cast<long>(count);
}

int BuiltinFileSystem::init (
    std::span<const BuiltinFile> files
) noexcept {

}
}