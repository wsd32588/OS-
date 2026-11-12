#include "user_api.h"
#include <stdint.h>

static volatile unsigned long counter = 123;
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

int main (unsigned long argument) {
    if (counter != 123 || data_message[0] != 'd') {
        fail("initial data mismatch\n");
    }

    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != 0) {
            fail("buffer is not initially zero\n");
        }
        buffer[i] = (unsigned char)(i + 1);
    }

    if (argument > 100) {
        fail("argument bigger than 100");
    }

    unsigned long expected_counter = 100UL + argument * 100UL;
    counter = expected_counter;
    data_message[0] = 'D';

    volatile unsigned long local = argument;

    if (user_yield() != 0) {
        fail("U3 yield failed\n");
    }

    if (counter != expected_counter ||
        data_message[0] != 'D' ||
        local != argument) {
        fail("worker data/stack mismatch after yield\n");
    }

    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != (unsigned char)(i + 1)) {
            fail("worker buffer mismatch after yield\n");
        }
    }

    static const char _message[] = "[worker] fresh image OK\n";
    if (user_write(_message, sizeof(_message) - 1) !=
        (long)(sizeof(_message) - 1)) {
        fail("worker write failed\n");
    }

    return (int)argument + 20;

}