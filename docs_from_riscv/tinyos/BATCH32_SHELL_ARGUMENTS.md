# BATCH32 shell 分词与前台命令参数

2026-11-18 交付任务。2026-11-19 完成实现和固定 P0，三题归档见第 8 节，验收见第 9 节。

## 1. 为什么 shell 要分词

B30 的 `user/main.c` 中，全局入口函数 `main` 在 U 态运行。它把一行输入直接当成一个程序路径，
遇到空格就丢弃该行。
因此用户可以执行 `/bin/worker`，却无法在同一行告诉 worker 要接收哪些参数。
B31 已让内核能通过 20 号系统调用把参数交给新进程镜像。B32 要让 shell 准备这份参数表。

shell 先按空格把输入分成词，再把第一个词同时用作查找程序的路径和 `argv[0]`。
`path_length` 只描述路径词，内核用这段字节精确查找程序。`command_argc` 数整张参数表的项数，
后面的词由 worker 按顺序读取。用户会在终端看到类似的输出。

```text
tinyos> /bin/worker red blue
Hello from TinyOS worker.
[worker] arg: red
[worker] arg: blue
tinyos> /bin/worker
Hello from TinyOS worker.
tinyos> exit
[shell] bye
```

### 一条前台命令怎样从 shell 到 worker

U 态是用户程序运行的特权级，S 态是 TinyOS 内核处理系统调用时的特权级。活动根页表决定 CPU
怎样把当前程序使用的虚拟地址翻译为物理地址。进入内核和切换根页表是两个不同的动作。

1. **父 shell 在 U 态读行并分词。** `user/main.c` 的 `main` 在 shell 的用户地址空间中保存整行，
   并建立指向各词的指针表。shell 使用 `user/syscall.S` 中的全局入口 `user_fork` 发出 9 号系统调用。
   CPU 执行 `ecall` 进入 S 态时，仍使用父 shell 的根页表。`kernel/task/syscall.c` 的
   `syscall_handle` 经 `kernel/task/sched.cpp` 的 `sched_fork_current` 调用
   `kernel/task/user_task_create.cpp` 的 `tinyos::user_task::fork`。该函数为子进程准备独立的
   用户地址空间和执行现场。父进程从 fork 收到子任务 ID，继续使用自己的地址空间。子进程获调度后，
   用子进程的根页表在 U 态从 fork 返回点继续，得到返回值 0。
2. **子进程在 U 态发起 exec。** `user/main.c` 的 `main` 在 `pid == 0` 分支调用
   `user/syscall.S` 中的全局入口 `user_exec_path_args`，其 C 声明在 `user/user_api.h`。
   CPU 执行该入口的 `ecall` 进入 S 态时，子进程原来的根页表仍然活动。
   `kernel/task/syscall.c` 的 `syscall_handle` 取出路径和参数表的用户地址，
   经 `kernel/task/sched.cpp` 的 `sched_exec_path_args` 把请求交给
   `kernel/task/user_task_exec.cpp` 的 `tinyos::user_task::exec_path_args`。
   `exec_path_args` 按子进程的旧根复制路径，查找已登记的程序，再复制参数表和字符串。
   此时子进程仍以旧 shell 镜像作为当前用户空间。
3. **内核在 S 态准备并提交新镜像。** `kernel/task/user_task_exec.cpp` 在 `tinyos::user_task`
   的匿名命名空间中定义 `exec_impl` 和 `write_exec_arguments`。前者准备候选地址空间，
   调用后者把参数字符串和新指针表写进候选用户栈，后者也负责写表尾的 `argv[argc] = NULL`。
   准备成功后，`exec_impl` 先切到内核根，再调用 `kernel/process/process.cpp` 的
   `tinyos::Process::release_address_space` 释放当前子进程的旧用户空间。随后 `exec_impl`
   调用同文件的 `tinyos::Process::adopt`，把候选空间交给原来的子进程对象，并激活新根。
   子进程回到 U 态后，从 `user/start.S` 的全局入口 `_start` 运行，由该入口调用
   `user/worker.c` 的全局 `main`。20 号系统调用为新入口设置 `a0 = 0`、`a1 = argc`、
   `a2 = argv`。其中 `argv` 指向新用户栈中的表。普通准备失败时，内核保留子进程的旧用户空间，
   有候选资源时由 `exec_impl` 的失败分支释放。子进程从旧现场返回 U 态，在原调用处收到 `-1`。
4. **父 shell 等待自己的前台子进程。** 父进程仍运行原来的 `user/main.c` 镜像。它在 U 态调用
   `user/syscall.S` 的全局入口 `user_waitpid`，传入父进程从 fork 得到的子任务 ID。
   等待调用进入 S 态时，活动根仍属于父 shell。`kernel/task/syscall.c` 的内部函数
   `syscall_waitpid` 检查该子任务是否退出。子任务尚未退出时，内核返回 `-2`。结果可用时，
   `syscall_waitpid` 将结果写入父进程的用户缓冲区，再收集子任务并返回其 ID。父 shell 收到匹配的
   ID 后才读取下一行。

## 2. 工程约定

| 输入 | 行为 |
| --- | --- |
| CR / LF / CRLF | 沿用 B30 的行结束、回显和去掉多余 LF 的规则。CR 后吞 LF 的状态跨子进程等待保留 |
| ASCII 空格 | 跳过行首、行尾以及相邻词之间的任意多个空格。空行或全空格行只重新提示 |
| 其他可打印字符 | `0x21` 至 `0x7e` 原样留在词内。引号、反斜杠等字符按字面值处理 |
| 行长度 | 最多 127 个输入字节，包含分隔空格，行尾另留一个 NUL 槽。第 128 个字节或其他控制字符（如 Tab）使整行无效，读到行尾再报错 |
| 词数 | 最多 4 个，含命令路径 `argv[0]`，因此最多 3 个额外参数 |
| 词长度 | 每词最多 63 个可见字节，留 1 字节给 NUL，路径词也遵守这个限制 |
| 内置命令 | 单词 `exit` 退出 shell，前后空格可有。带其他词的 `exit` 报一次 `[shell] invalid line`，然后提示下一行 |
| 无效行 | 行读取或分词任一阶段拒绝后，报告一次 `[shell] invalid line`，零 fork。词合法但路径无法执行时，走子进程 exec 失败的原分支 |

shell 将命令路径作为 `argv[0]`，路径长度只计第一个词的实际字节。内核仍负责查找内置程序。

## 3. 实现任务与自查

作者修改 `user/main.c` 的 shell 和 `user/worker.c` 的 worker。内核、系统调用签名、启动汇编和链接布局
沿用 B31。引号、转义、路径搜索、重定向、管道和后台执行留待后续批次。
各步骤的自查用于观察正在实现的行为，固定 P0 见第 4 节。

1. **读整行。** 在 `user/main.c` 的 `main` 里修改逐字节读入的内层循环。shell 使用一个
   能容纳 127 个输入字节和结尾 NUL 的缓冲区，接受 `0x20..0x7e`。shell 读完整条有效行后才写
   NUL。保留写入前的容量检查、无效行读到行尾才报错、每次回显的结果检查，以及跨命令等待保持的
   `skip_lf`。自查时输入含空格的命令，观察 shell 是否完整回显。再用 CRLF 结束一行，观察是否只出现一次新提示。
2. **在原缓冲区分词。** 在 `user/main.c` 的 `main` 中，把分词放在读完整行之后、fork 之前。
   shell 每处理一行就重置行长度、`command_argc` 和词表。词表的每个元素保存一个指向当前行缓冲区的
   地址。shell 找到词尾后在分隔位置写入 NUL，继续扫描时要越过这个位置。第一个词的字节数单独保存
   到 `path_length`。写词表前检查还有没有空槽，拒绝第 5 个词和超过 63 字节的词。表刚好装满时，
   仍需跳过余下的空格，因为只有出现新的词才超限。自查时比较四个词后跟空格和末尾多一个词的两行输入，
   前者应执行，后者应报一次 `invalid line` 且不创建子进程。
3. **处理零词、内置命令和前台执行。** 在 `user/main.c` 的 `main` 中，先判断词数是否为零，
   再读取首词。零词时，父 shell 直接回到提示符。首词完整匹配 `exit` 时，父 shell 在 fork 前处理它。
   独立的 `exit` 打印 bye 并让 shell 的 `main` 返回 0，随后 `user/start.S` 的 `_start`
   调用 `user/syscall.S` 的全局入口 `user_exit`，以该值结束 shell。带参数的 `exit` 报无效行
   并继续提示。其他合法命令由父 shell 创建一个子进程，子进程只发一次 20 号调用。调用返回时，
   子进程用 `user/main.c` 的文件内函数 `write_line` 写错误诊断，再以 127 退出。
   父 shell 保留原有的精确等待和收集逻辑。自查时分别输入 `exit later`、缺失路径和 `exit`，观察
   无效行、子进程执行失败和父 shell 退出三种结果。
4. **展示 worker 收到的参数。** 把 `user/worker.c` 的 `main` 改为
   `int main(unsigned long argument, size_t argc, const char* const* argv)`。
   worker 的 `main` 保留原欢迎语及其可写数组，从 `argv[1]` 起按顺序通过 stdout 输出
   `[worker] arg: <内容>`。它每次调用 `user/syscall.S` 的全局入口 `user_write_fd` 都要检查
   实际写入长度，失败返回非零。旧入口的 `argc == 0` 和 20 号的 `argc == 1` 都只打印欢迎语。
   两份应用继续保留非空可写 ELF 加载段（RW LOAD），代码和只读数据仍在一页内。
   自查时分别执行不带额外参数的 worker 和带两个额外参数的 worker，比较输出。

`user/main.c` 的 `main` 子分支调用示例

```c
if (pid == 0) {
    (void)user_exec_path_args(command_argv[0], path_length, command_argv, command_argc);
    (void)write_line(TINYOS_STDERR_FILENO, "[shell] exec failed\n");
    user_exit(127);
}
```

这里传入的 `command_argv` 和 `path_length` 来自刚读完的命令。子进程用数组名传入指针表，
用 `command_argc` 告诉内核表里有几项。正式 shell 的 `user/main.c` 入口继续使用
`main(unsigned long argument)`。

## 4. 固定 P0

协作者承担固定 P0。编译两个 profile，跑宿主测试和旧进程回归，随后在正式 user 镜像中进行一次有节奏的串口会话。

| 输入顺序 | 可观察结果 |
| --- | --- |
| 三个空格加 CRLF | 新提示符恰好一次，零 fork |
| `/bin/worker` 加 CRLF | 一条欢迎语，零额外参数，子退出 0 |
| `  /bin/worker  red   blue  ` 加 LF | 欢迎语，随后分别输出 red 和 blue，子退出 0 |
| `/bin/worker a b c d` 加 LF | 第五个词导致一次 invalid line，零 fork |
| `/bin/worker ` 之后 64 个 `x` 再加 LF | 超长词导致一次 invalid line，零 fork |
| 117 个空格之后 `/bin/worker` 再加 LF | 第 128 个字节触发整行丢弃，一次 invalid line，零 fork |
| `exit later` 加 LF | 一次 invalid line，零 fork |
| `/bin/missing red` 加 LF | 一次 exec failed，前台子退出 127，父 shell 继续 |
| `/bin/worker ` 之后 63 个 `x` 再加 LF | 成功输出完整的 63 字节参数，子退出 0 |
| ` exit ` 加 CR | shell 打印 bye 并退出 0 |

同一会话应观察到 4 次成功 fork、4 次 20 号 syscall（3 成功、1 拒绝）、4 次成功收集，
收集状态依次为 0、0、127、0。第一个 worker 在入口收到包含路径的 1 项，第二个收到包含路径
和两个参数的 3 项。父侧完成收集后才再次显示提示符。P1 可选练习用独立测试观察旧数值入口的
`argc == 0`，或者比较输入 buffer 与新镜像栈上 argv 的虚地址归属。

```sh
make -j4 CONSOLE_MODE=kdbg
make test
make CONSOLE_MODE=user test-process
make -j4 CONSOLE_MODE=user
make CONSOLE_MODE=user qemu
```

以上 shell 会话使用正式 worker。验证脚本可在隔离树加只读观察。终端中用 `Ctrl+A` 再按 `X` 退出 QEMU。

## 5. 实现后的三题

1. 父 shell 已在 U 态为 `/bin/worker amber` 完成分词，子进程刚从 `user/syscall.S` 的
   `user_fork` 返回且还未调用 exec。子进程读 `command_argv[1]` 及它指向的字符串时，CPU 使用
   哪份根页表？如果父子两边的表项数值相同，两边分别访问谁拥有的物理页？
2. 终端输入两个前导空格，再输入 `/bin/worker  amber    blue`，最后输入两个尾随空格和换行。
   `user/main.c` 的 `main` 传给 20 号调用的 `path_length`、`command_argc` 和各表项内容分别是什么？
   `user/worker.c` 的 `main` 会输出哪些参数行？
3. 比较两条输入。第一条是 117 个空格后接 `/bin/worker red` 和换行。第二条是 `/bin/missing red`
   后接换行。第一条触发长度限制后，`user/main.c` 的 `main` 为什么还要读取剩余字节直到行尾？
   对第二条，子进程从 `user/syscall.S` 的 `user_exec_path_args` 收到 `-1` 时，为什么由该子进程
   报错退出？父 shell 如何确认自己可以继续读命令？

## 6. 首轮草稿审查（2026-11-19）

以下结果对应首轮代码快照。隔离 user 构建通过，worker 的 `argc`、`argv` 各有一个未使用参数告警。真实 QEMU 中
`/bin/worker` 打印欢迎语后回提示符，`/bin/worker red blue` 在读行阶段报 invalid line，
`exitnow` 回显后直接退出且无 bye。证据在 `.cache/batch32-review-b2d0vg2u/`。

- 读行下界改为 `0x20` 以允许 ASCII 空格。有效行在内层循环之后补一次 NUL。
- 分词时跳完空格再判断是否存在第 5 个词。词长超过 63 或确有第 5 个词时，整行报一次错且零 fork。
  首词长度在扫描时单独记录为 `path_length`。
- 零词先继续循环，之后只保留一处精确的 `exit` 判断。首词长度必须为 4，另有参数时报 invalid line，
  单词 exit 输出 bye 后退出。子进程传 `command_argv[0]` 和 `path_length` 给 20 号。
- worker 保留欢迎语并从 `argv[1]` 起逐项输出，检查 stdout 写入结果。新加的内核入口注释与
  真实寄存器顺序不符，20 号实际为 a0=0、a1=argc、a2=argv，内核实现相比 B31 验收只改了注释。

本轮为草稿集中反馈，正式 P0 留到上述功能修正后。

## 7. 第二轮草稿审查（2026-11-19）

隔离 user 构建无告警。真实 QEMU 中，`/bin/worker red blue` 只输出 `red` 后回到提示符，
带前导空格的同一命令及 `exit later` 都报 exec failed，` exit ` 正常输出 bye。
证据在 `.cache/batch32-review-8fmh04ok/`。

- `user/main.c` 的 `main` 已放行 ASCII 空格，在分词时检查词数和词长，保存首词长度，先处理零词。
  子分支向 20 号调用传路径时，路径地址还需改为 `command_argv[0]`，以跳过首词前的空格。
- `user/main.c` 的 `main` 应先用首词长度和内容识别 `exit`，再在分支内检查词数。
  `exit later` 由父 shell 报一次 invalid line 并继续，零 fork。
- `user/worker.c` 的 `main` 已正确计算参数长度。worker 应先输出欢迎语，再输出每个参数的前缀、
  字符串和换行。只有实际写入长度不等于请求长度时，worker 才返回 1。
- 工作区实验函数 `lib/string.c` 的全局 `strlen` 将指针与 NULL 比较，无法在字符串的 NUL
  终止字节停下。该文件当前只进内核链接，worker 的本地计数算法已经够用。

本轮为草稿复查，固定 P0 尚待上述修正后的正式版本。

## 8. 三题原答与集中澄清（2026-11-19）

### 作者原答

> cpu使用的是子进程自己的页表，指向的依然是父进程的物理页

> path_length是去掉空格之后的纯字符串长度，command_argc是总共多少项参数，第一个表项是路径user/worker，第二个是amber,第三个是blue,只会输出amber和blue两个参数行

> 后续的字符分割会处理空格，可以等到时候再做处理和判断是否丢弃报错，而且不可以把剩余字节丢给下一个命令，而且因为此时已经切换到了子进程，不能把错误丢给依然在运行的父进程，不然父进程子进程都报错就没法运行了，并且父进程也无法得到-1,最后一点是父shell会不断循环确认waitpid == pid，等到为真就重新取回cpu使用权

### 集中澄清

1. 子进程从 fork 返回至 U 态时，CPU 使用子进程自己的根页表。`kernel/task/user_task_create.cpp`
   的 `tinyos::user_task::fork` 为子进程复制用户地址空间。父、子进程的 `command_argv[1]`
   可以保存同一个虚拟地址数值，CPU 经各自根页表访问两份不同的物理页。
2. `user/main.c` 的 `main` 对示例设置 `path_length = 11`、`command_argc = 3`，
   三个表项分别是 `/bin/worker`、`amber` 和 `blue`。第一个长度只统计首词的内容字节。
   `user/worker.c` 的 `main` 应在欢迎语后输出 amber 和 blue 两条参数行。
3. `user/main.c` 的 `main` 在读到第 128 个字节时标记当前行无效，随后继续从 stdin 取字节，
   直到本行的结束符。shell 可以等到行尾才报错，但此时直接跳过分词和 fork。这样第 128 字节之后
   尚未读走的尾部也归这一行。缺失路径则到达子进程的 exec 调用。`kernel/task/syscall.c` 的
   `syscall_handle` 把 `-1` 写进该子进程的现场，子进程报错并通过 `user/syscall.S` 的
   `user_exit` 以 127 退出。父 shell 在获得 CPU 时间时调用 `user/syscall.S` 的 `user_waitpid`，
   未等到结果时让出 CPU 并稍后重试。`kernel/task/syscall.c` 的 `syscall_waitpid` 完成结果写回和
   收集后返回匹配的子任务 ID，父 shell 随后结束等待循环，继续读下一行。

归档时静态核对发现，shell 已改用首词地址调用 20 号。当时带参数的 exit 分支打印错误后还会退出，
worker 只在无额外参数时输出欢迎语。作者随后修正这两处，第 9 节记录复验。

## 9. 全部固定 P0 验收（2026-11-19）

作者让 `user/main.c` 的 `main` 在带参数的 exit 分支报错后继续读命令，并让
`user/worker.c` 的 `main` 在参数循环前统一输出欢迎语。验收前，协作者给带参数 exit 的
错误消息补上换行，使下一条提示符从新行开始。以下结果对应补完后的正式源码。

| 验证项 | 实测结果 |
| --- | --- |
| 构建与 ELF | kdbg 和 user 两种配置完整构建，均无告警或未定义符号。两个用户镜像各有两个非空 LOAD。shell 的 RX 为 1612 字节，RW 为 128 字节 BSS。worker 的 RX 为 458 字节，RW 为 47 字节。两份 RX 均在一页内。 |
| 宿主与旧进程回归 | 137 个宿主用例、3 项 CTest 均通过。旧 `make CONSOLE_MODE=user test-process` 的 27 项全部通过。 |
| 正式 shell 串口 | 隔离 QEMU 会话的 15 项检查通过。第 4 节的 10 组输入输出逐字节匹配，共观察到 10 次提示。全空格行、第五个词、64 字节词、128 字节行和带参数的 exit 均未创建子进程。63 字节参数完整输出。 |
| 前台命令和参数 | shell 成功 fork 4 次，子进程发出 4 次 20 号调用。内核完成 3 次镜像替换，并拒绝 1 次缺失路径。三个 worker 的入口 `argc` 依次为 1、3、2，`argv[0]` 均为 `/bin/worker`。其余表项、表尾 NULL 和 16 字节栈对齐符合约定。 |
| 等待和退出 | 父 shell 4 次按对应子任务 ID 收集，退出状态依次为 0、0、127、0。每次收集后父 shell 才输出下一条提示符并读取下一条命令。最终 shell 退出 0。 |
| 资源 | 运行期申请 42 页、释放 48 页。8 份私有地址空间均在内核根下释放，各自根页最后归还。可用页比启动 shell 后净恢复 6 页，打开文件对象池归零，随后观测到两次 S 态 idle 定时器中断。 |

基础验证汇总在 `.cache/batch32-final-build-tm61ObrT/results.json`。
正式 QEMU 结果、逐场 UART 内容及采样计数在 `.cache/batch32-final-shell-cSQMcvAb/results.json`，
串口原始记录在同目录的 `qemu.log`。两组验证的 108 个正式源文件均保持一致。QEMU 在隔离树
链接观察器记录系统调用和页分配，正式 shell、worker 及内核算法使用原样源码。

B32 功能、固定 P0 和三题全部收尾。`lib/string.c` 中的 `strlen` 仍是第 7 节记录的独立草稿，
当前正式调用路径没有使用它。
