# BATCH31 通过 exec 传递 argc 和 argv

2026-11-18 交付并完成。全部固定 P0 已通过，三题已归档，见第 7—8 节。

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

## 6. 共用 exec 和参数栈的局部验证（2026-11-18）

按作者请求在 `user_task_exec.cpp` 接好共用事务、候选栈和调用端参数复制。
路径查找通过状态与输出 ID 区分失败及合法 ID 0。候选栈的写入成功后才提交。

隔离构建 kdbg 和 user 两配置均通过。`make test` 的 3 项 CTest 全过，
`make CONSOLE_MODE=user test-process` 通过，证据为
`.cache/process-regression/20261118-233044-3/results.json`。

`.cache/batch31-exec-check-r8pqft7h/` 中的 `shell.log` 记录正式 shell 的
worker、缺失路径、worker、exit。该目录的隔离 `argv-fixture.log` 记录 19 号执行 ID 0 并传入
数值 17、20 号拒绝第二项无 NUL 后继续执行、argc=0 忽略输入地址。三参数成功路径验证了
字符串内容与次序、NULL、栈范围、入口 SP 等于 argv 且 16 字节对齐，以及 stdout 可用。
隔离入口仅用第四个寄存器采样初始 SP。本阶段正式 shell 草稿引入了两个未使用参数告警。
第 8 节记录签名恢复及 20 号的 PMM、任务身份和 FD 观测复验。

## 7. 原三题与集中澄清（2026-11-18）

### 作者原答

1. 复制旧值针表之后等函数结束就会抛弃变成空指针，所以需要复制下来，新指针数值相同但是数据不一定四原来的数据
2. 新镜像一进start之后sp指向当前新的栈顶，a1，a2分别指向argv和argc,frame->a0
3. 写完候选表的一部分，复制失败直接释放栈上空间，旧程序从satp恢复

### 集中澄清

1. 旧指针表的内核副本里装的是旧用户虚地址。复制字符串是为了在释放旧空间之前把实际字节搬进新栈。
   新表按候选栈地址重建。同一虚地址数值经不同页表可以映射到不同物理页。局部数组的生命周期和表尾
   的 NULL 是两件事，尾项由内核显式写为零。
2. 20 号成功后，入口 `a0 = 0`、`a1 = argc`、`a2 = argv`。有参数时 `sp` 等于新表首地址，
   16 字节对齐。零项时 `a1 = a2 = 0`、`sp = stack_top`。
3. 候选栈写入报错时，`reject_candidate` 释放整份候选用户地址空间，包含用户栈页和候选页表。
   内核栈上的 `candidate` 记录持有这些资源。此时 `satp` 仍选中旧根，旧现场继续用于 trap 返回。
   syscall 已把旧 `sepc` 加 4，再写旧现场的 `a0 = -1`。`sret` 从旧程序的 ecall 后一条指令继续。

## 8. 全部固定 P0 验收（2026-11-18）

正式 shell 恢复单参数 `main`，子进程继续调用 19 号。两份独立验收的 108 个正式源文件
前后哈希一致。20 号专项在隔离树使用临时用户镜像和观察器，exec 实现与正式源字节一致。

| 验证 | 实测结果 |
| --- | --- |
| 构建、宿主和正式 shell | kdbg/user 两配置完整构建，零诊断、零未定义符号。137 个宿主用例及 3 项 CTest 通过。正式 user QEMU 按 worker、缺失路径、worker、exit 运行，9 项检查通过。 |
| 旧进程回归 | `make CONSOLE_MODE=user test-process` 全 27 项通过。运行期申请 21 页、释放 35 页，净恢复 14 页，收集 3 次，文件池归零并观察到两次 S 态 idle tick。 |
| 20 号拒绝与旧程序继续 | 同一真实用户调用先传入第二项前 64 字节无 NUL 的参数表。返回 -1 后从旧 PC+4 继续输出，SP、satp、根页表、Task、Process、ID、现场地址和 8 个 FD 槽均保持。PMM 零申请、零释放，可用页数保持。 |
| 20 号合法重试 | 同一调用者成功替换镜像。实际 worker 收到 a0=0、argc=3，以及 `{"worker", "blue sky", "", NULL}`。argv 和字符串位于新栈页，入口 SP 等于表首地址且按 16 字节对齐。同一任务和 8 个 FD 槽保持，worker 通过原 stdout 输出并退出 0。 |
| 回收 | 申请候选 6 页，exec 释放旧空间 6 页，worker 退出释放新空间 6 页。两次释放均在内核 satp 下，各自私有根最后释放，页无重释放。运行期合计申请 6、释放 12，可用页净恢复 6，文件池归零。 |

基线证据位于 `.cache/batch31-baseline-gy6d7g6m/results.json`。旧进程回归证据位于
`.cache/process-regression/20261118-234838-11/results.json`。20 号专项的 12 项检查及串口日志
位于 `.cache/batch31-resources-KgvT4J/`。本批验收对应第 4 节固定 P0，候选写入故障注入仍属于 P1。

B31 收尾。正式 shell 接入 20 号的任务见 [BATCH32](BATCH32_SHELL_ARGUMENTS.md)。
