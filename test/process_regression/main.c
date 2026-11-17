
#include "user_api.h"
#include <stdint.h>
#include "task/builtin_program.h"

static volatile unsigned long counter = 7;
static volatile unsigned char buffer[4096 + 32];
static char data_message[] = "data OK\n";

static void fail(const char* reason) {
    size_t length = 0;
    while (reason[length] != '\0') {
        ++length;
    };
    /* 即使诊断输出失败，也必须退出。 */
    user_write(reason, length);
    user_exit(1); /* 显式触发 EXIT 系统调用退出 */
}

static long wait_requested(long expected_status) {
    TaskExitResult result = {0};

    for (;;) {
        const long matched_id = user_waitpid(-1, &result);

        if (matched_id == -2) {
            if (user_yield() != 0) {
                fail("yield while waiting failed\n");
            }
            continue;
        }

        if (matched_id < 0) {
            fail("waitpid failed\n");
        }

        if (result.reason != TASK_RESULT_REQUESTED ||
            result.status != expected_status ||
            result.fault_scause != 0) {
            fail("unexpected child exit result\n");
        }

        return matched_id;
    }
}

int main(unsigned long user_tag) {

    if (counter != 7 || data_message[0] != 'd') {
        fail("U2 initial data mismatch\n");
    }

    unsigned long expected_counter = 100UL + user_tag * 100UL;
    counter = expected_counter;
    data_message[0] = 'D';

    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != 0) {
            fail("buffer is not initially zero\n");
        }
        buffer[i] = (unsigned char)(i + 1);
    }

        if (user_tag == 2 || user_tag == 3) {
        static const char spawned[] = "[U2] fresh image OK\n";
        static const char executed[] = "[exec] fresh image OK\n";
        const char* text = user_tag == 2 ? spawned : executed;
        const size_t length = user_tag == 2 ? sizeof(spawned) - 1 : sizeof(executed) - 1;
        if (user_write(text, length) != (long)length || user_yield() != 0) {
            fail("fresh demo write/yield failed\n");
        }
        return user_tag == 2 ? 23 : 37;
    }

    if (user_tag > 1) {
        fail("usar tag > 1");
    }

    char start_message[] = "[U0] start\n";
    start_message[2] = (char)('0' + user_tag);

    if (user_write(start_message, sizeof(start_message) - 1) !=
        (long)(sizeof(start_message) - 1)) {
        fail("write start message failed\n");
    }

    static const char message[] = "Hello from user!\n";

    /* TODO: yield -> putchar('R') -> sleep(2) -> putchar('X').
       Check each return value is 0. */
    for (size_t round = 0; round < 3; ++round) {
        ++counter;
        ++expected_counter;
        if (user_yield() != 0) {
            fail("yield failed\n");
        }

        if (counter != expected_counter) {
            fail("counter mismatch after yield\n");
        }

        char counter_message[] = "[U0] counter OK\n";
        counter_message[2] = (char)('0' + user_tag);

        if (user_write(counter_message, sizeof(counter_message) - 1) !=
            (long)(sizeof(counter_message) - 1)) {
            fail("write counter message failed\n");
        }
    }
        if (user_putchar('R') != 0) {
            fail("putchar R failed\n");
        }

        uint64_t ticks = 0;
        if (user_get_time(&ticks) != 0) {
            fail("get_time failed before sleep\n");
        }

        if (user_sleep(2) != 0) {
            fail("sleep failed\n");
        }

        uint64_t later_ticks = 0;
        if (user_get_time(&later_ticks) != 0 || later_ticks <= ticks) {
            fail("get_time last_tick failed before sleep\n");
        }

        if (user_putchar('X') != 0) {
            fail("putchar X failed \n");
        }

        if (counter != expected_counter) {
            fail("counter mismatch after sleep\n");
        }

    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != (unsigned char)(i + 1)) {
            fail("buffer fill failed\n");
        }
    }

    static const char time_message[] = "Time OK\n";
    if (user_write(time_message, sizeof(time_message) - 1) !=
        (long)(sizeof(time_message) - 1)) {
        fail("write time_message failed\n");
    }

    long written = user_write(message, sizeof(message) - 1);
    long length = (long)(sizeof(message) - 1);

    if (written != length) {
        fail("written not qeual to length\n");
    }
    /* TODO: compare written with (long)(sizeof(message) - 1). */

    written = user_write(data_message, sizeof(data_message) - 1);
    length = sizeof(data_message) - 1;
    if (written != length) {
        fail("written not qeual to length\n");
    }

    const void* invalid = (const void*)(uintptr_t)0x40002000;
    /* TODO: WRITE(invalid, 1) returns -1; WRITE(invalid, 0) returns 0. */

    if (user_write(invalid, 0) != 0) {
        fail("zero-length write failed\n");
    }
    if (user_write(invalid, 1) != -1) {
        fail("invalid-address write was not rejected\n");
    }
    /* TODO: any mismatch prints "[write] failed\n" before returning 1.
       EXIT currently ignores main's return value. */


    if (user_tag == 1) {
        (void)wait_requested(0);
        static const char reap_message[] = "[U1] reaped U0\n";

        if (user_write(reap_message, sizeof(reap_message) - 1) !=
            (long)(sizeof(reap_message) - 1)) {
            fail("write reap message failed\n");
        }

        if (user_spawn_program(UINT64_MAX, 0) != -1 ||
            user_exec_program(UINT64_MAX, 0) != -1) {
            fail("invalid program ID accepted\n");
        }

        const long spawned_id =
            user_spawn_program(USER_PROGRAM_WORKER, 3);
        if (spawned_id < 0) {
            fail("spawn U2 failed\n");
        }

        static const char spawn_message[] = "[U1] spawned U2\n";
        if (user_write(spawn_message, sizeof(spawn_message) - 1) !=
            (long)(sizeof(spawn_message) - 1)) {
            fail("write spawn message failed\n");
        }


        if (wait_requested(23) != spawned_id) {
            fail("unexpected spawned child ID\n");
        }

        // U2 修改、释放自己的空间后，U1 的数据仍应保持。
        if (counter != expected_counter || data_message[0] != 'D') {
            fail("U1 data changed after U2\n");
        }

        for (size_t i = 0; i < sizeof(buffer); ++i) {
            if (buffer[i] != (unsigned char)(i + 1)) {
                fail("U1 buffer changed after U2\n");
            }
        }

        static const char reaped_message[] = "[U1] reaped U2\n";
        if (user_write(reaped_message, sizeof(reaped_message) - 1) !=
            (long)(sizeof(reaped_message) - 1)) {
            fail("write U2 reap message failed\n");
        }

        volatile unsigned long fork_local = 1234UL;
        const long forked_id = user_fork();
        if (forked_id < 0) {
            fail("fork failed\n");
        }

        if (forked_id == 0) {
            if (counter != expected_counter ||
                fork_local != 1234UL ||
                buffer[0] != 1 ||
                data_message[0] != 'D') {
                fail("fork snapshot mismatch\n");
            }

            counter = 999UL;
            fork_local = 5678UL;
            buffer[0] = 0xee;

            if (user_yield() != 0 ||
                counter != 999UL ||
                fork_local != 5678UL ||
                buffer[0] != 0xee) {
                fail("fork child yield/data failed\n");
            }

            static const char fork_message[] = "[fork child] snapshot OK\n";
            if (user_write(fork_message, sizeof(fork_message) - 1) !=
                (long)sizeof(fork_message) - 1) {
                    fail("write fork result failed\n");
            }

            user_exec_program(USER_PROGRAM_WORKER, 17);
            fail("exec returned\n");
        }

        if (wait_requested(37) != forked_id) {
            fail("unexpected fork child ID\n");
        }

        if (counter != expected_counter ||
            fork_local != 1234UL ||
            buffer[0] != 1 ||
            data_message[0] != 'D') {
            fail("fork U1 failed");
        }

        const char forked_message[] = "[U1] fork isolation OK\n";

        if (user_write(forked_message, sizeof(forked_message) - 1) !=
            (long)sizeof(forked_message) - 1) {
            fail("write fork result failed\n");
        }

    }


    char end_message[] = "[U0] end\n";
    end_message[2] = (char)('0' + user_tag);

    if (user_write(end_message, sizeof(end_message) - 1) !=
        (long)(sizeof(end_message) - 1)) {
        fail("write end message failed\n");
    }

    return 0;
}
