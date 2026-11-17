#include "fs/builtin_fs.hpp"
#include "private/path.hpp"
namespace tinyos::fs {

namespace  {
bool is_vaile_path(std::span<const char> path) noexcept {
    if (path.size() < 2 ||
        path.size() > kMaxBuiltinPathLength ||
        path[0] != '/') {
        return false;
    }

    size_t component_start = 1;

    for (size_t i = 1; i <= path.size(); ++i) {
        if (i < path.size()) {
            if (path[i] == '\0') {
                return false;
            }
            if (path[i] != '/') {
                continue;
            }
        }

        // 遇到斜杠或路径末尾，检查刚结束的组件。
        const size_t length = i - component_start;

        if (length == 0 ||
            (length == 1 && path[component_start] == '.') ||
            (length == 2 &&
            path[component_start] == '.' &&
            path[component_start + 1] == '.')) {
            return false;
        }

        component_start  = i + 1;
    }

    return true;
}

}
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
    if (initialized_ ||
        files.empty() ||
        files.size() > kCapacity) {
        return -1;
    }

    for (const BuiltinFile& file : files) {
        if (!is_vaile_path(file.path)) {
            return -1;
        }
    }

    for (size_t i = 0; i < files.size(); ++i) {
        for (size_t j = i + 1; j < files.size(); ++j) {
            if (paths_equal(files[i].path, files[j].path)) {
                return -1;
            }
        }
    }

    for (size_t i = 0; i < files.size(); ++i) {
        file_[i] = files[i];
    }

    file_count = files.size();
    initialized_ = true;

    return 0;
}

const BuiltinFile* BuiltinFileSystem::lookup(
    std::span<const char> path
) const noexcept {
    if (!initialized_ || !is_vaile_path(path)) {
        return nullptr;
    }

    for (size_t i = 0; i < file_count; ++i) {
        if (paths_equal(file_[i].path, path)) {
            return &file_[i];
        }
    }

    return nullptr;
}
}