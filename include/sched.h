#ifndef TINYOS_SCHED_H
#define TINYOS_SCHED_H

#include <stdint.h>

struct trap_frame;

void sched_init(void);
int task_create(void (*entry)(void));

int scheduler_start(void);

void task_exit(void);

struct trap_frame *
sched_on_timer(struct trap_frame *frame);

struct trap_frame *
sched_on_yield(struct trap_frame *frame);

struct trap_frame*
sched_on_exit(struct trap_frame *frame);

struct trap_frame* sched_on_sleep(
    struct trap_frame* frame,
    unsigned long ticks
);
int task_create_user(uintptr_t entry, uintptr_t user_stack_top);

#endif
