#include "user_api.h"

#include <stdbool.h>

/*
 * 最小用户态 shell。
 *
 * 一条命令的生命周期：
 *   提示符 -> 逐字节读一行 -> 空行 / exit / 路径 -> fork + exec + wait -> 回到提示符。
 *
 * 两个跨命令保持的状态：
 *   - skip_lf：CR 结束一行后，紧跟的 LF 可能在子进程退出后才被读到，
 *     所以不能在每个命令开始时重置；
 *   - line_buffer[]：static，避免把 127 字节放在栈上，也保证 fork 后子进程
 *     能用自己的副本继续使用这个缓冲区。
 *
 * 输入规则（B30）：只接受可见非空白 ASCII；空格/控制字符/第 128 字节
 * 会把整行标记为 discard，但仍要读到行尾，否则残留字节会变成下一条命令。
 */

static const char goodbye[] = "[shell] bye\n";
static const char error_message[] = "[console] I/O failed\n";



/* 把以 NUL 结尾的固定消息写到 fd。
 * 长度受 SYSCALL_WRITE_MAX 限制；返回 0 表示整段写成功，-1 表示长度非法或短写。 */
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

    static char line_buffer[128];
    bool skip_lf = false;

    for (;;) {
        // 每条新命令：长度和丢弃状态重置，skip_lf 保持不变。
        size_t length = 0;
        bool discard_line = false;
        if(write_line(TINYOS_STDOUT_FILENO, "tinyos> ") != 0){
            goto io_error;
        }
        size_t path_length = 0;

        // 内层循环只负责把一行字节读进 line_buffer[]，不执行命令。
        for (;;) {
            char byte;
            long len = user_read(TINYOS_STDIN_FILENO, &byte, 1);

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

            if (byte < 0x20 || byte > 0x7e ||
                length >= sizeof(line_buffer) - 1) {
                discard_line = true;
                continue;
            }

            line_buffer[length++] = byte;
            if (user_write_fd(TINYOS_STDOUT_FILENO, &byte, 1) != 1) {
                goto io_error;
            }
        }

        if (discard_line) {
            (void)write_line(TINYOS_STDERR_FILENO, "[shell] invalid line\n");
            continue;
        }

        if (length == 0) {
            continue;
        }

        line_buffer[length] = '\0';
        
        const char* command_argv[6] = {0}; // 每读完一行，重新建立词表
        size_t command_argc = 0;
        size_t cursor = 0;
        while (cursor < length) {
            while (cursor < length && line_buffer[cursor] == ' ') {
                ++cursor;
            }
            if (cursor == length) {
                break;
            }
            if (command_argc == 6) {
                discard_line = true;
                break;
            }

            command_argv[command_argc] = &line_buffer[cursor];

            size_t len = 0;
            while (cursor < length && line_buffer[cursor] != ' ') {
                ++cursor;
                ++len;
            }
            if (len > 63) {
                discard_line = true;
                break;
            }

            if (command_argc == 0) {
                path_length = len;
            }
            ++command_argc;

            if (cursor < length) {
                line_buffer[cursor++] = '\0';
            }
        }

        if (discard_line) {
            (void)write_line(TINYOS_STDERR_FILENO, "[shell] invalid line\n");
            continue;
        }
        if (command_argc == 0) {
            continue;
        }

        const char* target_path = NULL;
        size_t target_length = 0;
        for (size_t i = 0; i < command_argc; ++i) {
            if (command_argv[i][0] != '<' || command_argv[i][1] != '\0') {
                continue;
            }
            if (i == 0 || i + 2 != command_argc) {
                discard_line = true;
                break;
            }

            const char* input_path = command_argv[i + 1];
            if (input_path[0] == '<' && input_path[1] == '\0') {
                discard_line = true;
                break;
            }

            target_path = input_path;
            while (target_path[target_length] != '\0') {
                ++target_length;
            }
            command_argc = i;
            break;
        }

        if (discard_line || command_argc > 4) {
            (void)write_line(TINYOS_STDERR_FILENO, "[shell] invalid line\n");
            continue;
        }

        if (command_argv[0][0] == 'e' &&
            command_argv[0][1] == 'x' &&
            command_argv[0][2] == 'i' &&
            command_argv[0][3] == 't' &&
            path_length ==4) {

            if (command_argc != 1 || target_path != NULL) {
                if (write_line(TINYOS_STDERR_FILENO, "[shell] invalid line\n") != 0) {
                    goto io_error;
                }
                continue;
            }
            if (user_write_fd(TINYOS_STDOUT_FILENO,
                goodbye, sizeof(goodbye) - 1) !=
                sizeof(goodbye) - 1) {
                goto io_error;
            }
            return 0;
        }

        // 子进程执行解析后的命令，父进程等待对应子任务。
        long pid = user_fork();
        if (pid < 0) {
            (void)write_line(TINYOS_STDERR_FILENO,"[shell] fork failed\n");
            continue;    
        }

        if (pid == 0) {
            if (target_path != NULL) {
                long target_fd = user_open(target_path, target_length);
                if (target_fd < 0) {
                    (void)write_line(TINYOS_STDERR_FILENO, "[shell] input redirect failed\n");
                    user_exit(126);
                }

                if (user_dup2(target_fd, TINYOS_STDIN_FILENO) != 0) {
                    (void)write_line(TINYOS_STDERR_FILENO, "[shell] input redirect failed\n");
                    user_exit(126);
                }
                if (target_fd != TINYOS_STDIN_FILENO && user_close(target_fd) != 0) {
                    (void)write_line(TINYOS_STDERR_FILENO, "[shell] input redirect failed\n");
                    user_exit(126);
                }
            }
            (void)user_exec_path_args(
                command_argv[0], path_length, command_argv, command_argc);
            (void)write_line(TINYOS_STDERR_FILENO, "[shell] exec failed\n");
            user_exit(127);
        }

        // 等待期间不读 stdin；-2 时让出 CPU 后继续等同一个 child id。
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

    return 0;

io_error:
    // 错误诊断尽力输出，失败时不再递归报错。
    (void)user_write_fd(
        TINYOS_STDERR_FILENO,
        error_message,
        sizeof(error_message) - 1);
    return 1;
}
