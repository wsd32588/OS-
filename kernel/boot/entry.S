.section .text.entry
.global _start

_start:
    la sp, boot_stack_top

    call kernel_main

halt:
    j halt

.section .bss.stack
.align 4

boot_stack_bottom:
    .space 4096 * 4

.global boot_stack_top
boot_stack_top: