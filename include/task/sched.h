#ifndef TINYOS_SCHED_H
#define TINYOS_SCHED_H

#include <stdint.h>

#include "task/task_exit_info.h"


typedef struct Sv39PageTable Sv39PageTable;
typedef struct UserAddressSpace UserAddressSpace;

struct trap_frame;

#ifdef __cplusplus
extern "C" {
#endif
int sched_init(const Sv39PageTable* kernel_root);
int task_create(void (*entry)(void));

int scheduler_start(void);
int task_peek_exit(
    int requested_id,
    TaskExitInfo* result_out,
    int* matched_id_out
);
int task_reap(int task_id);

void task_exit(void);

struct trap_frame* sched_on_timer(struct trap_frame* frame);

struct trap_frame* sched_on_yield(struct trap_frame* frame);

struct trap_frame* sched_on_exit(struct trap_frame* frame, long status);

struct trap_frame* sched_on_user_fault(struct trap_frame* frame);

struct trap_frame* sched_on_sleep(struct trap_frame* frame, unsigned long ticks);

int sched_fork_current(struct trap_frame* frame);

int sched_exec_current(struct trap_frame* frame, uint64_t program_id, uintptr_t user_argument);

int sched_exec_path(
    struct trap_frame* frame, uintptr_t path_address,
    uint64_t path_length, uintptr_t user_argument);

int sched_configure_user_reaper(int task_id);

int task_create_user(
    uintptr_t entry,
    uintptr_t user_stack_top,
    UserAddressSpace* space,
    uintptr_t user_argument
);

long sched_open_file(struct trap_frame* frame,
                     uintptr_t path_address, uint64_t path_length);
long sched_read_file(struct trap_frame* frame, long requested_fd,
                     uintptr_t destination_address, uint64_t length);
long sched_close_file(struct trap_frame* frame, long requested_fd);
long sched_write_file(
    struct trap_frame* frame, long requested_fd,
    uintptr_t source_address, uint64_t length
);
const Sv39PageTable* sched_current_user_root(void);

#ifdef __cplusplus
}
#endif

#endif
