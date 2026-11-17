#include "fs/open_file.hpp"

#include "console/console_service.hpp"
#include "runtime/panic.h"
#include "fs/builtin_fs.hpp"
#include <stdint.h>

namespace tinyos::fs {
const BuiltinFile* OpenFile::file() const noexcept {
    return file_;
}

size_t OpenFile::offset() const noexcept {
    return offset_;
}

size_t OpenFile::reference_count() const noexcept {
    return reference_count_;
}

OpenFileKind OpenFile::kind() const noexcept {
    return kind_;
}

long OpenFile::write(
    std::span<const unsigned char> source
) const noexcept {
    if (kind_ != OpenFileKind::Console || !can_write()) {
        return -1;
    }

    return console::write_console(source);
}

OpenFileAccess OpenFile::access() const noexcept {
    return access_;
}

bool OpenFile::can_read() const noexcept {
    return kind_ != OpenFileKind::None &&
        access_ != OpenFileAccess::WriteOnly;
}

bool OpenFile::can_write() const noexcept {
    return kind_ != OpenFileKind::None &&
        access_ != OpenFileAccess::ReadOnly;
}
long OpenFile::read_current(
    std::span<unsigned char> destination
) const noexcept {
    if (!can_read()) {
        return -1;
    }
    if (kind_ == OpenFileKind::Console) {
        return console::read_console_current(destination);
    }

    if (kind_ != OpenFileKind::Builtin || file_ == nullptr) {
        return -1;
    }

    return file_->read_at(offset_, destination);
}

int OpenFile::commit_read(size_t count) noexcept {
    if (!can_read()) {
        return -1;
    }
    if (kind_ == OpenFileKind::Console) {
        return console::commit_console_read(count);
    }

    if (kind_ != OpenFileKind::Builtin ||
        file_ == nullptr ||
        offset_ > file_->bytes.size()) {
        return -1;
    }

    const size_t remaining = file_->bytes.size() - offset_;
    if (count > remaining) {
        return -1;
    }

    offset_ += count;
    return 0;
}

OpenFile* OpenFilePool::acquire(
    const BuiltinFile& file
) noexcept {
    for (OpenFile& entry : files_) {
        if (entry.kind_ == OpenFileKind::None) {
            entry.access_ = OpenFileAccess::ReadOnly;
            entry.file_ = &file;
            entry.offset_ = 0;
            entry.reference_count_ = 1;
            entry.kind_ = OpenFileKind::Builtin;
            return &entry;
        }
    }

    return nullptr;
}

bool OpenFilePool::retain(OpenFile* file) noexcept {
    if (file == nullptr) {
        return false;
    }

    for (OpenFile& entry : files_) {
        if (&entry != file) {
            continue;
        }

        if (entry.kind_ == OpenFileKind::None ||
            entry.reference_count_ == 0 ||
            entry.reference_count_ == SIZE_MAX) {
            return false;
        }

        ++entry.reference_count_;
        return true;
    }

    return false;
}

void OpenFilePool::release(OpenFile* file) noexcept {
    if (file == nullptr) {
        PANIC_KERNEL("file is nullptr ,could not be release");
        return;
    }
    
    for (OpenFile& entry : files_) {
        if (&entry != file) {
            continue;
        }

        if (entry.kind_ == OpenFileKind::None || entry.reference_count_ == 0) {
            PANIC_KERNEL("match closed file");
            return;
        }

        --entry.reference_count_;

        if (entry.reference_count_ == 0) {
            entry.access_ = OpenFileAccess::ReadOnly;
            entry.kind_ = OpenFileKind::None;
            entry.file_ = nullptr;
            entry.offset_ = 0;
        }

        return;
    }

    PANIC_KERNEL("release: object does not belong to this pool");

}

size_t OpenFilePool::in_use() const noexcept {
    size_t using_files = 0;

    for (size_t i = 0; i < kCapacity; ++i) {
        if (files_[i].reference_count_ > 0) {
            using_files += 1;
        }
    }

    return using_files;
}

OpenFile* OpenFilePool::acquire_console(
    OpenFileAccess access
) noexcept {
    if ((access != OpenFileAccess::ReadOnly &&
        access != OpenFileAccess::WriteOnly &&
        access != OpenFileAccess::ReadWrite) ||
        !console::console_ready()) {
        return nullptr;
    }

    for (OpenFile& entry : files_) {
        if (entry.kind_ == OpenFileKind::None) {
            entry.file_ = nullptr;
            entry.reference_count_ = 1;
            entry.offset_ = 0;
            entry.kind_ = OpenFileKind::Console;
            entry.access_ = access;
            return &entry;
        }
    }

    return nullptr;
}
}