#ifndef TINYOS_SCHED_H
#define TINYOS_SCHED_H

struct trap_frame;

void sched_init(void);
int task_create(void (*entry)(void));

int scheduler_start(void);

void task_exit(void);

struct trap_frame *
sched_on_timer(struct trap_frame *frame);

#endif
