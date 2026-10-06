# 用户内存访问：第二批 WRITE syscall

状态：2026-10-06 本批功能验收完成。verify_syscall_write.py 已有 12 个持久场景，作者分批提供全部 PASS 日志，协作者审查脚本并通过临时副本复验两种特殊映射准备方式。root getter 的初始化前/无任务、内核任务、用户 syscall、idle、非法下标及 READY/BLOCKED/DEAD 边界由协作者在临时 QEMU 副本验证，调用前后 PMM 页数和 satp 不变。当前 make、宿主测试和旧 Q1/Q2 均通过；产生 unused-function 警告的空 C++ 草稿随后已由作者删除。root 探针尚未持久化，WRITE 脚本仍单独运行，未加入 make test-qemu。下一批独立 C 用户程序见 USER_PROGRAM_REQUIREMENTS.md。

本批目标：用户程序传入地址和长度，内核借助当前任务的页表安全复制，
成功后向 UART 输出全部字节。正常演示输出 Hello from user!，坏地址返回 -1，
用户程序能检查返回值并继续运行。保留当前共享 root、静态任务和恒等映射。

## ABI

在 include/syscall.h 增加两个 C/汇编共用的整数宏：

```c
#define SYSCALL_WRITE       5
#define SYSCALL_WRITE_MAX   256
```

| 寄存器 | 含义 |
|---|---|
| a7 | SYSCALL_WRITE |
| a0（调用前） | 用户虚拟地址 |
| a1（调用前） | 无符号字节数 |
| a0（返回后） | 成功返回字节数；失败返回 (u64)-1 |

- 长度为 0：直接返回 0，不检查地址或 root，不输出。
- 长度在 1..256：完整复制成功后按长度输出，返回原长度。
- 长度超过 256：返回 -1；不截断、不分块输出，也不先计算末尾地址。
- 非零长度下没有当前用户 root，或者 user_copy_from 失败：返回 -1，无用户数据输出。
- 零字节属于数据，例如 {'A', 0, 'B'} 必须输出三个字节，返回 3。
- 不追加换行，不使用 NUL 判断结束，不增加 WRITE 调试日志。
- 复用 syscall_handle 现有的 sepc += 4；WRITE 不再推进一次，也不切换任务。

## 修改范围

1. include/syscall.h：编号和上限。
2. include/sched.h、kernel/sched.c：只读的当前用户根页表获取接口。
3. kernel/syscall.c：私有 WRITE 助手和分发 case，引入 user_access.h。
4. kernel/user_entry.S：正常调用及返回值检查，字符串放进用户镜像。

不需要修改 user_copy_from、UserMemory 归属、准备/回收逻辑或构建源码清单。

### 当前任务 root

公开接口：

```c
const Sv39PageTable *sched_current_user_root(void);
```

实现仍放在 sched.c 内，避免将私有 tasks[] 或 struct task 暴露给 syscall。
先判断 current_task < 0 或 current_task >= task_count，再访问 tasks[current_task]。
要求状态为 TASK_RUNNING，随后返回 user_memory.root。
没有当前任务、idle、普通内核任务或状态不合适时返回 NULL。
返回的是借用指针，不分配、不释放、不转移归属。

### WRITE 执行路径

可以用下面骨架完成私有助手，返回类型与 trap_frame.a0 一致：

```c
static u64 syscall_write(uintptr_t source_address, u64 length) {
    /* 1. length == 0 返回 0；length > 上限返回 (u64)-1。 */

    const Sv39PageTable *root = sched_current_user_root();
    /* 2. root 为空返回失败。 */

    unsigned char buffer[SYSCALL_WRITE_MAX];
    /* 3. 调用 user_copy_from；失败立即返回，丢弃整个 buffer。 */
    /* 4. 复制成功后按长度逐字节 uart_putchar。 */
    /* 5. 返回原长度。 */
}
```

先检查原始 u64 长度，再转 size_t；当前 RV64 下 uintptr_t/size_t 为 64 位。
局部缓冲区固定 256 字节，避免由用户输入决定栈空间。
不要直接解引用 source_address，也不要用 uart_puts(buffer)：它会在 NUL 处停止，
且本缓冲区不保证存在末尾 NUL。

分发骨架：

```c
case SYSCALL_WRITE: {
    uintptr_t source_address = (uintptr_t)frame->a0;
    u64 length = frame->a1;
    frame->a0 = syscall_write(source_address, length);
    break;
}
```

参数先保存，再写返回值；case 内用大括号限定局部变量作用域。
失败时绝不能输出已复制的合法前缀。全过程不申请/释放物理页、不改映射、不 flush、
不设置 SUM，不触碰 satp。通过已经检查过的 PA 和现有恒等映射读取数据。

## 用户汇编：镜像内字符串

保留现有 yield -> R -> sleep(2) -> X，在 X 后、exit 前插入 WRITE。
字符串必须位于 user_image_start 与 user_image_end 之间，并放在 exit 和兜底死循环之后，
避免被当作指令执行。使用 .ascii，不隐式附加 NUL；本演示含换行共 17 字节。
在使用 li 之前定义 .equ USER_MESSAGE_LENGTH, 17；改变字符串时同步修改此常量。
当前工具链拒绝 li 寄存器, .Lmessage_end - .Lmessage，不能直接用前向标签差值作为 li 的操作数。

用户镜像从内核地址复制到 0x40000000，因此字符串地址必须相对当前 PC 计算。
在现有 .option push/.option pop 内增加 .option norelax，使用下面的成对重定位：

```asm
.equ USER_MESSAGE_LENGTH, 17

.Lwrite_address:
    auipc a0, %pcrel_hi(.Lmessage)
    addi  a0, a0, %pcrel_lo(.Lwrite_address)
    li    a1, USER_MESSAGE_LENGTH
    li    a7, SYSCALL_WRITE
    ecall

    li    t0, USER_MESSAGE_LENGTH
    bne   a0, t0, .Lwrite_fail

    # 后续正常 exit 和兜底死循环；补齐失败分支。
    # 失败分支用 PUTCHAR 输出 '?' 后 exit，不能默默继续成功路径。

.Lmessage:
    .ascii "Hello from user!\n"
.Lmessage_end:
    # user_image_end 位于全部指令和数据之后。
```

%pcrel_lo 指向 auipc 所在标签，而不是 .Lmessage。
两条指令构成相对偏移，整段代码和字符串一起搬迁后该距离不变。
不要写 li a0, .Lmessage，也不要让链接器改成依赖 gp 的取址；当前用户 gp 为 0。
原有 .option norvc 保留，镜像仍须不超过一个 4 KiB 页。

## 整批验收

作者先完成上述四组文件的正常路径。随后集中审查和验证以下全部条件；
边界探针在临时源码副本中运行，不将 fault 或修改权限的探针留在正常演示里。
无效地址探针只将地址作为 syscall 参数，不能先在用户态 load 它。

| 场景 | 必须观察到的结果 |
|---|---|
| 镜像内正常字符串 | 输出恰好 17 字节 Hello from user! 加换行，a0 == 17，随后 exit |
| 数据 A/NUL/B | UART 原始日志包含三个字节，a0 == 3，NUL 后的 B 仍输出 |
| 长度 256 / 257 | 256 成功返回 256；257 返回 -1 且不输出用户数据 |
| 未映射起点 0x40002000，长度 1 | 返回 -1；用户继续运行，无 user fault/kernel halt |
| 非 canonical 或加法溢出范围 | 返回 -1，不输出；复用复制函数的校验 |
| 内核恒等映射地址，缺 U | 返回 -1，不泄露内核内容 |
| 用户执行专用页，U/X 没有 R | 返回 -1，不因为可以执行而允许读取 |
| 合法跨页，物理页不相邻 | 输出两页相接的数据，返回总长度 |
| 栈页末尾两字节起，长度 4，下一页未映射 | 返回 -1，前两字节也不能输出 |
| 任意坏地址，长度 0 | 返回 0，无用户数据输出 |
| root 获取的边界 | idle/无任务/普通内核任务返回 NULL；当前用户任务返回其借用 root |
| 原功能回归 | yield/sleep/exit 正常；用户退出后 A 和 S-mode timer 继续运行，旧宿主及 Q1/Q2 通过 |

所有 U-mode 探针都要在 ecall 后实际比较 a0，不能只根据打印内容判断返回值。
跨页失败可取 0x40001ffe/4；合法跨页取 0x40000ffe/4 并在临时副本准备已知四字节。
不要假设两页物理地址相邻。NUL 用例检查原始字节日志，终端肉眼看不到 NUL。
root 边界和 PMM 页数/映射不变可由临时调试探针检查；正常功能无需增加测试专用全局变量。

```sh
make test CTEST='ctest --verbose'
make
python3 -u test/verify_syscall_write.py
make test-qemu
git diff --check
```

timeout 返回 124 是观察时间结束，不是内核故障。宿主测试覆盖复制算法，
本批 syscall 寄存器、返回恢复和真实 U-mode 执行必须由 QEMU 验证。
当前 make test-qemu 只有旧 Q1/Q2，不能将其通过当作 WRITE 已验收。
