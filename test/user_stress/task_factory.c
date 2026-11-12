#include "task_factory.h"

_Noreturn void stress_fail(const char* reason) {
    size_t length = 0;
    while (reason[length] != '\0') {
        ++length;
    }
    const char prefix[] = "[stress-user] FAIL ";
    (void)user_write(prefix, sizeof(prefix) - 1);
    (void)user_write(reason, length);
    (void)user_putchar('\n');
    user_exit(99);
}

long test_worker_status(uintptr_t argument) {
    return 100 + (long)((argument & ~TEST_EXEC_ARGUMENT) - 2);
}

void test_factory_create(TestTaskFactory* factory, TestTaskBatch* batch,
                         unsigned long round) {
    batch->round = round;
    for (unsigned int i = 0; i < TEST_WORKERS; ++i) {
        const uintptr_t argument = 2 + round * TEST_WORKERS + i;
        long id;
        if ((round & 1UL) == 0) {
            id = user_spawn(argument);
        } else {
            volatile uintptr_t snapshot_cookie = argument ^ 0x123456UL;
            id = user_fork();
            if (id == 0) {
                if (snapshot_cookie != (argument ^ 0x123456UL)) {
                    stress_fail("fork stack snapshot");
                }
                user_exit(stress_worker(argument, 1));
            }
        }
        if (id < 0 || (factory->last_id >= 0 && id != factory->last_id + 1)) {
            stress_fail("create or monotonic ID");
        }
        batch->ids[i] = id;
        factory->last_id = id;
    }
    /* A real fifth USER occupies the last slot; DEAD is still occupied. */
    if (user_putchar(0x1c) != 0 || user_spawn(999999) != -1) {
        stress_fail("full table rejection");
    }
}

void test_factory_collect(const TestTaskBatch* batch) {
    unsigned int seen = 0;
    while (seen != (1U << TEST_WORKERS) - 1U) {
        TaskExitResult result = {0};
        const long id = user_waitpid(-1, &result);
        if (id == -2) {
            if (user_yield() != 0) {
                stress_fail("waiting yield");
            }
            continue;
        }
        unsigned int index = 0;
        while (index < TEST_WORKERS && batch->ids[index] != id) {
            ++index;
        }
        if (index == TEST_WORKERS || (seen & (1U << index)) != 0) {
            stress_fail("matched child identity");
        }
        if ((batch->round % 4) == 0 && index == 2) {
            if (result.reason != TASK_RESULT_FAULT || result.status != 0 ||
                result.fault_scause != 13) {
                stress_fail("fault result");
            }
        } else if (result.reason != TASK_RESULT_REQUESTED ||
                   result.status != test_worker_status(2 + batch->round * TEST_WORKERS + index) ||
                   result.fault_scause != 0) {
            stress_fail("exit result");
        }
        seen |= 1U << index;
    }
    TaskExitResult result = {0};
    if (user_waitpid(-1, &result) != -1 ||
        user_waitpid(batch->ids[0], &result) != -1) {
        stress_fail("old child still collectible");
    }
}
