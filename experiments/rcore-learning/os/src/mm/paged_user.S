    .section .text, "ax"
    .balign 4
    .option push
    .option norelax
    .global __paged_enter
    .global __paged_return
__paged_enter:
    # a0=context high VA, a1=user token, a2=restore alias, a3=trap alias.
    # Save the interrupted boot Rust call's callee-saved state, including gp/tp.

    addi sp, sp, -128
    sd ra, 0(sp)
    sd gp, 8(sp)
    sd tp, 16(sp)
    sd s0, 24(sp)
    sd s1, 32(sp)
    sd s2, 40(sp)
    sd s3, 48(sp)
    sd s4, 56(sp)
    sd s5, 64(sp)
    sd s6, 72(sp)
    sd s7, 80(sp)
    sd s8, 88(sp)
    sd s9, 96(sp)
    sd s10, 104(sp)
    sd s11, 112(sp)
    sd sp, 280(a0)
    csrw sscratch, a0
    csrw stvec, a3
    jr a2

    .balign 4
__paged_return:
    # High trap code has restored kernel satp/sp and the low fatal vector.
    # t3 holds the satp read before switching away from the user root.
    mv a0, t3
    ld ra, 0(sp)
    ld gp, 8(sp)
    ld tp, 16(sp)
    ld s0, 24(sp)
    ld s1, 32(sp)
    ld s2, 40(sp)
    ld s3, 48(sp)
    ld s4, 56(sp)
    ld s5, 64(sp)
    ld s6, 72(sp)
    ld s7, 80(sp)
    ld s8, 88(sp)
    ld s9, 96(sp)
    ld s10, 104(sp)
    ld s11, 112(sp)
    addi sp, sp, 128
    ret

    .section .text.trampoline, "ax"
    .balign 4
    .global __paged_restore
    .global __paged_alltraps
__paged_restore:
    # BATCH19 B BEGIN
    # a0=TRAP_CONTEXT, a1=user token; both are still available here.
    # Switch satp to a1, sfence; set sp=a0; load saved sstatus/sepc
    # (256/264) and write their CSRs; set sscratch=a0.
    nop
    # BATCH19 B END

    csrw satp, a1
    sfence.vma

    mv sp, a0

    ld t0, 256(sp)
    ld t1, 264(sp)
    csrw sstatus, t0
    csrw sepc, t1

    csrw sscratch, sp

    ld x1, 8(sp)
    ld x3, 24(sp)
    ld x4, 32(sp)
    ld x5, 40(sp)
    ld x6, 48(sp)
    ld x7, 56(sp)
    ld x8, 64(sp)
    ld x9, 72(sp)
    ld x10, 80(sp)
    ld x11, 88(sp)
    ld x12, 96(sp)
    ld x13, 104(sp)
    ld x14, 112(sp)
    ld x15, 120(sp)
    ld x16, 128(sp)
    ld x17, 136(sp)
    ld x18, 144(sp)
    ld x19, 152(sp)
    ld x20, 160(sp)
    ld x21, 168(sp)
    ld x22, 176(sp)
    ld x23, 184(sp)
    ld x24, 192(sp)
    ld x25, 200(sp)
    ld x26, 208(sp)
    ld x27, 216(sp)
    ld x28, 224(sp)
    ld x29, 232(sp)
    ld x30, 240(sp)
    ld x31, 248(sp)
    ld sp, 16(sp)
    sret

    .balign 4
__paged_alltraps:
    # Hardware kept the user sp. sscratch names this task's supervisor page.
    csrrw sp, sscratch, sp
    sd zero, 0(sp)
    sd x1, 8(sp)
    sd x3, 24(sp)
    sd x4, 32(sp)
    sd x5, 40(sp)
    sd x6, 48(sp)
    sd x7, 56(sp)
    sd x8, 64(sp)
    sd x9, 72(sp)
    sd x10, 80(sp)
    sd x11, 88(sp)
    sd x12, 96(sp)
    sd x13, 104(sp)
    sd x14, 112(sp)
    sd x15, 120(sp)
    sd x16, 128(sp)
    sd x17, 136(sp)
    sd x18, 144(sp)
    sd x19, 152(sp)
    sd x20, 160(sp)
    sd x21, 168(sp)
    sd x22, 176(sp)
    sd x23, 184(sp)
    sd x24, 192(sp)
    sd x25, 200(sp)
    sd x26, 208(sp)
    sd x27, 216(sp)
    sd x28, 224(sp)
    sd x29, 232(sp)
    sd x30, 240(sp)
    sd x31, 248(sp)
    csrr t0, sscratch
    sd t0, 16(sp)
    csrr t0, sstatus
    sd t0, 256(sp)
    csrr t0, sepc
    sd t0, 264(sp)
    csrr t3, satp

    # BATCH19 C BEGIN
    # Read kernel_satp/kernel_sp/trap_handler at 272/280/288(sp)
    # into t0/t1/t2 BEFORE leaving this context mapping. Then switch satp,
    # sfence.vma zero, zero, and set sp=t1. t2 is the low return target.
    nop
    # BATCH19 C END
    ld t0, 272(sp)
    ld t1, 280(sp)
    ld t2, 288(sp)

    csrw satp, t0
    sfence.vma

    mv sp, t1

    # Infrastructure: after the kernel table is active, install a low fatal
    # vector before returning to Rust. Only this literal uses high PC-relative
    # addressing. The literal value itself is the absolute low address.
    lla t4, .Lpaged_low_fault
    ld t4, 0(t4)
    csrw stvec, t4
    csrr t5, sstatus
    andi t5, t5, 256
    bnez t5, .Lpaged_supervisor_fault
    jr t2
.Lpaged_supervisor_fault:
    jr t4
    .balign 8
.Lpaged_low_fault:
    .dword __paging_fault_entry
    .option pop
