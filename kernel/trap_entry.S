#include "trap_offsets.h"
.section .text.trap
.balign 4

.global trap_entry
trap_entry:
    #创建trap frame
    addi sp, sp, -TRAP_FRAME_SIZE

    #保存通用寄存器
    sd ra, TRAP_FRAME_RA_OFFSET(sp)
    sd gp, TRAP_FRAME_GP_OFFSET(sp)
    sd tp, TRAP_FRAME_TP_OFFSET(sp)

    sd t0, TRAP_FRAME_T0_OFFSET(sp)
    sd t1, TRAP_FRAME_T1_OFFSET(sp)
    sd t2, TRAP_FRAME_T2_OFFSET(sp)

    sd s0, TRAP_FRAME_S0_OFFSET(sp)
    sd s1, TRAP_FRAME_S1_OFFSET(sp)

    sd a0, TRAP_FRAME_A0_OFFSET(sp)
    sd a1, TRAP_FRAME_A1_OFFSET(sp)
    sd a2, TRAP_FRAME_A2_OFFSET(sp)
    sd a3, TRAP_FRAME_A3_OFFSET(sp)
    sd a4, TRAP_FRAME_A4_OFFSET(sp)
    sd a5, TRAP_FRAME_A5_OFFSET(sp)
    sd a6, TRAP_FRAME_A6_OFFSET(sp)
    sd a7, TRAP_FRAME_A7_OFFSET(sp)

    sd s2, TRAP_FRAME_S2_OFFSET(sp)
    sd s3, TRAP_FRAME_S3_OFFSET(sp)
    sd s4, TRAP_FRAME_S4_OFFSET(sp)
    sd s5, TRAP_FRAME_S5_OFFSET(sp)
    sd s6, TRAP_FRAME_S6_OFFSET(sp)
    sd s7, TRAP_FRAME_S7_OFFSET(sp)
    sd s8, TRAP_FRAME_S8_OFFSET(sp)
    sd s9, TRAP_FRAME_S9_OFFSET(sp)
    sd s10, TRAP_FRAME_S10_OFFSET(sp)
    sd s11, TRAP_FRAME_S11_OFFSET(sp)

    sd t3, TRAP_FRAME_T3_OFFSET(sp)
    sd t4, TRAP_FRAME_T4_OFFSET(sp)
    sd t5, TRAP_FRAME_T5_OFFSET(sp)
    sd t6, TRAP_FRAME_T6_OFFSET(sp)

    addi t0, sp, TRAP_FRAME_SIZE
    sd t0, TRAP_FRAME_SP_OFFSET(sp)

    #保存trap CSR
    csrr t0, sepc
    sd t0, TRAP_FRAME_SEPC_OFFSET(sp)

    csrr t0, sstatus
    sd t0, TRAP_FRAME_SSTATUS_OFFSET(sp)

    csrr t0, scause
    sd t0, TRAP_FRAME_SCAUSE_OFFSET(sp)

    csrr t0, stval
    sd t0, TRAP_FRAME_STVAL_OFFSET(sp)

    #C函数传入第一个参数 a0
    #传入 struct trap_frame *
    mv a0, sp
    call trap_handler

    mv sp, a0
    #------------若handler返回

    #恢复CSR
.global trap_restore
trap_restore:
    ld t0, TRAP_FRAME_SEPC_OFFSET(sp)
    csrw sepc, t0

    ld t0, TRAP_FRAME_SSTATUS_OFFSET(sp)
    csrw sstatus, t0

    #恢复通用寄存器
    ld ra, TRAP_FRAME_RA_OFFSET(sp)
    ld gp, TRAP_FRAME_GP_OFFSET(sp)
    ld tp, TRAP_FRAME_TP_OFFSET(sp)

    ld t0, TRAP_FRAME_T0_OFFSET(sp)
    ld t1, TRAP_FRAME_T1_OFFSET(sp)
    ld t2, TRAP_FRAME_T2_OFFSET(sp)

    ld s0, TRAP_FRAME_S0_OFFSET(sp)
    ld s1, TRAP_FRAME_S1_OFFSET(sp)

    ld a0, TRAP_FRAME_A0_OFFSET(sp)
    ld a1, TRAP_FRAME_A1_OFFSET(sp)
    ld a2, TRAP_FRAME_A2_OFFSET(sp)
    ld a3, TRAP_FRAME_A3_OFFSET(sp)
    ld a4, TRAP_FRAME_A4_OFFSET(sp)
    ld a5, TRAP_FRAME_A5_OFFSET(sp)
    ld a6, TRAP_FRAME_A6_OFFSET(sp)
    ld a7, TRAP_FRAME_A7_OFFSET(sp)

    ld s2, TRAP_FRAME_S2_OFFSET(sp)
    ld s3, TRAP_FRAME_S3_OFFSET(sp)
    ld s4, TRAP_FRAME_S4_OFFSET(sp)
    ld s5, TRAP_FRAME_S5_OFFSET(sp)
    ld s6, TRAP_FRAME_S6_OFFSET(sp)
    ld s7, TRAP_FRAME_S7_OFFSET(sp)
    ld s8, TRAP_FRAME_S8_OFFSET(sp)
    ld s9, TRAP_FRAME_S9_OFFSET(sp)
    ld s10, TRAP_FRAME_S10_OFFSET(sp)
    ld s11, TRAP_FRAME_S11_OFFSET(sp)

    ld t3, TRAP_FRAME_T3_OFFSET(sp)
    ld t4, TRAP_FRAME_T4_OFFSET(sp)
    ld t5, TRAP_FRAME_T5_OFFSET(sp)
    ld t6, TRAP_FRAME_T6_OFFSET(sp)

    ld sp, TRAP_FRAME_SP_OFFSET(sp)

    sret

.global task_enter
task_enter:
    mv sp, a0
    j trap_restore