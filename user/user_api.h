#ifndef TINYOS_USER_API_H
#define TINYOS_USER_API_H
#include <stddef.h>
#include <stdint.h>
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
//program_id 选择只读 ELF 描述，0和1合法，不是任务身份
//argument 传给新程序入口，保存到 fresh frame 的 a0；0也合法
#ifdef __cplusplus
[[noreturn]] void user_exit(long status);
#else
_Noreturn void user_exit(long status);
#endif

#ifdef __cplusplus
}
#endif

#endif
