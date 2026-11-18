#include "user_api.h"

#include <stdbool.h>

static const char goodbye[] = "[shell] bye\n";
static const char error_message[] = "[console] I/O failed\n";

static int write_line(int fd, const char* message) {
    if (message == NULL) {
        return -1;
    }

    size_t length = 0;
    while (message[length] != '\0') {
        if (length == SYSCALL_WRITE_MAX) {
            return -1;
        }
        ++length;
    }

    const long written = user_write_fd(fd, message, length);
    return written == (long)length ? 0 : -1;
}

int main(unsigned long argument) {
    (void)argument;
    int status = 1;

    static char path[127];
    bool skip_lf = false;

    for (;;) {
        size_t length = 0;
        bool discard_line = false;
        if(write_line(TINYOS_STDOUT_FILENO, "tinyos> ") != 0){
            goto io_error;
        }

        for (;;) {
            char byte;
            long len = user_read(TINYOS_STDIN_FILENO, &byte, 1);
            //逐字节读取

            if (len == SYSCALL_ERR_WOULD_BLOCK) {
                if (user_yield() != 0) {
                    goto io_error;
                }

                continue;
            }

            if (len != 1) {
                goto io_error;
            }

            if (skip_lf) { //不换行
                skip_lf = false;
                if (byte == '\n') {
                    continue;
                }
            }

            if (byte == '\r' ||
                byte == '\n') { // CR LF都结束一行
                if (byte == '\r') {
                    skip_lf = true; //如果时回车但不换行
                }
                if (user_write_fd(TINYOS_STDOUT_FILENO, "\n", 1)
                    != 1) {
                    goto io_error;
                }
                break;
            }

            if (discard_line) {
                continue;
            }

            if (byte < 0x21 || byte > 0x7e ||
                length >= sizeof(path)) {
                discard_line = true;
                continue;
            }

            path[length++] = byte;
            if (user_write_fd(TINYOS_STDOUT_FILENO, &byte, 1) != 1) {
                goto io_error;
            }
        }

        if (discard_line) {
            (void)write_line(TINYOS_STDERR_FILENO, "[shell] invalid line\n");
            continue;
        }

        if (discard_line) {
            (void)write_line(TINYOS_STDERR_FILENO,
                "[shell] invalid line\n");
            continue;
        }
        if (length == 0) {
            continue;
        }

        if (length == 4 && path[0] == 'e' && path[1] == 'x'
            && path[2] == 'i' && path[3] == 't') {
            if (user_write_fd(TINYOS_STDOUT_FILENO,
                goodbye, sizeof(goodbye) - 1) !=
                sizeof(goodbye) - 1) {
                goto io_error;
            }
            return 0;
        }

        long pid = user_fork();
        if (pid < 0) {
            (void)write_line(TINYOS_STDERR_FILENO,"[shell] fork failed\n");
            continue;
            
        }

        if (pid == 0) {
            (void)user_exec_path(path, length, 0);
            
            (void)write_line(TINYOS_STDERR_FILENO,
                    "[shell] exec failed\n");
            user_exit(127);
           
        }

        TaskExitResult result = {0};
        for (;;) {
            long waitpid = user_waitpid(pid, &result);

            if (waitpid == SYSCALL_ERR_WOULD_BLOCK && user_yield() == 0) {
                continue;
            }

            if (waitpid == pid) break;

            (void)write_line(TINYOS_STDERR_FILENO, "[shell] wait failed");
            // -1：报一次错，然后 yield 循环等（reaper 不能丢孩子）
            for (;;) {
                (void)user_yield();
            }
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
