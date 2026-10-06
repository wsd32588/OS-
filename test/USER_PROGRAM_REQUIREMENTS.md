# 独立用户态 C 程序：第一批

状态：2026-10-07 本批验收完成。作者实现五个 user 源文件、incbin 和三个测试脚本复制清单；协作者按授权接入根 Makefile/.gitignore。实际用户 ELF 为 RV64，入口 0x40000000，bin 354 字节，无未定义符号或 gp 引用，内核 rodata 中的 image 字节与 bin 完全一致。协作者运行默认构建、宿主回归和完整 12 个 WRITE QEMU 场景全部通过，作者报告 make test-qemu 的旧 Q1/Q2 已通过；本轮未重复 Q1/Q2。临时副本验证用户 C/共享头修改的增量构建、初始化与未初始化可写全局的拒绝、超一页镜像拒绝。当前仅支持代码/只读数据/栈，仍使用共享根页表，不解析 ELF。未提交。

## 目标和范围

将当前汇编演示改为独立编译的 C 用户程序，保留原来可观察的 yield -> R -> sleep(2) -> XHello from user! -> exit。

路径是：user 源码 -> 独立 demo.elf -> objcopy 生成 demo.bin -> 内核 rodata 嵌入 -> 现有 prepare_demo_user_memory 复制到用户代码页 -> _start 调用 C main。

本批仍使用 0x40000000 的一页 U/R/X 镜像，以及 0x40001000 的一页 U/R/W 栈。栈指针已由 task_create_user 设置，不在用户启动文件中重复设置。内核不解析 ELF；不能把有 ELF 文件头的 demo.elf 当作原始指令页复制。

本批只支持代码、只读常量和栈局部变量。static const char 数组可进入 rodata；可写全局变量、静态可写变量、BSS/TLS 和 C 库尚无运行时支持，链接时主动拒绝。不要引入 printf、malloc、libc 启动文件或新的地址空间。

## 文件清单

新增 user/start.S、user/syscall.S、user/user_api.h、user/main.c、user/user.ld。

修改根 Makefile、.gitignore、kernel/user_entry.S，以及 test/verify_syscall_write.py、test/verify_prepare_rollback.py、test/verify_task_creation_rollback.py 的源码复制清单。

prepare_demo_user_memory、调度器、syscall 分发和用户回收继续使用现有实现。

## 1. 用户 C 接口

user/user_api.h：

```c
#ifndef TINYOS_USER_API_H
#define TINYOS_USER_API_H
#include <stddef.h>
long user_write(const void* buffer, size_t length);
long user_putchar(unsigned char character);
long user_yield(void);
long user_sleep(unsigned long ticks);
_Noreturn void user_exit(void);
#endif
```

这些接口声明属于用户侧。不要在用户程序中调用 kernel 的 uart_putchar/syscall_handle。syscall 编号继续包含已有 include/syscall.h，共用 ABI 常量。

## 2. 用户 syscall 汇编

user/syscall.S 核心骨架：

```asm
#include "syscall.h"
.section .text, "ax", @progbits
.option norelax

.global user_write
user_write:
    li a7, SYSCALL_WRITE
    ecall
    ret

# TODO: user_putchar, user_yield, user_sleep follow the same ABI.

.global user_exit
user_exit:
    li a7, SYSCALL_EXIT
    ecall
1:
    j 1b

.section .note.GNU-stack, "", @progbits
```

C ABI 已把 user_write 的两个参数放在 a0/a1，内核返回值也在 a0，因此包装函数只需设置 a7、ecall、ret。user_putchar 的参数和 user_sleep 的参数都使用 a0，user_yield 没有参数。补齐三者全局符号，使用对应编号。

user_exit 声明 _Noreturn，因此不能 ret；ecall 万一返回，进入死循环。当前内核 EXIT 不处理退出码，main 返回 1 本身不会被测试框架认作失败，必须有失败标记。

## 3. 从入口进入 C

user/start.S：

```asm
#include "syscall.h"
.section .text.entry, "ax", @progbits
.option norelax
.global _start
_start:
    call main
    call user_exit
1:
    j 1b
.section .note.GNU-stack, "", @progbits
```

_start 是用户入口，调用 main 时遵循正常 C ABI。用户栈已经存在且 16 字节对齐。main 返回后调用 user_exit。这里不能跳到 kernel_main，也不能使用内核 gp/栈地址。

## 4. main 的练习

user/main.c 骨架：

```c
#include "user_api.h"
#include <stdint.h>

int main(void) {
    static const char message[] = "Hello from user!\n";

    /* TODO: yield -> putchar('R') -> sleep(2) -> putchar('X').
       Check each return value is 0. */

    long written = user_write(message, sizeof(message) - 1);
    /* TODO: compare written with (long)(sizeof(message) - 1). */

    const void* invalid = (const void*)(uintptr_t)0x40002000;
    /* TODO: WRITE(invalid, 1) returns -1; WRITE(invalid, 0) returns 0. */

    /* TODO: any mismatch prints "[write] failed\n" before returning 1.
       EXIT currently ignores main's return value. */
    return 0;
}
```

补齐 TODO，确保所有重要返回值被检查。失败处理可用一个小 fail 函数，以 user_write 输出固定的 [write] failed 加换行，再返回 1 交启动代码退出，也可像作者当前实现一样直接调用 user_exit；这一次诊断输出可以明确标为 best effort，不成功也应走退出路径。正常消息长度用 sizeof(message) - 1，排除结尾 NUL。

程序顺序和验收：

1. user_yield() 返回 0。
2. user_putchar('R') 返回 0。
3. user_sleep(2) 返回 0。
4. user_putchar('X') 返回 0。
5. user_write(message, sizeof(message) - 1) 返回 17。
6. user_write((const void*)(uintptr_t)0x40002000, 1) 返回 -1。
7. 同一个坏指针、长度 0 返回 0。
8. main 返回，由启动代码调用 EXIT。

这些检查应当真的在 C 中判断，不只凭 UART 输出推断。

## 5. 用户链接脚本

user/user.ld：

```ld
OUTPUT_ARCH(riscv)
ENTRY(_start)
PHDRS {
    image PT_LOAD FLAGS(5);
}
SECTIONS {
    . = 0x40000000;
    __user_image_start = .;
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } :image
    .rodata : {
        *(.rodata .rodata.* .srodata .srodata.*)
    } :image
    __user_image_end = .;
    .data : {
        *(.data .data.* .sdata .sdata.*)
        *(.got .got.* .igot .igot.*)
        *(.tdata .tdata.*)
        *(.init_array .init_array.* .fini_array .fini_array.*)
    } :image
    .bss (NOLOAD) : {
        *(.bss .bss.* .sbss .sbss.* .tbss .tbss.*)
        *(COMMON)
    } :image
    /DISCARD/ : {
        *(.eh_frame .eh_frame.* .comment .note.*)
    }
    ASSERT(_start == 0x40000000, "user entry must be at image start")
    ASSERT(__user_image_end - __user_image_start <= 4096, "user image exceeds one page")
    ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "writable globals are not supported yet")
}
```

这里的地址是运行时用户 VA，不是内核中的存储地址，也不是 PMM 返回的 PA。text.entry 排在最前，确保镜像第一个字节对应 _start。链接器必须拒绝超 4096 字节、非起始入口以及可写段，不能等运行时 fault 才发现。

PHDRS 的 FLAGS(5) 表示这个用户 ELF LOAD 段是 R/X。它用于描述构建产物；实际页表权限继续由 prepare_demo_user_memory 设置。

## 6. 根 Makefile 构建规则

保留 all: kernel.elf 作为默认目标，将下列规则加入现有根 Makefile，不需要再创建一个用户 Makefile。配方行必须是 Tab。

```make
OBJCOPY = riscv64-linux-gnu-objcopy
USER_BUILD_DIR = user/build
USER_OBJS = $(USER_BUILD_DIR)/start.o $(USER_BUILD_DIR)/syscall.o $(USER_BUILD_DIR)/main.o
USER_CFLAGS = $(ARCH_FLAGS) -std=c11 -Wall -Wextra -O0 -g \
    -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -msmall-data-limit=0 -Iinclude -Iuser

$(USER_BUILD_DIR):
	mkdir -p $@

$(USER_BUILD_DIR)/%.o: user/%.c | $(USER_BUILD_DIR)
	$(CC) $(USER_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/%.o: user/%.S | $(USER_BUILD_DIR)
	$(CC) $(USER_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(USER_BUILD_DIR)/demo.elf: $(USER_OBJS) user/user.ld
	$(LD) --no-relax -T user/user.ld $(USER_OBJS) -o $@

$(USER_BUILD_DIR)/demo.bin: $(USER_BUILD_DIR)/demo.elf
	$(OBJCOPY) -O binary $< $@

kernel/user_entry.o: $(USER_BUILD_DIR)/demo.bin

-include $(USER_OBJS:.o=.d)
```

编译 C 和 S 使用相同 RV64 架构/ABI。用户还需要 -msmall-data-limit=0 和禁用 relaxation，避免 gp==0 时访问小数据；反汇编应无 gp 引用。直接调用 ld 只链接这三个对象，不引入 libc/crt。-MMD/-MP 加上用户 .d 文件让头文件修改触发重编。

kernel/user_entry.o 必须显式依赖 demo.bin，.incbin 的二进制输入不能指望普通 C 头依赖扫描自动发现。| USER_BUILD_DIR 是 order-only 依赖：保证目录存在，不因为目录时间变化反复重编。

.gitignore 加一行 /user/build/，不要提交 ELF、bin 或用户构建缓存。

## 7. 内核嵌入原始镜像

kernel/user_entry.S 的正常镜像改为：

```asm
.section .rodata.user_image, "a", @progbits
.balign 4
.global user_image_start
.global user_image_end
user_image_start:
    .incbin "user/build/demo.bin"
user_image_end:
```

GNU 汇编器将 bin 原样放入内核 rodata，仍导出 user_image_start/end。现有复制逻辑、fence.i、映射/flush、任务创建全部继续使用这两个标签。

## 8. 三个 QEMU 脚本复制 user 源码

三个脚本的 copytree 目录元组都补 user：

```python
for name in ("include", "kernel", "drivers", "user"):
    # 保留该脚本现有的 repository/REPO 变量和目录写法
```

ignore_patterns 改为：

```python
shutil.ignore_patterns("*.o", "*.d", "build")
```

临时工程必须从源码生成用户镜像，不能依赖工作区现有的 user/build。WRITE 生成场景仍然直接覆盖临时 user_entry.S；其基线使用新的 C 镜像。Hello 字符串保持不变，既有 EXPECTED_BASELINE 不需改。Q1/Q2 的探针和检查逻辑保留。

## 整批验收

```sh
make -j4
riscv64-linux-gnu-readelf -h user/build/demo.elf
riscv64-linux-gnu-objdump -d user/build/demo.elf
wc -c user/build/demo.bin
python3 -u test/verify_syscall_write.py baseline
make test
python3 -u test/verify_syscall_write.py
make test-qemu
git diff --check
```

必须同时满足：

- demo.elf 为 RV64，Entry == 0x40000000，_start 位于镜像起点，bin 大小在 1..4096。
- 无未定义符号或 gp 引用，构建没有新警告。
- C main 实际检查所有返回值，QEMU 输出 Hello 一次，无失败标记/fault/release failed/halt，exit 后 A 与至少两次 S-mode timer 继续运行。
- 修改 user/main.c 的消息再 make，main.o -> demo.elf -> demo.bin -> user_entry.o -> kernel.elf 都更新，确认后恢复标准消息。
- 在临时副本中添加可写全局变量或超过一页的只读数组，链接必须失败；探针不留在正常程序中。协作者集中检查时负责验证这两项。
- 原 12 个 WRITE 场景、宿主 H1-H8/复制测试和 Q1/Q2 回归全部通过。

作者已整批实现并经集中审查；协作者负责构建入口、临时探针和验收，工作区 user 程序由作者完成。
