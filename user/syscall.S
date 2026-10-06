#include "syscall.h"
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

.section .note.GNU-stack, "", @progbits
