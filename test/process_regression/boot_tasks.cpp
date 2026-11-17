#include "boot_private.h"

#include "drivers/uart.h"
#include "fs/builtin_fs.hpp"
#include "fs/file_descriptor_table.hpp"
#include "fs/file_service.hpp"
#include "fs/open_file.hpp"
#include "mm/user_address_space.h"
#include "process/builtin_program.hpp"
#include "runtime/panic.h"
#include "task/builtin_program.h"
#include "task/sched.h"

namespace {
#include "boot_file_checks.inc"

void check_boot_files() {
    static constexpr char demo[] = "/bin/demo";
    static constexpr char worker[] = "/bin/worker";
    static constexpr char about[] = "/etc/about";
    const std::span<const char> paths[] = {
        {demo, sizeof(demo) - 1}, {worker, sizeof(worker) - 1}, {about, sizeof(about) - 1}
    };
    for (size_t i = 0; i < 3; ++i) {
        const auto* file = tinyos::fs::lookup_file(paths[i]);
        if (file == nullptr) { PANIC_KERNEL("regression file lookup failed"); }
        if (i < 2) {
            unsigned char header[4]{};
            if (file->read_at(0, header) != 4 || header[0] != 0x7f ||
                header[1] != 'E' || header[2] != 'L' || header[3] != 'F') {
                PANIC_KERNEL("regression ELF header invalid");
            }
        } else {
            unsigned char bytes[32]{};
            if (file->read_at(0, bytes) != 18) { PANIC_KERNEL("regression about read failed"); }
            open_file_test(*file);
            uart_puts("[fs] /etc/about: ");
            for (size_t j = 0; j < 18; ++j) { uart_putchar(static_cast<char>(bytes[j])); }
        }
    }
    uart_puts("[fs] ELF lookup/read OK\n");
}

void release_pending(UserAddressSpace (&spaces)[2]) {
    for (auto& space : spaces) {
        if (user_address_space_release(&space) != SV39_OK) {
            PANIC_KERNEL("process regression: pending address-space cleanup failed");
        }
    }
}
}

extern "C" int boot_create_tasks(const Sv39PageTable* kernel_root) {
    check_boot_files();
    if (kernel_root == nullptr || task_create(task_a) != 0 || task_create(task_b) != 1) {
        return -1;
    }
    UserAddressSpace spaces[2]{};
    uintptr_t entries[2]{};
    uintptr_t stack_tops[2]{};
    int ids[2]{};
    for (size_t i = 0; i < 2; ++i) {
        if (tinyos::prepare_builtin_user_space(
                USER_PROGRAM_DEMO, spaces[i], entries[i], stack_tops[i]) != SV39_OK) {
            release_pending(spaces);
            return -1;
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        ids[i] = task_create_user(entries[i], stack_tops[i], &spaces[i], i);
        if (ids[i] != static_cast<int>(i + 2)) {
            release_pending(spaces);
            return -1;
        }
    }
    return sched_configure_user_reaper(ids[1]);
}
