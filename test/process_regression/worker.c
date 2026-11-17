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

    const char path[] = "/etc/about";
    const char expected[] = "TinyOS builtin fs\n";
    unsigned char file_buffer[18] = {0};

    long count = user_read_file(
        path, sizeof(path) - 1,
        0, file_buffer, 7);
    if (count != 7) {
        fail("read about first part failed\n");
    }


    count = user_read_file(
        path, sizeof(path) - 1, 7,
        file_buffer + 7, 11);
    if (count != 11) {
        fail("read about second part failed\n");
    }

    for (size_t i = 0; i < sizeof(file_buffer); ++i) {
        if (file_buffer[i] != (unsigned char)expected[i]) {
            fail("about content mistach\n");
        }
    }

    const char message[] = "[worker] file read OK\n";
    if (user_write(message, sizeof(message) - 1) !=
        (long)(sizeof(message) - 1)) {
        fail("write file read message failed\n");
    }

    const long first_fd = user_open(path, sizeof(path) - 1);
    if (first_fd < 0) {
        fail("open first fd failed\n");
    }

    const long second_fd = user_open(path, sizeof(path) - 1);
    if (second_fd < 0 || second_fd == first_fd) {
        fail("open second fd failed\n");
    }

    /* 同一次打开，第二次 read 从上次的位置继续。 */
    if (user_read(first_fd, file_buffer, 7) != 7 ||
        user_read(first_fd, file_buffer + 7, 11) != 11) {
        fail("fd sequential read failed\n");
    }

    for (size_t i = 0; i < sizeof(file_buffer); ++i) {
        if (file_buffer[i] != (unsigned char)expected[i]) {
            fail("fd content mismatch\n");
        }
    }

    /* 独立打开的位置仍然是文件开头。 */
    if (user_read(second_fd, file_buffer, 7) != 7) {
        fail("independent fd read failed\n");
    }

    for (size_t i = 0; i < 7; ++i) {
        if (file_buffer[i] != (unsigned char)expected[i]) {
            fail("independent fd content mismatch\n");
        }
    }

    if (user_close(second_fd) != 0) {
        fail("close second fd failed\n");
    }

    /* first_fd 留给进程退出路径自动关闭。 */
    const char fd_message[] = "[worker] fd read OK\n";
    if (user_write(fd_message, sizeof(fd_message) - 1) !=
        (long)(sizeof(fd_message) - 1)) {
        fail("write fd message failed\n");
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