#ifndef TINYOS_SYSCALL_H
#define TINYOS_SYSCALL_H

#define SYSCALL_PUTCHAR 1
#ifndef __ASSEMBLER__
struct trap_frame;

void syscall_handle(
    struct trap_frame *frame
);

#endif /* __ASSEMBLER__ */

#endif /* TINYOS_SYSCALL_H */
