#include "fs/file_service.hpp"
#include "fs/open_file.hpp"
#include "runtime/panic.h"
#include "private/path.hpp"
#include "console/console_service.hpp"

namespace tinyos::fs {
namespace {
const BuiltinFileSystem* g_filesystem{};
constinit OpenFilePool g_open_files{};
}

int configure_file_service(
    const BuiltinFileSystem& filesystem
) noexcept {
    if (!filesystem.initialized() ||
        g_filesystem != nullptr) {
        return -1;
    }

    g_filesystem = &filesystem;
    return 0;
}

bool file_service_ready() noexcept {
    return g_filesystem != nullptr;
}

const BuiltinFile* lookup_file(
    std::span<const char> path
) noexcept {
    if (!file_service_ready()) {
        return nullptr;
    }

    return g_filesystem->lookup(path);
}

int open_file(
    FileDescriptorTable& table,
    std::span<const char> path
) noexcept {
    if (path.empty() ||
        !file_service_ready()) {
        return -1;
    }
    static constexpr char kConsolePath[] = "/dev/console";

    if (paths_equal(path, {kConsolePath, sizeof(kConsolePath) - 1})) {
        if (!console::console_user_io_enabled()) {
            return -1;
        }

        return table.open_console(g_open_files);
    }
    const BuiltinFile* file = lookup_file(path);
    if (file == nullptr) {
        return -1;
    }
    return table.open(g_open_files, *file);
}

int close_file(FileDescriptorTable& table, int fd) noexcept {
    if (!file_service_ready()) {
        return -1;
    }

    return table.close(g_open_files, fd);
}

void close_all_files(FileDescriptorTable& table) noexcept {
    if (table.empty()) {
        return;
    }

    if (!file_service_ready()) {
        PANIC_KERNEL("unconfigured file service owns a nonempty table");
    }

    table.close_all(g_open_files);
}

int inherit_files(
    const FileDescriptorTable& source,
    FileDescriptorTable& output
) noexcept {
    if (!file_service_ready()) {
        return -1;
    }

    return output.inherit_from(g_open_files, source);
}

int duplicate_file(
    FileDescriptorTable &table,
    int source_fd, int target_fd
) noexcept {
    if (!file_service_ready()) {
        return -1;
    }

    return table.duplicate_to(g_open_files, source_fd, target_fd);
}

int prepare_standard_streams(
    FileDescriptorTable& output
) noexcept {
    if (!file_service_ready() ||
        !console::console_user_io_enabled()) {
        return -1;
    }

    return output.initialize_standard_stream(g_open_files);
}

}