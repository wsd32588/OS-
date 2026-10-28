    .section .text
    .balign 4
    .global __alltraps
    .global __restore

__alltraps:
    # BATCH05 A：交换 sp/sscratch；在内核栈上预留 272 字节。
    csrrw sp, sscratch, sp
    addi sp, sp, -272
    # 机械地保存寄存器：x2 通过 sscratch 单独保存。
    sd x0,   0(sp)
    sd x1,   8(sp)
    sd x3,  24(sp)
    sd x4,  32(sp)
    sd x5,  40(sp)
    sd x6,  48(sp)
    sd x7,  56(sp)
    sd x8,  64(sp)
    sd x9,  72(sp)
    sd x10, 80(sp)
    sd x11, 88(sp)
    sd x12, 96(sp)
    sd x13,104(sp)
    sd x14,112(sp)
    sd x15,120(sp)
    sd x16,128(sp)
    sd x17,136(sp)
    sd x18,144(sp)
    sd x19,152(sp)
    sd x20,160(sp)
    sd x21,168(sp)
    sd x22,176(sp)
    sd x23,184(sp)
    sd x24,192(sp)
    sd x25,200(sp)
    sd x26,208(sp)
    sd x27,216(sp)
    sd x28,224(sp)
    sd x29,232(sp)
    sd x30,240(sp)
    sd x31,248(sp)

    # BATCH05 B：sstatus 存到 256、sepc 存到 264、用户 sp 存到 16。
    # 然后把现场地址放进 a0，调用 trap_handler。
    csrr t0, sstatus
    sd t0, 256(sp)
    csrr t1, sepc
    sd t1, 264(sp)

    csrr t2, sscratch
    sd t2, 16(sp)

    mv a0, sp
    call trap_handler

__restore:
    # BATCH05 C：把 a0 里的现场指针当作 sp；恢复 sstatus/sepc；
    # 在恢复 t0/t1/t2 之前，把保存的用户 sp 写进 sscratch。
    mv sp, a0

    ld t0, 256(sp)
    ld t1, 264(sp)
    ld t2, 16(sp)

    csrw sstatus, t0
    csrw sepc, t1
    csrw sscratch, t2

    ld x1,   8(sp)
    ld x3,  24(sp)
    ld x4,  32(sp)
    ld x5,  40(sp)
    ld x6,  48(sp)
    ld x7,  56(sp)
    ld x8,  64(sp)
    ld x9,  72(sp)
    ld x10, 80(sp)
    ld x11, 88(sp)
    ld x12, 96(sp)
    ld x13,104(sp)
    ld x14,112(sp)
    ld x15,120(sp)
    ld x16,128(sp)
    ld x17,136(sp)
    ld x18,144(sp)
    ld x19,152(sp)
    ld x20,160(sp)
    ld x21,168(sp)
    ld x22,176(sp)
    ld x23,184(sp)
    ld x24,192(sp)
    ld x25,200(sp)
    ld x26,208(sp)
    ld x27,216(sp)
    ld x28,224(sp)
    ld x29,232(sp)
    ld x30,240(sp)
    ld x31,248(sp)

    # BATCH05 D：释放 272 字节，交换 sp/sscratch，然后 sret。
    addi sp, sp, 272
    csrrw sp, sscratch, sp
    sret
