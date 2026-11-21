.section .text.entry
.global _start

_start:
    # ------------------------------------------------------------------
    # 清零 .bss
    #
    # .bss 是 NOLOAD 段，不占 ELF 文件空间，加载器不会把内容读进来。
    # 但内核的全局/静态对象都放在这里，必须从确定的 0 状态开始：
    #   - C++ 全局对象的零初始化；
    #   - VirtIO split virtqueue 的 descriptor/avail/used 和请求内存；
    #   - avail.idx / used.idx 如果保留随机值，设备会误判队列状态，
    #     可能 DMA 到错误地址或把旧描述符当成新请求。
    #
    # BSS 只保证字节对齐，而且这里还没有设置栈，所以用 sb 逐字节清零，
    # 不用 sd/lw；循环结束后才设置 boot_stack_top。
    # ------------------------------------------------------------------
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
