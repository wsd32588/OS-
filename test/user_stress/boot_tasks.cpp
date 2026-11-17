#include "boot_private.h"

#include "mm/user_address_space.h"
#include "process/builtin_program.hpp"
#include "runtime/panic.h"
#include "task/builtin_program.h"
#include "task/sched.h"

namespace {
void release_pending(UserAddressSpace (&spaces)[2]) {
    for (auto& space : spaces) {
        if (user_address_space_release(&space) != SV39_OK) {
            PANIC_KERNEL("stress bootstrap cleanup failed");
        }
    }
}
}

// Leave all five slots available to the persistent USER and its four workers.
extern "C" int boot_create_tasks(const Sv39PageTable* kernel_root) {
    if (kernel_root == nullptr) {
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
        if (ids[i] != static_cast<int>(i)) {
            release_pending(spaces);
            return -1;
        }
    }
    return sched_configure_user_reaper(ids[1]);
}
