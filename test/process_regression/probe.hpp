#ifndef TINYOS_TEST_PROCESS_REGRESSION_PROBE_HPP
#define TINYOS_TEST_PROCESS_REGRESSION_PROBE_HPP

#include "arch/riscv/trap.h"
#include "mm/user_address_space.h"
#include "task/private/task.hpp"

extern "C" {
void regression_after_boot();
void regression_on_timer(const trap_frame* frame);
const task* regression_task(int slot);
int regression_current_slot();
size_t regression_service_in_use();
}

#endif
