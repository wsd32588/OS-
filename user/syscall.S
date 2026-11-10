#include "task/syscall.h"
.section .text, "ax", @progbits
.option norelax

.global user_write
user_write:
    li a7, SYSCALL_WRITE
    ecall
    ret

# TODO: user_putchar, user_yield, user_sleep follow the same ABI.
.global user_putchar
user_putchar:
    li a7, SYSCALL_PUTCHAR
    ecall
    ret

.global user_yield
user_yield:
    li a7, SYSCALL_YIELD
    ecall
    ret

.global user_sleep
user_sleep:
    li a7, SYSCALL_SLEEP
    ecall
    ret

.global user_exit
user_exit:
    li a7, SYSCALL_EXIT
    ecall
1:
    j 1b

.global user_get_time
user_get_time:
    li a7, SYSCALL_GET_TIME
    ecall
    ret

.global user_reap
user_reap:
    li a7, SYSCALL_REAP
    ecall
    ret

.global user_spawn
user_spawn:
    li a7, SYSCALL_SPAWN
    ecall
    ret

.global user_fork
user_fork:
    li a7, SYSCALL_FORK
    ecall
    ret

.global user_exec
user_exec:
    li a7, SYSCALL_EXEC
    ecall
    ret

.global user_waitpid
user_waitpid:
    li a7, SYSCALL_WAITPID
    ecall
    ret

.section .note.GNU-stack, "", @progbits
