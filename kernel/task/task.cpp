#include "task/private/task.hpp"

void task::clear_metadata() noexcept {
    context.frame = nullptr;
    entry = nullptr;
    task_id = -1;
    kind = TASK_KIND_KERNEL;
    sleep_ticks = 0;
    proc = nullptr;
}