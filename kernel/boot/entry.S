.section .text.entry
.global _start

_start:
    # 此时尚未使用栈，用字节清零，避免对齐和尾部问题
    la t0, __bss_start
    la t1, __bss_end

.Lclear_bss:
    bgeu t0, t1, .Lbss_ready
    sb zero, 0(t0)
    addi t0, t0, 1
    j .Lclear_bss

.Lbss_ready:
    la sp, boot_stack_top

    # a0/a1 是调用者保存寄存器，构造调用可能改写它们
    addi sp, sp, -16
    sd a0, 0(sp)
    sd a1, 8(sp)

    call cpp_runtime_init
    bnez a0, halt

    ld a0, 0(sp)
    ld a1, 8(sp)
    addi sp, sp, 16

    call kernel_main

    # 仅在内核 main 受控返回后执行全局析构
    li a0, 0
    call __cxa_finalize

halt:
    j halt

.section .bss.stack
.align 4

boot_stack_bottom:
    .space 4096 * 4

.global boot_stack_top
boot_stack_top:
