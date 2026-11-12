#include "task_factory.h"

static volatile unsigned long counter = 7;
static volatile unsigned char buffer[4096 + 32];
static volatile char data_marker = 'd';

static void check_fresh(void) {
    if (counter != 7 || data_marker != 'd') {
        stress_fail("fresh data");
    }
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != 0) {
            stress_fail("fresh BSS");
        }
    }
}

long stress_worker(uintptr_t argument, int inherited) {
    const uintptr_t original = argument & ~TEST_EXEC_ARGUMENT;
    const unsigned long round = (original - 2) / TEST_WORKERS;
    const unsigned int index = (unsigned int)((original - 2) % TEST_WORKERS);
    if (inherited) {
        if (counter != 42 || data_marker != 'R' || buffer[0] != 0x5a ||
            buffer[sizeof(buffer) - 1] != 0x5a) {
            stress_fail("fork data snapshot");
        }
    } else {
        check_fresh();
    }
    const unsigned char pattern = (unsigned char)(original % 251 + 1);
    counter = original + 1000;
    data_marker = 'W';
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        buffer[i] = pattern;
    }
    volatile uintptr_t stack_cookie = original ^ 0xabcdefUL;
    if (user_yield() != 0 || counter != original + 1000 || data_marker != 'W' ||
        stack_cookie != (original ^ 0xabcdefUL)) {
        stress_fail("worker yield snapshot");
    }
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != pattern) {
            stress_fail("private buffer");
        }
    }
    if ((argument & TEST_EXEC_ARGUMENT) == 0 && (round % 4) == 0) {
        if (index == 1) {
            (void)user_exec(original | TEST_EXEC_ARGUMENT);
            stress_fail("exec returned");
        }
        if (index == 2) {
            __asm__ volatile("ld zero, 0(zero)" ::: "memory");
            stress_fail("fault returned");
        }
    }
    return test_worker_status(original);
}

int main(unsigned long argument) {
    if (argument >= 2) {
        return (int)stress_worker(argument, 0);
    }
    check_fresh();
    if (argument == 0) {
        return 0;
    }
    TaskExitResult initial = {0};
    long id;
    do {
        id = user_waitpid(-1, &initial);
        if (id == -2 && user_yield() != 0) {
            stress_fail("bootstrap yield");
        }
    } while (id == -2);
    if (id < 0 || initial.reason != TASK_RESULT_REQUESTED || initial.status != 0) {
        stress_fail("bootstrap child");
    }
    counter = 42;
    data_marker = 'R';
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        buffer[i] = 0x5a;
    }
    if (user_putchar(0x1d) != 0) {
        stress_fail("baseline checkpoint");
    }
    TestTaskFactory factory = {.last_id = -1};
    for (unsigned long round = 0; round < TEST_STRESS_ROUNDS; ++round) {
        TestTaskBatch batch = {0};
        test_factory_create(&factory, &batch, round);
        test_factory_collect(&batch);
        if (counter != 42 || data_marker != 'R') {
            stress_fail("parent data isolation");
        }
        for (size_t i = 0; i < sizeof(buffer); ++i) {
            if (buffer[i] != 0x5a) {
                stress_fail("parent buffer isolation");
            }
        }
        if (user_putchar(0x1e) != 0) {
            stress_fail("round checkpoint");
        }
    }
    if (user_putchar(0x1f) != 0) {
        stress_fail("final checkpoint");
    }
    return 0;
}
