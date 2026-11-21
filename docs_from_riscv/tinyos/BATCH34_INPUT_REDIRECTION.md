# BATCH34 shell 的标准输入重定向

2026-11-19 交付任务，2026-11-20 完成实现、固定 P0 和三题归档，见第 7—8 节。

## 1. 为什么要让 shell 接管输入绑定

B33 验证了一个子进程可以打开 `/etc/about`，用 `dup2` 把它接到自己的 FD 0，再用
exec 替换用户镜像。当前正式 shell 只识别命令词，用户在提示符里写下 `< /etc/about` 时，
shell 还无法理解这是在选择输入来源。

B34 让父 shell 先识别输入文件的路径，由前台子进程在 exec 前调整自己的 FD 0。
目标程序仍按平常方式读 FD 0，父 shell 继续从控制台接收下一条命令。TinyOS 现有普通文件是只读内置文件，
可以直接用它们做输入重定向。输出重定向、管道、引号和转义留给后续批次。

```text
tinyos> /bin/worker --stdin < /etc/about
TinyOS builtin fs
tinyos> /bin/worker red blue
Hello from TinyOS worker.
[worker] arg: red
[worker] arg: blue
tinyos>
```

### 在 rCore 教程中的位置

按机制对照，TinyOS 已具备第 6 章面向进程的一部分文件接口，包括打开和关闭文件、每进程
文件描述符表、打开对象及普通文件的顺序读取。TinyOS 还能通过只读内置文件查找并执行
已登记的程序。第 6 章的存储后端仍待学习和实现，包括块设备、
块缓存、磁盘布局、inode 与持久文件写入。B34 使用已验证的 FD 机制，内容对应第 7 章
的标准输入重定向。独立 rCore 学习记录仍停在第 4 章结尾，这里对照的是 TinyOS 的能力。
[rCore 第 6 章目录](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter6/index.html)、
[rCore 命令行参数与标准 I/O 重定向](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter7/3cmdargs-and-redirection.html)、
[独立学习进度](../../experiments/rcore-learning/PROGRESS.md)。

### 一条重定向命令怎样执行

1. `user/main.c` 的全局 `main` 在 U 态用父 shell 的根页表读取一行，把命令词与输入路径分开。
   它先校验整行，再调用 `user/syscall.S` 的全局入口 `user_fork`。进入 S 态后，
   `kernel/task/user_task_create.cpp` 的 `tinyos::user_task::fork` 为子进程建立独立的
   用户空间和 FD 表，表中已有的槽仍引用各自原来的打开对象。父进程保留自己的表，在原镜像中等待。
2. 子进程被调度后，仍在 `user/main.c` 的 `main` 中，从 fork 的返回值 0 分支进入重定向准备。
   子进程调用 `user/syscall.S` 的全局入口 `user_open` 打开输入文件，再调用同文件的
   `user_dup2` 把该打开对象绑定到 FD 0。两个入口分别通过 15 和 21 号系统调用进入 S 态，
   当时活动的根页表属于调用子进程。21 号调用由 `kernel/fs/file_descriptor_table.cpp` 的
   `tinyos::fs::FileDescriptorTable::duplicate_to` 修改该子进程的 FD 表。绑定成功后，子进程用
   `user/syscall.S` 的 `user_close` 关掉额外的源 FD，FD 0 持有的引用让文件继续有效。
3. 子进程通过 `user/syscall.S` 的全局入口 `user_exec_path_args` 发出 20 号调用。
   `kernel/task/user_task_exec.cpp` 的 `tinyos::user_task::exec_path_args` 用子进程的旧根复制命令
   路径和参数。同文件、同命名空间中的内部函数 `exec_impl` 装载候选空间，提交时用候选空间
   替换该子进程的旧用户空间，保留子进程的 FD 表。因此，`user/worker.c` 的全局 `main`
   在新根下进入 U 态后，通过同一份 FD 表从 FD 0 读取刚绑定的文件。系统调用和 FD 的内部路径
   分别沿用 [B31](BATCH31_EXEC_ARGUMENTS.md) 与 [B33](BATCH33_DUP2.md)。
4. 父 shell 在 U 态通过 `user/syscall.S` 的全局入口 `user_waitpid` 等待对应子任务 ID。
   未就绪时沿用 B32 的让出 CPU 再重试。`kernel/task/syscall.c` 中的内部函数 `syscall_waitpid`
   将退出结果写回父进程的用户缓冲区并收集该子任务后，父 shell 再次提示。父进程的 FD 0 在
   整个过程中保持原控制台绑定。

## 2. 工程约定

| 条件 | 处理方式 |
| --- | --- |
| 命令形状 | `命令 [参数 ...] [ < 输入路径 ]`，仅独立的 ASCII 空格分词 `<` 具有重定向含义 |
| 词和行的容量 | 总行长仍为 127 字节，各词最多 63 字节。命令部分最多 4 个词，含 `argv[0]`。末尾 `< 路径` 另占 2 个词，整行临时词表最多 6 项 |
| 合法重定向 | 恰好一个独立的 `<`，前面至少一个命令词，后面恰好一个路径词，路径词本身不能是独立的 `<` |
| 传给 exec 的内容 | `argv[0]` 仍是命令路径。`argc` 只统计命令和程序参数，`<` 和输入路径另由 shell 用来准备 FD |
| 无效行 | 缺命令、缺输入路径、重复 `<`、路径之后还有词、超过容量、或 `exit` 带参数或重定向时，父 shell 写一次 `[shell] invalid line\n`，零 fork |
| 符号边界 | 嵌在词内的 `<` 仍按普通字符处理，其他字符沿用 B32。空白行只重新提示，CR/LF/CRLF 按原规则处理 |
| 重定向失败 | 子进程的打开、改绑或关闭额外 FD 任一步失败时，子进程向 stderr 写一次 `[shell] input redirect failed\n` 并退出 126。此时不进入 exec |
| exec 失败 | 重定向准备完成后，若路径 exec 返回，子进程向 stderr 写一次 `[shell] exec failed\n` 并退出 127 |

子进程使用 `user_open` 返回的实际编号。若它等于 FD 0，绑定后保留这格。若编号不同，
只有成功改绑后才关闭多余的源 FD。子进程遇到失败时沿现有退出路径关闭它仍持有的所有 FD。

## 3. 实现任务与自查

作者只修改 `user/main.c` 和 `user/worker.c`，沿用现有内核、用户接口和启动汇编。

1. **识别命令与输入来源。** 在 `user/main.c` 的 `main` 中保留读行、超长行丢弃和 CRLF 去重。
   读完有效行后，在现有就地分词中把临时容量从 4 增到 6，记录词的起点和长度。
   分词结束后识别末尾 `< 路径`，把命令部分的词数记为 `command_argc`，
   把可选的输入路径及长度单独保存。检查前者最多 4 词，并在 fork 前处理非法重定向和内置 `exit`。
   自查时输入 `/bin/worker one two three < /etc/about`，确认 6 个词能组成合法命令，
   传给 worker 的仍只有 4 项。
2. **在子进程内准备 FD 0。** 修改 `user/main.c` 的 `main` 中 `pid == 0` 分支。
   有输入路径时，子进程在调用 20 号前按 `user_open → user_dup2 → user_close` 的顺序准备好 stdin。
   `user_open` 的非负返回值是实际源 FD，`user_dup2(source_fd, TINYOS_STDIN_FILENO)` 成功返回 0，
   `user_close` 只用于原编号与 FD 0 不同时。失败时该子进程用 `user/main.c` 的文件内函数
   `write_line` 输出诊断，再通过 `user/syscall.S` 的全局入口 `user_exit` 退出 126。
   无论是否重定向，父进程仍按原有逻辑精确等待。自查时用缺失的输入文件观察 126 分支的诊断，再输入
   正常命令确认父 shell 还能读取控制台。
3. **用 worker 展示 stdin 的来源。** 修改 `user/worker.c` 的 `main`。
   当且仅当 `argc == 2` 且 `argv[1]` 完整等于 `--stdin` 时，进入复制模式。
   worker 通过 `user/syscall.S` 的全局入口 `user_read` 从 FD 0 读入 16 字节的局部缓冲区。
   每次返回正数，worker 都用同文件的 `user_write_fd` 把实际读取的字节数原样写到 FD 1。
   读取返回 0 时结束并返回 0，读取返回负值或写入不完整时返回 1。
   本演示以普通文件的 EOF 为结束条件，控制台暂空 `-2` 也按失败处理。复制模式只输出输入字节，
   其他调用继续使用 B32 的欢迎语和参数输出。保留两个应用已有的非空可写 LOAD 及一页 RX 限制。
   自查时执行 `/bin/worker --stdin < /etc/about`，预期文件内容共 18 字节，读取依次返回 16、2、0。

示例命令在 shell 里分成下列信息，两段路径长度分别计算。

```text
命令路径      /bin/worker
传给 exec     ["/bin/worker", "--stdin"]，argc = 2
用于打开的路径 /etc/about
```

## 4. 固定 P0

协作者承担 P0，运行两种构建配置、既有宿主测试与旧进程回归，然后用正式 shell 和 worker 完成一次 QEMU 会话。

| 顺序和输入 | 通过条件 |
| --- | --- |
| `/bin/worker --stdin < /etc/about` | worker 的读取依次返回 16、2、0，stdout 只写出原文件的 18 字节，子退出 0 |
| `/bin/worker --stdin < /etc/missing` | 子进程报告 input redirect failed 一次，退出 126，无 exec |
| `/bin/worker --stdin <` | 父 shell 报一次 invalid line，零 fork |
| `exit < /etc/about` | 父 shell 报一次 invalid line，零 fork，并继续提示 |
| `/bin/worker one two three < /etc/about` | 子进程成功准备输入，worker 收到路径加三个普通参数，输出原欢迎语和三个参数行，退出 0 |
| `/bin/missing < /etc/about` | 子进程改绑后进入 exec 失败分支，仅报告 exec failed，退出 127，退出路径归还改绑后的文件引用 |
| `/bin/worker red blue` | 保留 B32 的欢迎语和两个参数行，退出 0 |
| `exit` | 父 shell 输出 bye，退出 0，文件对象池归零 |

要求观察到 5 次成功 fork、3 次成功 dup2、4 次 20 号 exec（3 成功、1 拒绝）和 5 次前台收集。
子进程的收集状态依次为 0、126、0、127、0。每个重定向发生在子进程，父 shell 的控制台 FD 0
保持原绑定。父 shell 收集对应任务后才提示下一行。P1 可选验证在源编号恰好为 0 时的保险分支，
或测试重复 `<` 和路径后存在多余词的组合。

```sh
make -j4 CONSOLE_MODE=kdbg
make test
make CONSOLE_MODE=user test-process
make -j4 CONSOLE_MODE=user
make CONSOLE_MODE=user qemu
```

最后一条命令进入正式用户态 shell。输入上述命令后，可以用 `Ctrl+A` 再按 `X` 退出 QEMU。

## 5. 实现后的三道理解题

1. 父 shell 已经解析完 `/bin/worker --stdin < /etc/about`。比较在 fork 前和在子分支里
   执行 FD 0 重绑定，哪张表会变化，下一次提示符从哪里读取？结合两边各自的 FD 表说明。
2. 假设子进程的 FD 0 原先为空，`user/syscall.S` 的 `user_open` 正好返回 0，后续 `user_dup2(0, 0)`
   成功。此时如果子进程还执行 `user_close(0)`，worker 进入新镜像后读取 stdin 会发生什么？
3. 假设打开输入文件和改绑都成功，随后命令路径查找失败。哪一个进程会收到 20 号调用的返回值？
   哪个退出路径负责归还剩余引用，父 shell 怎样区分这个结果与打开输入文件失败？

## 6. 首轮功能复核（2026-11-19）

当前保存的 shell 和 worker 可以执行输入重定向的成功路径。两种配置均无告警或未定义符号，
3 项 CTest 的 139 个宿主用例及旧进程回归 27 项通过。108 个正式源在验证前后保持一致。
基础结果在 `.cache/batch34-review-build-qdw9J8LX/results.json`。

正式 user QEMU 的八组串口检查通过五组。两条无效语法、三参数重定向、普通带参数命令和 `exit`
的输出符合约定。复制模式确实读出了 `/etc/about`，但 worker 在文件字节前多打一行欢迎语。
缺失输入文件和缺失执行路径两条命令均直接回到提示符，缺少对应诊断。
逐场记录在 `.cache/batch34-review-LXVrrd9M/results.json` 和同目录 `qemu.log`。

静态核对确认以下修正点：

- `user/main.c` 的 `main` 在子进程重定向准备失败时目前返回 1。
  按第 2 节改为输出 `[shell] input redirect failed\n` 后退出 126。成功改绑后，只有临时 FD
  与 FD 0 不同时才关闭临时 FD。
- `user/main.c` 的 `main` 在 20 号调用返回时目前直接退出 126。
  按第 2 节改为输出 `[shell] exec failed\n` 后退出 127。
- `user/worker.c` 的 `main` 当前在判断 `--stdin` 前输出欢迎语。将欢迎语写入放到复制模式分支之后，
  让复制模式只输出文件字节，普通模式继续输出欢迎语和参数。

原样串口会话用于字节比对。另一个隔离副本用链接观察器采样系统调用，记录在
`.cache/batch34-review-LXVrrd9M/observed/qemu.log`。该副本观察到 5 次 fork、
3 次成功 dup2、4 次 20 号调用及 5 次收集。成功复制时读取返回 16、2、0。两条失败命令的实际退出码
分别为 1 和 126。观察器日志通过 UART 输出，文件字节以原样串口会话为准。
上述问题已修正，正式 P0 复验见第 7 节。

## 7. 全部固定 P0 验收（2026-11-20）

作者在 `user/main.c` 的 `main` 中分开了重定向失败和 exec 失败的诊断及退出码。
协作者在子进程关闭临时 FD 前补上了 FD 0 判断，并把 `user/worker.c` 的 `main` 中的欢迎语移出
整个 `argc == 2` 分支。这样复制模式只输出文件内容，普通模式仍打印欢迎语和参数。

| 验证项 | 实测结果 |
| --- | --- |
| 构建与 ELF | kdbg 和 user 完整构建均无告警或未定义符号。两份应用各有两个非空 LOAD。shell RX 2092 字节、RW BSS 128 字节，worker RX 656 字节、RW 47 字节。RX 均在一页内。 |
| 宿主与旧进程回归 | 139 个宿主用例、3 项 CTest、旧 `test-process` 的 27 项全部通过。 |
| 正式串口 | 第 4 节的 8 组输入输出逐字节匹配。worker 的复制模式读取依次返回 16、2、0，stdout 原样输出 18 字节。两条无效语法均未 fork，普通命令的欢迎语和参数行保持。 |
| 重定向和镜像替换 | 5 次 fork、4 次 open（1 次失败）、3 次 dup2 和 3 次关闭额外 FD。4 次 20 号 exec 中 3 次成功，1 次因缺失路径被拒绝。输入文件打开失败的子进程直接退出 126。缺失命令的子进程在 exec 返回后退出 127。 |
| 父进程与等待 | 父 shell 的任务身份、根页表和所有 FD 保持。父 shell 按对应子任务 ID 完成 5 次收集，状态依次为 0、126、0、127、0。每条后续提示都在收集完成后输出。最终父 shell 退出 0。 |
| 资源 | 三次改绑的打开对象都在相应子进程退出时归还，包括缺失命令的失败路径。运行期申请 48 页、释放 54 页，9 份地址空间各自最后释放私有根页。可用页净恢复启动 shell 占用的 6 页，文件对象池归零，随后观察到两次 S 态 idle 定时器中断。 |

基础验证见 `.cache/batch34-final-build-6tBu0zyK/results.json`。正式 QEMU 的 17 项检查、逐场 UART
内容和计数见 `.cache/batch34-final-shell-f8xPlKw5/results.json`，原始串口见同目录 `qemu.log`。
两组验证前后，108 个正式源文件的哈希保持一致。QEMU 使用原样 shell 和 worker，
在隔离树链接观察器记录调用和页分配，宿主通过 QMP 读取观测数据。

## 8. 三题原答与集中澄清（2026-11-20）

### 作者原答

> FD0变化，下一次提示符子进程shell从目标文件读取

> 会该怎么做就怎么做，文件没关，读错了

> 父进程，user_exit(127),返回值1是输入失败，

### 集中澄清

1. 下一次提示符和下一条命令都由父 shell 的 `user/main.c` 中的 `main` 负责。若父 shell 在 fork
   前改绑 FD 0，父表首先变化，子进程随后继承该绑定。父等待完成后会用原来那格 FD 0 读取目标
   文件。B34 将改绑放在子分支，只有子表的 FD 0 变化，父 shell 等待结束后仍从控制台读取。
2. 当 `user/syscall.S` 的 `user_open` 返回 0 时，`user_dup2(0, 0)` 保留同一格，
   不增加额外引用。`user/main.c` 的 `main` 用 `&&` 短路跳过此时的 `user_close`。
   若误在 exec 前关闭 FD 0，exec 会保留这个空槽。`user/worker.c` 的 `main` 在 U 态从 FD 0
   读取会得到 `-1`，并退出 1。源编号大于 0 时，`user_close` 只关闭多余的源槽。
3. 缺失命令路径时，`kernel/task/syscall.c` 的 `syscall_handle` 将 `-1` 写入发起 20 号调用的
   **子进程**现场。子进程回到 `user/main.c` 的 `main` 后报错，经 `user/syscall.S` 的
   `user_exit` 以 127 退出。`kernel/task/user_task_exit.cpp` 在 `tinyos::user_task` 的
   匿名命名空间中定义的 `task_destroy` 在 S 态调用 `kernel/process/process.cpp` 的
   `tinyos::Process::close_files`，归还子进程仍持有的 FD 引用。父 shell 从
   `user/syscall.S` 的 `user_waitpid` 取得被收集的子 ID，在 `result.status` 中看到退出码。
   重定向准备失败是 126，exec 失败是 127。退出码 1 则可来自 worker 已启动后的读写失败。

B34 功能、固定 P0 和三题均已完成，当前前台 shell 阶段收尾。存储后端的推进顺序见
[TinyOS 改造路线图](../TINYOS_EVOLUTION.md#rcore-第-6-章存储后端)。
