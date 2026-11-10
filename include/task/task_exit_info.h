#ifndef TINYOS_TASK_EXIT_INFO_H
#define TINYOS_TASK_EXIT_INFO_H

typedef enum TaskExitReason { TASK_EXIT_NONE, TASK_EXIT_REQUESTED, TASK_EXIT_FAULT } TaskExitReason;

/* Kernel record retained after address-space release; not a userspace wire format. */
typedef struct TaskExitInfo {
    TaskExitReason reason;
    long status;
    unsigned long fault_scause;
} TaskExitInfo;

#endif
