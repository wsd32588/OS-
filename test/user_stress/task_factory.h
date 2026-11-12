#ifndef TINYOS_TEST_USER_TASK_FACTORY_H
#define TINYOS_TEST_USER_TASK_FACTORY_H

#include "user_api.h"

#ifndef TEST_STRESS_ROUNDS
#define TEST_STRESS_ROUNDS 500
#endif

#define TEST_WORKERS 4
#define TEST_EXEC_ARGUMENT ((uintptr_t)1 << 28)

typedef struct TestTaskBatch {
    long ids[TEST_WORKERS];
    unsigned long round;
} TestTaskBatch;

typedef struct TestTaskFactory {
    long last_id;
} TestTaskFactory;

_Noreturn void stress_fail(const char* reason);
long stress_worker(uintptr_t argument, int inherited);
void test_factory_create(TestTaskFactory* factory, TestTaskBatch* batch,
                         unsigned long round);
void test_factory_collect(const TestTaskBatch* batch);
long test_worker_status(uintptr_t argument);

#endif
