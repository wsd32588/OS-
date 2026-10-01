#include "trap_offsets.h"
.section .text.trap
.balign 4
.equ SSTATUS_SPP_MASK, 0x100

.global trap_entry
trap_entry:
    # U-mode: sp <- kernel stack, sscratch <- user sp
    # S-mode: 第一次交换得到 sp=0，需要再交换回来
    csrrw sp, sscratch, sp
    bnez sp, .Ltrap_stack_ready
    csrrw sp, sscratch, sp

.Ltrap_stack_ready:
    #创建 trap_frame
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

    #保存trap CSR
    #原始 t0已保存， 现在用作临时寄存器
    csrr t0, sstatus
    sd t0, TRAP_FRAME_SSTATUS_OFFSET(sp)

    andi t0, t0, SSTATUS_SPP_MASK
    bnez t0, .Ltrap_from_supervisor

    #SPP=0: trap 来自U-Mode, 用户 sp 暂存在 sscratch
    csrr t0, sscratch
    sd t0, TRAP_FRAME_SP_OFFSET(sp)
    j .Ltrap_sp_saved

.Ltrap_from_supervisor:
    #SPP=1: trap 来自S-Mode, 原sp是frame顶部
    addi t0, sp, TRAP_FRAME_SIZE
    sd t0, TRAP_FRAME_SP_OFFSET(sp)

.Ltrap_sp_saved:
    #进入内核后重建S-Mode约定
    #这样内核处理 trap 是若发生 trap, 不会把用户sp当内核栈
    csrw sscratch, zero

    csrr t0, sepc
    sd t0, TRAP_FRAME_SEPC_OFFSET(sp)

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
    andi t1, t0, SSTATUS_SPP_MASK
    bnez t1, .Lrestore_supervisor

    #即将返回U-Mode
    #当前 frame 正上方就是下次trap使用的内核栈顶
    addi t1, sp, TRAP_FRAME_SIZE
    csrw sscratch, t1
    j .Lrestore_scratch_ready

.Lrestore_supervisor:
    #即将返回 S-mode
    csrw sscratch, zero

.Lrestore_scratch_ready:
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