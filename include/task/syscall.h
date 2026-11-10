#ifndef TINYOS_SYSCALL_H
#define TINYOS_SYSCALL_H

#define SYSCALL_PUTCHAR     1
#define SYSCALL_YIELD       2
#define SYSCALL_EXIT        3
#define SYSCALL_SLEEP       4
#define SYSCALL_WRITE       5
#define SYSCALL_GET_TIME    6
#define SYSCALL_REAP        7
#define SYSCALL_SPAWN       8
#define SYSCALL_FORK        9
#define SYSCALL_EXEC        10
#define SYSCALL_WAITPID     11
#define SYSCALL_WRITE_MAX   256


#ifndef __ASSEMBLER__
struct trap_frame;

#ifdef __cplusplus
extern "C" {
#endif

struct trap_frame* syscall_handle(
    struct trap_frame *frame
);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLER__ */

#endif /* TINYOS_SYSCALL_H */
