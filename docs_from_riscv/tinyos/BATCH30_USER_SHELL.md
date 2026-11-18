# BATCH30 用户态 shell

日期为 2026-11-18。shell 和固定 P0 已通过，原三题已归档，本批收尾。见第 9—10 节。

## 1. 目标与可观察结果

把 `user/main.c` 的单键演示改成一个最小 shell。它从 stdin 读一行，在子进程里执行路径，父进程收集退出结果后再显示提示符。输入 `exit` 时由 shell 自己退出。

```text
tinyos> /bin/worker
Hello from TinyOS worker.
tinyos> /bin/missing
[shell] exec failed
tinyos> /bin/worker
Hello from TinyOS worker.
tinyos> exit
[shell] bye
```

`/bin/worker` 已经用继承的 stdout 输出，这批直接复用。范围为一个前台命令，整行是一个路径或内置 `exit`。参数、引号、路径搜索、行编辑、后台任务、管道和重定向留待后续。默认 `kdbg` 继续启动内核控制台，`CONSOLE_MODE=user` 启动 shell。

## 2. 读一条有界命令

- 用 `user_write_fd(TINYOS_STDOUT_FILENO, ...)` 打印 8 字节提示符 `tinyos> `。用 `user_read(TINYOS_STDIN_FILENO, &byte, 1)` 读；返回 `-2` 时 `user_yield()` 再读。正长度读取期望得到 1，其他结果走 I/O 错误处理。
- 路径容量为 127 字节，与现有内核上限一致。只保存已收到的字节和长度。可以用 127 字节数组，传显式长度，无需追加 NUL 或调用字符串库。写入前检查数组边界。
- 普通输入接受 `0x21` 到 `0x7e` 的可见非空白 ASCII 并原样回显。遇到空格、其他控制字节或第 128 个路径字节时，丢弃整行，继续读取到行尾才报 `[shell] invalid line\n`，然后重新提示。被丢弃的行即使前缀是 `/bin/worker` 也没有执行机会
- CR 和 LF 都结束一行，并向 stdout 输出一个 `\n`。CR 后只吞掉紧接的一个 LF。碰到其他字节时先清除该标志再正常处理。这个标志要跨命令执行和等待保存，比如 CR 结束命令后，LF 可能要等到 worker 退出才被 shell 读到。
- 空行只重新提示。长度恰好为 4 且字节完全等于 `exit` 时，打印 `[shell] bye\n` 后返回 0。其余非空有效行作为路径传给 `user_exec_path`，绝对路径与登记规则由既有执行接口检查。

可用 `size_t length`、`bool discard_line`、`bool skip_lf` 表达输入状态。结束一行时重置长度和丢弃状态，CRLF 标志按上述规则单独管理。除提示符、回显和换行外，诊断写 stderr，已有 stdout 写入的返回长度检查可以复用。诊断尽力输出，避免诊断失败触发自身。

## 3. fork、exec 和等待

一行完整路径触发一次 `user_fork()`。

| fork 返回值 | 行为 |
| --- | --- |
| `< 0` | shell 报 `[shell] fork failed\n`，重新显示提示符。 |
| `0` | 子进程调用 `user_exec_path(line, length, 0)`。调用只要返回，子进程就写 stderr `[shell] exec failed\n`，随后 `user_exit(127)`。 |
| `> 0` | 父进程保存这个公开 Task ID，等待并收集该子进程，然后重新提示。 |

子进程拿到 fork 时的私有用户页副本，可以用自己的 `line` 和 `length` 调用 exec。成功后按路径进入新镜像，继承来的 0/1/2 继续有效。用 `SPAWN` 会走另一条进程创建路径，本批练习 `fork → exec → waitpid` 的组合。

父进程使用栈上的 `TaskExitResult result = {0};` 和 `user_waitpid(child_id, &result)`。返回 `SYSCALL_ERR_WOULD_BLOCK` 时 `user_yield()` 后继续等同一个 ID。返回 `child_id` 表示已收集，`result.reason` 和 `result.status` 才是结束信息。`TASK_RESULT_REQUESTED` 后直接重新提示，子进程已经报告 exec 失败，父进程也无需重复报告 127。`TASK_RESULT_FAULT` 时可写 `[shell] child fault\n` 再重新提示。

等待期间父进程暂停读取 stdin，完成收集后才接收下一条命令。`exit` 也只会在提示符处被读取。boot shell 已登记为 reaper，内核要求它退出前收集所有孩子。因此等待时若遇到意外 `-1` 或 yield 失败，只报一次错并留在让出 CPU 的循环里。`-1` 不代表已经收集，不能继续接命令或直接退出。输入、提示写入等无待收集子进程时的 I/O 失败，可向 stderr 尽力诊断后返回 1。

`/bin/demo` 仍是已登记的程序，执行它会启动一层子 shell。这时外层照常等待，内层 `exit` 后回到外层。现有 Task 表只有 5 槽，因此继续嵌套到槽满时由普通 fork 失败分支处理。本批验收用 `/bin/worker`，无需另写嵌套演示。

## 4. 实现范围与顺序

作者只改 [user/main.c](/home/asus/tinyos/user/main.c)。原用户接口、worker、启动方式和链接布局已经具备。

1. 先实现提示符、有界输入和 `exit`，有了完整的路径后再进入 fork 分支。
2. 接子进程 exec 路径，确认失败分支最后必到 `user_exit(127)`。接父进程等待，实际保存 fork 返回的 ID。
3. 保留有实际用途的可写数据，例如让 `static char` 提示符继续承担输出，保证 ELF 的 RW LOAD 非空。链接脚本限制代码和只读数据合计一页。用户程序使用 freestanding C11，直接比较 `exit` 的四字节即可。

日常运行命令

```sh
make CONSOLE_MODE=user qemu
```

在提示符下依次输入 `/bin/worker`、`/bin/missing`、`/bin/worker`、`exit`。用 `Ctrl+A` 再按 `X` 退出 QEMU。

## 5. 固定验收范围

P0 由协作者承担。QEMU 逐字节控制串口输入，避免快于设备消费速度的粘贴造成假失败。

| 优先级 | 场景与预期 |
| --- | --- |
| P0 | 两个 profile 构建和原宿主套件通过。实际 shell ELF 两个 LOAD 均非空，RX 在一页内。 |
| P0 | user 模式执行两次 `/bin/worker`，其中间执行一次 `/bin/missing`。两次 worker 各输出一条消息并退出 0，缺失路径只出一条 exec 失败诊断且子退出 127。三次执行均按返回的子 ID 完成等待，父 Task/ID/根/FD 保持，每次收集后才重新提示。 |
| P0 | 同一 QEMU 负载依次发送空行 CRLF、第一次 worker CRLF、缺失路径 LF、128 个 `x` 后接 LF、第二次 worker CR、`exit` LF。空行与超长行零 fork，CRLF 只结束一次行，第二次 worker 证明可从丢弃状态恢复。最终 `exit` 只退出 shell，页资源与 FD 池恢复，随后进入 idle。 |
| P0 | 统计上述场景为 3 次成功 fork、2 次成功 exec、3 次 reap。按实际 ELF 页数核对，每份空间的页只释放一次。若 shell 和 worker 都仍为 6 页，运行期申请 30 页，累计释放 36 页，净恢复启动 shell 占用的 6 页。默认 kdbg 交互与既有进程回归保持。 |
| P1 | 四层以上嵌套带来的槽满处理、子进程 fault 或非法字节的额外组合。可选讨论。 |

需要时由协作者提供隔离观察器，使用作者提交的正式 shell 和 worker 验证。保留 B29 已验证的路径和回滚语义。

## 6. 收尾三题

1. 为什么 shell 自己直接调用 exec 后无法再显示提示符？为什么 fork 子进程能够继续读取局部 `line`？
2. `waitpid` 返回子 ID 和子退出码分别在哪儿？为什么打印下一次提示符要等到收集成功，而不是只观察到子进程已停止输出？
3. 为什么超长行要读到行尾才恢复？一条 CRLF 命令在 worker 执行完之后才读到 LF 时，哪个状态负责避免再次提交？

## 7. 首轮 main.c 审查（2026-11-18）

隔离 user 构建成功，两个 LOAD 实测为 RX 1100 字节、RW 168 字节（含 BSS）。QEMU 中 `/bin/worker` 能输出后返回提示符，`exit` 输出 bye。空行和 `/bin/missing` 都输出截断的 `[shell]` 再接提示符。以下为作者待修点。

- `write_line` 接收指针，`sizeof(&message) - 1` 在 RV64 一直给出 7。让调用者显式传字节长度，在调用点用字符串数组的 `sizeof - 1`，诊断字符串含换行。返回长度比较也随之消除有符号警告。
- 行缓冲现在为 128 字节，容量检查应对应 127 字节数组或显式的 127 上限。丢弃行后先报错并重新提示，再对未丢弃的空行直接 `continue`。当前空行会 fork 并走路径拒绝。CRLF 状态与换行判断顺序正确。
- `fork` 失败后向 stderr 尽力诊断并无条件 `continue`，否则诊断写入成功后会误用 `waitpid(-1)`。子进程调用路径 exec 时传 0，只要系统调用返回，就诊断并无条件 `user_exit(127)`，避免落到父等待。当前失败退出码为 27。
- 父等到精确子 ID 才再次提示。`SYSCALL_ERR_WOULD_BLOCK` 分支需检查 yield 的返回值，异常返回或 yield 失败按第 3 节报一次错并停在让出 CPU 的循环。当前注释下没有异常处理，错误时会反复调用 waitpid。
- 普通回显、行尾和 bye 的 stdout 写入需检查实际字节数，stderr 诊断可明确尽力。旧可写 `message` 没有引用，编译有未使用告警。可删除旧提示或把它换成实际用到的提示符，静态行缓冲已经提供可写 BSS。

此次只核对作者源码与复现几个交互分支，完整 P0 留待上述修正后。

## 8. 正常交互复查（2026-11-18）

作者已集中计算诊断字符串长度、修正 127 字节上限和超长字节的 `continue`、补空行分支，并让 exec 失败的诊断完整带换行。根据保存源码重建的 user 镜像无编译警告，RX 为 1213 字节，RW 为 127 字节 BSS。隔离 QEMU 的空 CRLF、worker CRLF、缺失路径 LF、128 个 `x` 加 LF、第二次 worker CR 和 exit LF 的串口输出逐字节匹配预期，记录见 `.cache/batch30-review-20261118/transcript-results.json`。单独发送纯 ASCII `asd` 时，当前源码回显后输出 `[shell] exec failed`。作者日志的 `invalid line` 对应输入层拒绝，具体触发字节或镜像版本未确认。

还须修正已定契约中的三处分支。fork 返回负数后，诊断写入结果不应决定是否 `continue`。exec 失败的子退出码应为 127，目前是 27。父等待的异常返回与 yield 失败应一次诊断后停在让出 CPU 的循环。另检查回显、行尾和 bye 的实际写入长度，删掉重复的 `discard_line` 分支。当前仅核对串口输出，完整 P0 尚未运行。

再次核对保存的草稿，fork 失败 `continue`、子失败退出 127 和三个 stdout 检查均已补齐。父等待中 `WOULD_BLOCK` 分支现在误用 `break`，会在尚未收集子进程时回到提示符。成功 yield 应 `continue`，只有 `waitpid` 返回子 ID 才 `break`。其他返回和 yield 失败共用一次诊断与让出 CPU 的循环。输入侧恢复行尾处理之后的 `if (discard_line) { continue; }`，避免非法字节后的有效字节继续存入或回显。外层行尾的两段相同诊断留一段。

## 9. 正式 shell 与固定 P0（2026-11-18）

作者把等待分支改成 `WOULD_BLOCK` 且 yield 成功就重试，只在 `waitpid` 返回原子 ID 后结束等待。异常分支一次诊断后反复让出 CPU。输入侧在处理行尾后检查丢弃状态，恢复到下一行才继续保存和回显字节。外层重复的 `discard_line` 检查在第一段后不可达，当前不影响验收。

| 验证 | 实测结果 |
| --- | --- |
| 构建与宿主 | user/kdbg 均通过，无警告及未解析符号。137 个宿主用例、3 项 CTest 通过。正式 shell RX 1244 字节，RW 为 127 字节 BSS。worker RX 240 字节、RW 27 字节。两个镜像均占 6 页私有空间。 |
| 正式 shell | QEMU 13 项检查通过。空 CRLF、worker CRLF、缺失路径 LF、128 个 `x` 后的 LF、worker CR 和 `exit` LF 的串口内容精确匹配。空行和超长行零 fork，6 次提示符前都没有待收集的孩子。 |
| 流程与归属 | 共成功 fork 3 次，路径 exec 成功 2 次、拒绝 1 次，收集退出码依次为 0、127、0。三次 `WOULD_BLOCK` 均继续等待对应 ID。父 Task、Process、根及 FD 身份保持，子进程共享原标准流，最后回到父 shell。 |
| 回收 | 6 份空间在内核根下释放且各自的根页最后归还。运行期申请 30 页、释放 36 页，恢复启动 shell 占用的 6 页。shell 退出 0，文件池归零，观察到两次 idle S 态中断。 |
| 原回归 | 默认 kdbg 的五次命令及 36 项观察通过。旧进程隔离回归 27 项通过，35 次释放、21 次运行期申请、净恢复 14 页、3 次 reap 无 PMM。 |

`.cache/batch30-final-20261118/summary.json` 索引各组结果，108 个正式文件哈希与当前工作区一致。正式 QEMU 使用原样的 shell 和 worker。测试观察器只在隔离副本记录 syscall、PMM 和身份状态，通过 QMP FIFO 读取。观察到 idle 后由宿主停止 QEMU。槽满嵌套和异常 wait 故障注入属于既定测试之外，本次通过的是第 5 节 P0。

## 10. 原三题原答与集中澄清（2026-11-18）

### 作者原答

> 现在单进程无法同时处理fork和shell自己接收提示符？fork出来的子进程从父进程继承了文件路径

> waitpid返回值在最后追踪到了frame->a0追到syscall_waitpid，获得了下一个id，子进程退出码在 tinyos::user_task::finish写入了.status，收集成功之后才算完成

> 超长行不丢弃到行尾截断会继续往下写撑爆当前buffer导致数组溢出，如果LF到了退出才被读到，char byte;
>           long len = user_read(TINYOS_STDIN_FILENO, &byte, 1);
>            这里会直接吃下

### 集中澄清

1. fork 后父子都可被调度。父 shell 把读取下一条命令推迟到 `waitpid` 成功，是前台命令的行为约定。若父 shell 自己 exec，它的用户镜像和栈会被替换，提示符循环随之消失。fork 子进程能读取路径，因为 fork 复制了父进程的用户内存。当前 `path` 是静态 BSS 数组，`length` 是局部变量，二者都由子进程拥有自己的副本。FD 继承另有机制，父子在各自表中持有同一个 OpenFile 的引用。
2. `frame->a0` 是 syscall 返回寄存器。`syscall_waitpid` 成功返回实际收集到的 `match_id`，对本批的精确等待，它就是父 shell 自己保存的 fork 返回值。`finish` 将正常退出码写入内核 `TaskExitInfo.status`，`syscall_waitpid` 把退出结果复制到用户 `TaskExitResult` 后才调用 `task_reap`。shell 用返回值确认收集的对象，退出码在 `result.status`。没有后续输出只能说明串口安静，收集成功后才可以复用子 Task 槽。
3. 在 `path[length++]` 之前检查 `length >= sizeof(path)`，第 128 个字节就不会写到数组外。此后保持 `discard_line` 直到 CR 或 LF，防止把同一行尾部错当成下一条命令。读到 CR 时设 `skip_lf = true`，变量声明在外层命令循环之前，因此跨子进程执行、等待和下次提示保留。`user_read` 取出一个字节，随后的 `skip_lf` 分支清标志并对紧跟的 LF 执行 `continue`，这一步才跳过再次结束一行。下一字节若是普通字符则正常处理。

### 协作者整理的修正版（不是作者重答）

1. 父 shell 继续原镜像，子进程用 fork 得到的路径副本 exec。父选择等待前台子进程，以便完成后继续同一个提示符循环。
2. `waitpid` 的返回值是被收集的子 ID，退出码由 `TaskExitResult.status` 提供。成功收集后再显示提示符，子 Task 槽也可复用。
3. 容量检查挡住越界，丢弃至行尾维护命令边界；跨外层循环的 `skip_lf` 吞掉 CR 后紧邻的 LF。

原三题已完成集中澄清。第 9 节的验收结果继续有效，本轮更新理解归档和索引。B30 收尾。
