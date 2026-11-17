#ifndef TINYOS_USER_API_H
#define TINYOS_USER_API_H
#include <stddef.h>
#include <stdint.h>
#include "fs/standard_stream.h"
#include "task/syscall.h"
#include "task/task_exit_result.h"


#ifdef __cplusplus
extern "C" {
#endif

long user_write(const void* buffer, size_t length);
long user_putchar(unsigned char character);
long user_yield(void);
long user_sleep(unsigned long ticks);
long user_get_time(void* tick);
long user_reap(int task_id, TaskExitResult* result);
long user_spawn(uintptr_t user_argument);
long user_fork(void);
long user_exec(uintptr_t user_argument);
long user_waitpid(long requested_id, TaskExitResult* result);
long user_spawn_program(uint64_t program_id, uintptr_t argument);
long user_exec_program(uint64_t program_id, uintptr_t argument);
long user_read_file(
    const char* path, size_t path_length, uint64_t offset,
    void* destination, size_t length);
//program_id 选择只读 ELF 描述，0和1合法，不是任务身份
//argument 传给新程序入口，保存到 fresh frame 的 a0；0也合法
long user_open(const char* path, size_t path_length);
long user_read(long fd, void* destination, size_t length);
long user_close(long fd);
long user_write_fd(long fd, const void *source, size_t length);
long user_exec_path(const char* path, size_t path_length, uintptr_t argument);
#ifdef __cplusplus
[[noreturn]] void user_exit(long status);
#else
_Noreturn void user_exit(long status);
#endif

#ifdef __cplusplus
}
#endif

#endif
