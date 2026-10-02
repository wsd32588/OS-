#ifndef TINYOS_SYSCALL_H
#define TINYOS_SYSCALL_H

#define SYSCALL_PUTCHAR     1
#define SYSCALL_YIELD       2
#define SYSCALL_EXIT        3
#define SYSCALL_SLEEP       4

#ifndef __ASSEMBLER__
struct trap_frame;

struct trap_frame* syscall_handle(
    struct trap_frame *frame
);

#endif /* __ASSEMBLER__ */

#endif /* TINYOS_SYSCALL_H */
