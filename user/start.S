#include "syscall.h"
.section .text.entry, "ax", @progbits
.option norelax
.global _start

_start:
    call main
    call user_exit

1:
    J 1b

.section .note.GNU-stack, "", @progbits