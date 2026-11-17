#include "boot_private.h"

#include "task/sched.h"
#include "mm/user_address_space.h"
#include "process/builtin_program.hpp"
#include "runtime/panic.h"
#include "task/builtin_program.h"

int boot_create_tasks(const Sv39PageTable* kernel_root) {
#if TINYOS_CONSOLE_USER_IO
    if (kernel_root == nullptr) {
        return -1;
    }

    UserAddressSpace candidate{};
    uintptr_t entry{};
    uintptr_t stack_top{};

    const auto reject_candidate = [&candidate]() -> int {
        if (user_address_space_release(&candidate) != SV39_OK) {
            PANIC_KERNEL("boot user space cleanup failed");
        }
        return -1;
    };

    const int result = tinyos::prepare_builtin_user_space(
        USER_PROGRAM_DEMO, candidate, entry, stack_top);
    if (result != SV39_OK) {
        return reject_candidate();
    }

    const int task_id = task_create_user(
        entry, stack_top, &candidate, 0);
    if (task_id < 0) {
        return reject_candidate();
    }

    return sched_configure_user_reaper(task_id);
#else
    (void)kernel_root;
    return task_create(task_a) < 0 ? -1 : 0;
#endif
}
