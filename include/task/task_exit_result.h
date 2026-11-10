#ifndef TINYOS_TASK_EXIT_RESULT_H
#define TINYOS_TASK_EXIT_RESULT_H
#include <stddef.h>
#include <stdint.h>

#define TASK_RESULT_REQUESTED   UINT64_C(1)
#define TASK_RESULT_FAULT        UINT64_C(2)

typedef struct TaskExitResult {
    uint64_t reason;
    int64_t status;
    uint64_t fault_scause;
}TaskExitResult;


#ifdef __cplusplus
#define TINYOS_TASK_RESULT_ASSERT static_assert
#else
#define TINYOS_TASK_RESULT_ASSERT _Static_assert
#endif

TINYOS_TASK_RESULT_ASSERT(
    sizeof(TaskExitResult) == 24,
    "TaskExitResult size must be 24 bytes"
);

TINYOS_TASK_RESULT_ASSERT(
    offsetof(TaskExitResult, reason) == 0,
    "TaskExitResult.reason offset must be 0"
);

TINYOS_TASK_RESULT_ASSERT(
    offsetof(TaskExitResult, status) == 8,
    "TaskExitResult.status offset must be 8"
);

TINYOS_TASK_RESULT_ASSERT(
    offsetof(TaskExitResult, fault_scause) == 16,
    "TaskExitResult.fault_scause offset must be 16"
);

#undef TINYOS_TASK_RESULT_ASSERT
#endif