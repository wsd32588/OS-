# BATCH01 A：在 .text.entry 中导出 _start。
# 调用 Rust 之前，把 sp 设到 64 KiB 启动栈的顶端。
# 在 .bss.stack 中定义 boot_stack_lower_bound 和 boot_stack_top。
# 栈向低地址增长；栈顶必须 16 字节对齐。
# 不要让 rust_main 返回到一个未定义的调用者。
.section .text.entry
.global _start

_start:
    la sp, boot_stack_top
    call rust_main
halt:
    j halt

.section .bss.stack
.balign 16
.global boot_stack_lower_bound
boot_stack_lower_bound:
    .space 4096 * 16

.global boot_stack_top
boot_stack_top: