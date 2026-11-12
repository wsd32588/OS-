#include "process/spawn.h"
#include "task/sched.h"
#include "drivers/uart.h"
#include "mm/user_address_space.h"
#include "process/builtin_program.hpp"

extern "C" int process_spawn_builtin(uint64_t program_id, uintptr_t user_argument) {

    UserAddressSpace candidate{};
    uintptr_t entry{};
    uintptr_t stack_top{};

    const int result = tinyos::prepare_builtin_user_space(
        program_id,
        candidate,
        entry,
        stack_top);

    if (result != SV39_OK) {
        int res = user_address_space_release(&candidate);
        if (res != SV39_OK) {
            uart_puts("release space candidate failed");
            for (;;) {
                asm volatile("wfi");
            }
        }
        return -1;
    }

    int new_id = task_create_user(
        entry,
        stack_top,
        &candidate,
        user_argument);

    if (new_id < 0) {
        int res = user_address_space_release(&candidate);
        if (res != SV39_OK) {
            uart_puts("release space candidate failed");
            for (;;) {
                asm volatile("wfi");
            }
        }
        return -1;
    }
    

    return new_id;
}