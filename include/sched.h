#ifndef TINYOS_SCHED_H
#define TINYOS_SCHED_H

#include <stdint.h>

#include "vm.h"

struct trap_frame;

typedef struct {
    Sv39PageTable* root; //根页表
    void* code_page; //PMM返回的指针
    void* stack_page;
    uintptr_t code_address; // 用户虚地址
    uintptr_t stack_address;
} UserMemory;

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

int task_create_user(
    uintptr_t entry,
    uintptr_t user_stack_top,
    UserMemory* memory
);

#endif
