#include "syscall.h"

.section .rodata.user_image, "a", @progbits
.balign 4

.option push
.option norvc

.global user_image_start
.global user_image_end

user_image_start:

    addi a7, zero, SYSCALL_YIELD
    ecall

    addi a0, zero, 82
    addi a7, zero, SYSCALL_PUTCHAR
    ecall

    addi a0, zero, 2
    addi a7, zero, SYSCALL_SLEEP
    ecall

    addi a0, zero, 88
    addi a7, zero, SYSCALL_PUTCHAR
    ecall

    addi a7, zero, SYSCALL_EXIT
    ecall

1:
    addi t0, t0, 1
    j 1b

user_image_end:

.option pop
