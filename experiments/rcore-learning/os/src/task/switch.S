    .section .text
    .balign 4
    .global __switch
    .global __task_bootstrap

__switch:
    # a0：可写的当前 TaskContext；a1：已初始化的下一个 TaskContext。
    # BATCH07 A：通过 a0 把当前 ra 存到偏移 0、sp 存到偏移 8。

    sd ra,  0(a0)
    sd sp,  8(a0)
    # 机械地保存 callee-saved 寄存器槽位。
    sd s0,  16(a0)
    sd s1,  24(a0)
    sd s2,  32(a0)
    sd s3,  40(a0)
    sd s4,  48(a0)
    sd s5,  56(a0)
    sd s6,  64(a0)
    sd s7,  72(a0)
    sd s8,  80(a0)
    sd s9,  88(a0)
    sd s10, 96(a0)
    sd s11,104(a0)

    # BATCH07 B：通过 a1 从偏移 0 加载下一个 ra。
    ld ra,  0(a1)

    ld s0,  16(a1)
    ld s1,  24(a1)
    ld s2,  32(a1)
    ld s3,  40(a1)
    ld s4,  48(a1)
    ld s5,  56(a1)
    ld s6,  64(a1)
    ld s7,  72(a1)
    ld s8,  80(a1)
    ld s9,  88(a1)
    ld s10, 96(a1)
    ld s11,104(a1)

    # BATCH07 C：通过 a1 从偏移 8 加载下一个 sp，然后 ret。

    ld sp,  8(a1)
    ret
__task_bootstrap:
    # 第二章的 __restore 仍然从 a0 接收现场指针。
    mv a0, sp
    j __restore
