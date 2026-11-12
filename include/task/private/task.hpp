#pragma once

#include "arch/riscv/task_context.h"
#include "process/process.hpp"

enum task_state {
    TASK_UNUSED,
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD,
    TASK_BLOCKED,
};

enum task_kind {
    TASK_KIND_KERNEL,
    TASK_KIND_USER
};

struct task {
    RiscvTaskContext context;

    void (*entry)(void);
    int task_id;
    enum task_state state;
    enum task_kind kind;
    unsigned long sleep_ticks;
    tinyos::Process* proc;

    constexpr task()  = default;

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    task(task&&) = delete;
    task& operator=(task&&) = delete;

    void clear_metadata() noexcept;
};