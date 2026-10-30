#include "user_api.h"
#include <stdint.h>

static volatile unsigned long counter = 7;
static volatile unsigned char buffer[4096 + 32];
static char data_message[] = "data OK\n";

static void fail(void) {
    static const char fail_msg[] = "[write] failed\n";
    user_write(fail_msg, sizeof(fail_msg) - 1);
    user_exit(); /* 显式触发 EXIT 系统调用退出 */
}


int main(void) {

    if (counter != 7) {
        fail();
    }
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != 0) {
            fail();
        }
        buffer[i] = (unsigned char)(i + 1);
    }
    counter = 8;
    data_message[0] = 'D';

    static const char message[] = "Hello from user!\n";

    /* TODO: yield -> putchar('R') -> sleep(2) -> putchar('X').
       Check each return value is 0. */

    if (user_yield() != 0) {
        fail();
    }

    if (user_putchar('R') != 0) {
        fail();
    }

    if (user_sleep(2) != 0) {
        fail();
    }

    if (user_putchar('X') != 0) {
        fail();
    }

    if (counter != 8) {
        fail();
    }

    for (size_t i = 0; i < sizeof(buffer); ++i) {
        if (buffer[i] != (unsigned char)(i + 1)) {
            fail();
        }
    }

    long written = user_write(message, sizeof(message) - 1);
    long length = (long)(sizeof(message) - 1);

    if (written != length) {
        fail();
    }
    /* TODO: compare written with (long)(sizeof(message) - 1). */

    written = user_write(data_message, sizeof(data_message) - 1);
    length = sizeof(data_message) - 1;
    if (written != length) {
        fail();
    }

    const void* invalid = (const void*)(uintptr_t)0x40002000;
    /* TODO: WRITE(invalid, 1) returns -1; WRITE(invalid, 0) returns 0. */

    if (user_write(invalid, 0) != 0) {
        fail();
    }
    if (user_write(invalid, 1) != -1) {
        fail();
    }
    /* TODO: any mismatch prints "[write] failed\n" before returning 1.
       EXIT currently ignores main's return value. */
    return 0;
}
