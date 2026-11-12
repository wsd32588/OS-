#pragma once

#include <stdint.h>

#include "task/task_exit_info.h"

struct task;
struct trap_frame;
typedef struct Sv39PageTable Sv39PageTable;
typedef struct UserAddressSpace UserAddressSpace;
namespace tinyos { class TaskTable; }

namespace tinyos::user_task {
int create(TaskTable& table, const Sv39PageTable* kernel_root,
           uintptr_t entry, uintptr_t stack_top, UserAddressSpace& source,
           uintptr_t argument, int parent_id);
int fork(TaskTable& table, task& parent, const trap_frame& frame);
int exec(task& current, trap_frame& frame,
         const Sv39PageTable* kernel_root,uint64_t program_id,  uintptr_t argument);
void finish(TaskTable& table, task& current, const trap_frame& frame,
            const Sv39PageTable* kernel_root, int reaper_id,
            TaskExitReason reason, long status);
int find_reaper(const TaskTable& table, int reaper_id) noexcept;
}
