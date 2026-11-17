#include "user_api.h"

static char message[] = "[console] ready (q to quit)\n";
static const char goodbye[] = "[console] bye\n";
static const char error_message[] = "[console] I/O failed\n";

int main(unsigned long argument) {
    (void)argument;

    int status = 1;

    if (user_write_fd(TINYOS_STDOUT_FILENO, message, sizeof(message) - 1) !=
        (long)(sizeof(message) - 1)) {
        goto io_error;
    }
    for (;;) {
        unsigned char byte = 0;
        const long count = user_read(
            TINYOS_STDIN_FILENO, &byte, 1);

        if (count == SYSCALL_ERR_WOULD_BLOCK) {
            if (user_yield() != 0) {
                goto io_error;
            }
            continue;
        }

        if (count != 1) {
            goto io_error;
        }

        if (byte == 'q') {
            if (user_write_fd(TINYOS_STDOUT_FILENO, goodbye, sizeof(goodbye) - 1) !=
                (long)(sizeof(goodbye) - 1)) {
                goto io_error;
            }

            status = 0;
            break;
        }

        if (user_write_fd(TINYOS_STDOUT_FILENO, &byte, 1) != 1) {
            goto io_error;
        }
    }

    return status;

io_error:
    // 错误诊断尽力输出，失败时不再递归报错。
    (void)user_write_fd(
        TINYOS_STDERR_FILENO,
        error_message,
        sizeof(error_message) - 1);
    return 1;
}
