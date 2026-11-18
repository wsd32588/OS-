# BATCH31 通过 exec 传递 argc 和 argv

2026-11-18 交付任务，待实现与验证。B30 已归档。

## 1. 目标与范围

调用者执行 `/bin/worker` 时，可以传入一组字符串。执行成功后，新程序从自己的用户栈读出
`"worker"`、`"blue sky"` 和空字符串，顺序和内容与调用时一致。前台 shell 的分词和
`tinyos> /bin/worker blue sky` 这样的命令输入留给下一批。正式 shell 本批继续使用 19 号接口，
参数效果通过隔离的用户程序演示。

本批只进入路径 exec 的参数传递。ELF 装载、原任务身份、FD、SPAWN 和原数值参数 ABI 沿用现有做法。

## 2. 接口

```c
long user_exec_path_args(
    const char* path, size_t path_length,
    const char* const* argv, size_t argc);
```

| 边界 | 约定 |
| --- | --- |
| ecall | `SYSCALL_EXEC_PATH_ARGS = 20`，`a0` 是 path，`a1` 是路径字节数，`a2` 是旧 argv 表地址，`a3` 是 argc |
| 路径 | 沿用 19 号的显式长度、精确查找和内置程序登记检查 |
| 输入表 | 0 到 4 项。RV64 每项 8 字节，存旧用户虚地址。读取恰好 argc 项，调用者可省略尾部 NULL |
| 输入字符串 | 每项最多 64 字节，包含终止 NUL。空字符串合法，正数 argc 时表地址和各字符串地址必须非零且可读 |
| 零项 | 忽略调用者提供的表地址，沿用空用户栈，入口 `a1 = 0`、`a2 = 0` |
| 新入口 | 20 号成功时 `a0 = 0`、`a1 = argc`、`a2 = 新用户栈中的 argv`。正数 argc 时新表的 `argv[argc] = NULL` |
| 原入口 | 10、13、19 号继续将原数值参数传到 `a0`，新现场的 `a1`、`a2` 为 0 |
| 返回 | 普通失败返回 `-1`，旧程序从 ecall 后继续。成功从新镜像的 `_start` 运行 |

本批采用 TinyOS 自己的入口约定。`user/start.S` 已直接调用 `main`，可以继续使用。仅隔离测试
接收程序用三参数入口，例如 `int main(unsigned long argument, size_t argc, const char* const* argv)`。
调用者决定 argv 内容，内核按表复制，路径仅用于选程序。

## 3. 实现步骤

1. **接入接口。** 在 `include/task/syscall.h`、`user/user_api.h` 和 `user/syscall.S` 定义
   20 号和用户调用。在 `kernel/task/syscall.c` 取四个寄存器，沿 `sched.h`、`sched.cpp` 的
   现有路径做当前 USER 任务及 SIE 检查，再进入 `user_task_exec.cpp`。分发只在失败时给旧现场
   写 `a0 = -1`，成功直接离开该分支，保留 exec 装好的现场。
2. **从旧根取得自有副本。** 复用 19 号的有界路径复制与程序选择。先限制 argc，再计算表字节数。
   通过 `user_copy_from` 复制表到内核局部数组，再从每个旧用户地址复制字符串。可逐字节读取，
   用 `VirtAddr::try_add` 检查偏移，读到 NUL 即停。地址不可读或前 64 字节找不到 NUL 就拒绝。
   记录包含 NUL 的实际长度。完成这一步后，后续代码只需要内核中的字符串副本及其长度。
3. **在候选栈布置数据。** 共用现有 exec 的候选准备和入口校验。从 `stack_top` 向下布置 NUL
   字符串，记下每项在候选栈中的虚地址。再为 `argc + 1` 个指针留空间，将表首向下按 16 字节对齐，
   令新 `sp` 等于表首。先检查范围再做减法和写入，字符串、表和 SP 都须落在
   `[stack_top - SV39_PAGE_SIZE, stack_top)` 内。使用 `user_copy_to(candidate.root, ...)`
   写实际长度与新指针表。表末多出的项由内核置零。
4. **准备完整后提交。** 拷贝失败走现有候选释放路径，此时旧根仍活动。准备完成才填好新现场并
   进入现有提交顺序：切内核根、释放旧空间、接管候选与新现场、激活新根。继续沿用原 Task、
   Process、ID 和 FD 表。候选栈使用 ELF loader 已分配的页。

布局可先用三个地址在纸上算一遍。下图地址从低到高，表中指针顺序与调用方一致。

```text
低地址
  栈页中供新程序向下使用的空间
  sp = argv → [新 arg0 VA][新 arg1 VA][新 arg2 VA][NULL]
  对齐产生的空隙
  各字符串（每项含 NUL）
  stack_top
高地址
```

内核缓冲区的地址仅供复制使用。旧、新用户空间可能有相同的虚地址数值，
应根据相应根页表核对字节归属。

## 4. 固定 P0

核心实现由作者完成，协作者集中审查并承担验证。实际结果在实施后补入本文件。

| 场景 | 通过条件 |
| --- | --- |
| 构建和宿主回归 | kdbg / user 两种配置完整链接，`make test` 全过 |
| 原入口与 shell | 既有 `test-process` 继续通过。正式 user QEMU 仍能执行 worker、报告缺失路径、再次执行 worker 并 exit |
| 隔离 QEMU 拒绝与重试 | 合法路径和指针表，第二项前 64 字节没有 NUL。20 号返回 -1，旧镜像继续、旧根和 FD 保持，参数验证阶段零 PMM 分配。由同一调用者随后提交合法参数 |
| 隔离 QEMU 成功 | 新入口实际收到 a0=0、argc=3、`{"worker", "blue sky", "", NULL}`，入口 SP 16 字节对齐。argv 及每项都落在新栈页，worker 能用原 stdout 输出。观察同一任务身份和 FD、旧空间释放及 worker 退出后的候选空间回收 |

```sh
make -j4 CONSOLE_MODE=kdbg
make test
make CONSOLE_MODE=user test-process
make -j4 CONSOLE_MODE=user
make CONSOLE_MODE=user qemu
```

正式 QEMU 依次输入 `/bin/worker`、`/bin/missing`、`/bin/worker`、`exit`。隔离 QEMU 复用
`make CONSOLE_MODE=user qemu`，协作者在临时树替换两份演示 ELF 的用户源码并保留各自的可写
LOAD 段，使用同一产品内核验证 syscall 20。P1 可选探针是候选 `copy_to` 失败的回滚和
合法 NUL 正好位于旧页最后一个字节。

## 5. 实现后的三题

1. 为什么复制旧指针表后还要复制每个字符串？为什么新旧指针数值相同也可能指到不同字节？
2. 新镜像一进 `_start`，`sp`、`a1`、`a2` 分别表示什么？谁负责写出 `argv[argc]`？
3. 如果已经写完候选表的一部分，随后一次复制失败，释放哪份空间？旧程序从哪个位置恢复？

可选阅读《操作系统：设计与实现》第三版进程管理部分的 `exec` 和初始栈，重点看调用参数
如何跨地址空间交接，可对照 TinyOS 的 `user_task_exec.cpp` 和 `user/start.S`。
