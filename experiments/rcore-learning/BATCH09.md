# BATCH09：定时器中断与用户态抢占

位置：第三章，协作调度之后；第四章地址空间之前。
2026-10-17：作者已完成 timer.rs 三函数和 timer trap 分支并移除guard，功能验收通过。
终端timeout差异已定位为开发版QEMU的DTB节点与旧固件关机路径不兼容；
Makefile默认固定系统10.2.1后，原运行命令正常退出0，详见第11节与ENVIRONMENT.md。
以下保留本批原始实现要求，供复习。

## 1. 这次解决什么

第八批里，一个应用不 yield/exit，另一个就无法运行。
本批让时钟触发中断，内核主动暂停当前应用，再选一个 Ready 任务。
复用你已经写好的 TaskManager、TaskContext 和 __switch，不重写调度策略。

两个用户应用现在各打印三轮 compute 日志，每轮之间执行有限的纯指令循环，
没有调用 yield。它们最终仍会 done/exit，所有任务退出后关机。
循环由协作者提供，只负责制造可被抢占的计算过程，不引入新的系统调用。
保留 yield 接口，主动让出仍可使用；默认演示专门观察 timer 抢占。

本批范围是单 hart、仅用户态可被 S timer 抢占。
不实现内核抢占、嵌套 S trap、优先级、Blocked/sleep、浮点/向量现场或页表。
这些范围不会因加上 timer 自动获得；尤其共享任务表仍依赖内核执行期间没有嵌套中断。

## 2. 时钟与截止时间

| 项目 | 当前配置 | 含义 |
| --- | --- | --- |
| time CSR | u64，持续递增 | 时间计数器 |
| CLOCK_FREQUENCY | 10,000,000 ticks/s | QEMU virt 的时基，不是 CPU 主频 |
| TICKS_PER_SECOND | 100 | 目标每秒100次 timer 事件 |
| TIMER_INTERVAL | 100,000 ticks | 约10ms的间隔 |
| SBI set_timer 参数 | 当前 time + TIMER_INTERVAL | 下一事件的绝对截止时间 |

协作者用当前 QEMU 导出 DTB，实际读取 `/cpus` 的 timebase-frequency=10000000。
DTB 在 `.cache/batch08-before-timer-p1edw15w/virt.dtb`。
为保持本批范围，先用明确的平台常量；固定官方 ch3 已通过 device_tree 模块从 DTB 动态取频率，
这里没有声称照搬其全部设备树实现。换平台需要重新核对时基。

set_timer 不能只传100000：它接收绝对时间，不是“从现在起等待多少 ticks”。
它是一次性的截止事件，因此每次 timer 处理都要设置下一次。
成功设置未来截止时间也会清除 pending timer 状态。

当前固件通过机器态时钟处理把事件转发成 S timer pending，再由硬件在满足条件时陷入 S。
内核通过 SBI TIME 请求固件设置时钟；没有直接在 S-mode 写机器态比较器。
这与之前的用户 syscall 不同：SBI 调用是 S→M，用户 ecall 是 U→S。

这是一版简单的时钟轮转：每次事件按处理时刻的 now+interval 重新设置。
yield/exit 不重置这个截止时间，也不保证每个新切入的任务都从零获得完整10ms。
内核处理、日志及主机调度都会影响实际可观察时序；不要用输出次数推断精确CPU时间。

## 3. STIE、SIE、SPIE 分别控制什么

| 状态 | 本批如何处理 |
| --- | --- |
| sie.STIE，bit5 | 启用 S timer 这一中断来源 |
| sstatus.SIE，bit1 | 实际在 S-mode 执行内核时保持0，避免 S 嵌套 trap |
| sstatus.SPIE，bit5 | 初始用户现场设1；sret 时 SIE←SPIE，SPIE←1 |
| sstatus.SPP，bit8 | 初始用户现场设0，使 sret 去 U-mode |

同为 bit5 的 STIE 和 SPIE 位于不同 CSR，不能混为同一开关。
在 U-mode，S 级中断不受 SIE 的值屏蔽；仍需 STIE、委托以及 pending 等条件满足。
所以不能将“SPIE=1”解释为 U-mode 接收 timer 必需的总开关。

进入 S trap 时，硬件令 SPIE←SIE、SIE←0。
__restore 在执行 sret 前会把保存的 sstatus 写回，那个保存值也必须保证 SIE=0，
否则尚在内核恢复寄存器时就可能遭遇 S timer，而我们的栈交换入口只支持来自 U 的 trap。
初始现场已接为清 SPP/SIE、置 SPIE；后续现场由入 trap 后的硬件状态保存，SIE 也是0。
sret 执行后 SIE 才会按 SPIE 变为1，此时 CPU 已返回 U-mode。

因此本批不要执行 `csrsi sstatus, 2` 给内核开启 S 全局中断。
设置 sie.STIE 并保持 S-mode 的 SIE=0，才能继续使用现有 U-only trap 结构。

## 4. 已经接好的模块

| 文件 | 职责 | 作者完成点 |
| --- | --- | --- |
| os/src/timer.rs | 时基、读取 time、重新设定截止、启用 STIE | A/B/C 三个函数 |
| os/src/trap/mod.rs | 区分 ecall 与 S timer | D：timer 分支 |
| os/src/sbi.rs | TIME 探测、set_timer返回值检查 | 已提供 |
| os/src/main.rs | 任务准备后 timer::init，再 run_first | 已接好 |
| os/src/trap/context.rs | 初始 SPP/SIE/SPIE | 已接好 |
| os/src/task/mod.rs | yield/timer 共用挂起转换，分别打印原因 | 已接好 |
| user/src/bin/hello.rs、hello_peer.rs | 无 yield 的有限计算 | 已提供 |

作者上批的 manager.rs、TaskContext 构造、switch.S 和 trap.S 都保持原实现。
原始第八批代码与 Makefile 保存在 `.cache/batch08-before-timer-p1edw15w/`。
没有安装工具或添加依赖，也没有为用户开启 rdtime 权限。
演示循环是固定次数的减一/分支，不要求用户访问 time CSR，避免暗中依赖 scounteren 配置。

启动顺序：

```text
clear_bss
  → trap::init：SIE=0、sie=0，安装 stvec
  → load_apps / prepare_app / task::init
  → timer::init：确认 TIME 可用 → 设置首个截止时间 → 启用 STIE
  → task::run_first → __switch → bootstrap → __restore → sret → U-mode
```

不能把 timer::init 放在 trap::init 前面，否则后者又会把 sie 清掉。
TIME 不可用或编程失败时，启动接线打印错误并失败关机，不进入用户应用。

## 5. 作者实现 A/B/C

### A. read_time() -> u64

声明一个 u64 局部值，通过内联汇编 `csrr {value}, time` 读取并返回。
操作数使用 `value = out(reg) value`，选项可用 `options(nomem, nostack)`。
把汇编包在 unsafe 中：这里依赖当前 RV64 S-mode 有权读取 time，当前固件已经提供该权限。
读出的数字单位是 ticks，不是毫秒，也不是当前任务运行了多少条指令。

### B. set_next_trigger() -> Result<(), &'static str>

1. 调用 read_time，取得 now。
2. 用 `now.checked_add(TIMER_INTERVAL)` 计算 deadline；溢出返回 Err。
3. 将 deadline 传给 `crate::sbi::set_timer(deadline)`，返回它的 Result。

已有 SBI 包装调用非 legacy 的 `sbi_rt::set_timer(u64)`，检查 SbiRet.is_err()，
并转换成 Result。不要忽略错误，也不要调用与用户 syscall 同名的调度接口。
可以用 match 处理 checked_add，或用 `ok_or("timer deadline overflow")?`。

### C. enable_timer_interrupt()

在 unsafe 内使用 `csrs sie, {mask}`，将 SIE_STIE 作为 `in(reg)` 输入。
csrs 将掩码里的位设1，保留 sie 的其他位；这里只开启 STIE。
移除 `let _ = SIE_STIE;` 占位即可。

注意 SIE_STIE 的值是32。CSR立即数版本的掩码只有5位，不能直接写 `csrsi sie, 32`；
使用寄存器输入即可，也不要误写成修改 sstatus。
init 已保证 B 成功后才调用 C，不能在发生编程错误后仍把中断来源打开。

## 6. 作者实现 D：timer trap

RV64 的 scause 最高位表示是否为中断，低位表示原因编号。
常量已经接好，匹配的是完整值：

```text
用户 ecall：                0x0000000000000008
Supervisor timer interrupt：0x8000000000000005
```

仅匹配5会混淆同步异常，不能识别 S timer。
在 SUPERVISOR_TIMER_INTERRUPT 分支填写：

1. 调用 `crate::timer::set_next_trigger()`。
2. 若 Err，打印错误原因并 `crate::sbi::shutdown(true)`。
3. 成功后调用 `crate::task::preempt_current_and_run_next()`。

先重新设置截止再切走，否则旧任务的处理函数一旦暂停，下一事件的设置也会被拖延。
切回后正常结束分支，沿函数末尾返回原 context_pointer。
删除这处 todo，timer 分支不需要另造一份 TrapContext。

| 路径 | sepc | 保存的用户 a0 |
| --- | --- | --- |
| 用户 ecall | 加4，跳过已经处理的 ecall | 写 syscall 返回值 |
| timer 中断 | 保持原值，它就是中断返回应继续执行的位置 | 保持原值，没有 syscall 返回值 |

不要把 ecall 分支的 `sepc += 4` 或写 x[10] 复制进 timer 分支。
中断可能打在循环的任何指令边界，擅自加4会跳过本应执行的指令。
带压缩指令的程序也不能靠固定加4猜测下一条。

## 7. 完整执行与恢复路径

```text
app0 计算，无 yield
  → 时钟到期，经固件产生 S timer pending
  → 硬件 U→S：scause=中断标志|5、保存 sepc、SIE清0、跳 stvec
  → __alltraps 保存 app0 TrapContext
  → timer 分支重新设定下一事件
  → preempt_current_and_run_next
    → app0 Ready，app1 Running，current=1
    → __switch 保存 app0 内核续点，恢复 app1
  → app1 执行，随后也可被 timer 抢占
```

app0 再次选中后，恢复它的 TaskContext，先续接暂停的内核 timer 处理函数，
handler 返回 app0 的 frame 地址，再由 __restore 恢复它的用户现场，sret 回用户计算。
__switch 不保存 CSR；不要切回后再从真实 sepc/scause 读取 app0 原来的状态。
原状态来自 app0 栈上的 TrapContext。

借用规则仍按第八批：管理器引用在 __switch 前结束。
当前内核不会被这个 S timer 嵌套打断，才能继续用这份单 hart 静态管理器。
只有一个 Ready/Running 任务时，timer 可以选回自己，包装跳过实际 __switch。
exit 状态和累计失败语义沿用第八批，已经 Exited 的任务不会再运行。

## 8. 完成与验收现象

完成四处 TODO 后，移除 timer.rs 的 compile_error，保留检查和断言。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
timeout 8s make run
echo $?
```

应看到 timer interval=100000、task start、两个应用各自三轮 compute 日志，
以及 `[task] timer 0 -> 1`、`[task] timer 1 -> 0`。
在任一应用 done 前应已经发生跨任务抢占，且默认演示没有 `[task] yield`。
两个应用各 done/exit0 一次，最后 all applications exited，QEMU 正常退出0，无panic。

例如某次运行的一部分：

```text
[user0] compute round 0
[task] timer 0 -> 1
[user1] compute round 0
[task] timer 1 -> 0
...
[kernel] application 1 exited: code=0
[task] exit 1 -> 0
[user0] done
[kernel] application 0 exited: code=0
[kernel] all applications exited
```

精确抢占次数、轮次交错及退出先后不固定，不要求逐行复现这个例子。
循环有固定工作量，耗时随主机变化；默认8秒用于本机最小观察，不是实时性保证。

## 9. P0 验证与预检记录

两次 P0 由协作者承担，作者新增测试代码为零，不追加故障/寄存器矩阵：

- 正常：默认 QEMU，两应用无 yield，双方在完成前均发生真实 S timer 跨任务抢占；
  每份 frame/sp/sepc 归属正确，保存SPP/SIE为0，处理时实际SIE=0、STIE=1；
  timer 调度前后原 frame 的34个字段完全不变，运行状态恰一Running；
  两应用各三轮、各退出一次，最后全Exited，退出0。
- 拒绝：仅临时副本注入 SBI timer 编程失败，init 返回 Err；
  实际SIE/STIE仍为0，没有 task start/用户输出，诊断后失败关机退出255。
  这验证设置失败不会继续启动；不要求作者另写错误注入接口。

2026-10-16，`.cache/batch09-preflight-p1glgvfm/` 补齐核心并增加上述观察后，
两次无警告构建，QEMU分别退出0、255，源码哈希确认作者os/user文件未被预检修改。
正常这次观察到 app0=3、app1=2 次 timer trap，这些数字只属于这次运行。
预检只验证相关路径，不宣称固定官方第三章完整构建通过。

## 10. 完成后集中讨论的三个问题

2026-10-17：三题已作答并集中复核。下面保留初答对照，参考解答按当前实现与架构语义修正。

1. CLOCK_FREQUENCY、TIMER_INTERVAL 与 set_timer 的 deadline 分别是什么？
   为什么要用 now+interval，且要在 __switch 前重新设置？

   作者初答（2026-10-17）：

   - CLOCK_FREQUENCY＝时钟频率，TIMER_INTERVAL＝时间间隔，deadline＝"下一个进程运行最后时间节点"，
     now+interval 是为了"方便计算"。

   参考解答：

   - 三个量的区别是"速率 / 差值 / 坐标"：
     CLOCK_FREQUENCY 是 time CSR 的递增速率（ticks/s，QEMU virt 为 10,000,000，来自 DTB 的
     timebase-frequency，不是 CPU 主频）；TIMER_INTERVAL 是从本次 now 到计划的下一事件的差值，
     单位为 ticks（CLOCK_FREQUENCY / TICKS_PER_SECOND = 100,000 ticks ≈ 10 ms）；
     deadline 是绝对的 time 计数截止值。time 达到它时，定时事件到期；CPU 何时进入 S-mode，
     还受中断使能与当前特权级等条件影响。
     本项目按 hart 设置时钟，yield/exit 可能在 deadline 之前就切换任务，因此 deadline
     不专属于下一任务，也不是该任务保证运行到的终点。
   - 使用 now+interval 是接口语义要求：set_timer 接收绝对时刻，而本项目需要从当前时刻
     再等待一个间隔。例如 now=2,000,000、interval=100,000，就设置 deadline=2,100,000。
     直接传100,000会把它解释成绝对截止值；如果 now 已超过该数，它已经过期。
   - 每次到期后不会自动安排下一轮；已经置位的 pending 也不会因进入 trap 自动清除。
     应先设置未来 deadline，清除本次 pending 并安排下一事件，再调用 __switch。
     因为切换会挂起当前内核调用链，若把设置操作放在切换之后，它要等当前任务续接才能执行；
     新任务返回 U-mode 时还可能因旧 pending 立刻再次陷入，造成连续中断，而非"再也不会被抢占"。

2. ecall 与 timer 为什么都进入 __alltraps，却只有 ecall 要推进 sepc、写返回值a0？
   timer 后恢复时，TaskContext 与 TrapContext 各完成哪一步？

   作者初答要点（2026-10-17）：

   - 将 sepc 理解为"特权状态异常计数器"，把继续执行与"本次没出现错误"联系起来；
     注意到 ecall 需要 a0 返回值，而 timer 不应推进 sepc 或写 a0。

   参考解答：

   - sepc 保存的是指令地址，PC 指程序计数器，不是异常发生次数。
     两者都经 stvec 进入 __alltraps，但 ecall 是同步异常，保存的 sepc 指向 ecall 自身。
     本项目在分发 syscall 前先将保存的 sepc 加4，使可返回调用处理结束后继续下一条指令；
     否则 sret 后会重复执行同一条 ecall。+4 来自 ecall 固定为32位，与是否启用 C 扩展无关。
     即使 syscall 返回 -1，请求也已经处理，仍应跳过 ecall。
     这里应说"调用请求已处理"，不应说"ecall 是正常执行完成的指令"：架构上 ecall
     触发同步异常，不计为退休指令。本项目约定返回值放 a0，因此写保存现场的 x[10] 槽位。
   - timer 是异步中断，sepc 已给出应恢复执行的位置，应保持原值；随意加4会跳过需要执行的
     指令，遇到压缩指令还可能落到错误边界。中断没有 syscall 返回值，用户 a0 也必须保留。
     是否修改 sepc 取决于 trap 的具体语义和返回策略，不能按"有没有错误"判断。
     例如修复缺页后若要重试原来的访存指令，通常也保持 sepc。
   - 恢复时两者的分工：TaskContext 保存/恢复内核执行续点（ra、sp、s0-s11 等，由 __switch
     在任务间切换）；TrapContext 保存/恢复用户现场（sstatus、sepc、x 寄存器、用户 sp，
     由 __restore 写回 CSR 并 sret）。timer 抢占切回后，内核先续接被挂起的 timer 分支，
     返回原 TrapContext 指针，再由 __restore 恢复用户现场回到 U-mode。
   - 本项目处理函数运行在 S-mode，有权读写 sepc；trap 来自 U 还是 S 不是决定是否 +4 的理由。

3. STIE 与 SIE/SPIE 有什么区别？为什么能保持 S-mode 的 SIE=0，仍抢占 U-mode？
   若内核处理期间也允许 S timer，我们现有栈交换入口会缺少什么支持？

   作者初答（2026-10-17）：

   - "因为Smode是下一个要执行的，而不是被打断的。"

   参考解答：

   - 三者处在不同层次：sie.STIE 是"来源使能"，决定 S timer 这一类中断是否允许；sstatus.SIE
     是"S 模式执行期间 S 级中断的全局开关"；sstatus.SPIE 是 SIE 的存档位（进入 S trap 时 SIE→SPIE，
     sret 时 SIE←SPIE，SPIE←1）。STIE 与 SPIE 同为 bit5 但属于不同 CSR，不能混为一谈。
   - 能保持 SIE=0 仍抢占 U-mode，是因为 SIE 只屏蔽"S 模式执行时"的中断；在 U-mode 下，
     S 级中断不受该位屏蔽。只要 sie.STIE、固件在 mideleg 中的委托、以及 pending 满足，
     timer 中断就会从 U 陷入 S。依据是 CPU 当前的特权级和中断使能规则，不是哪个模式"下一个执行"。
     进入 S 后硬件把 SIE 清0且内核不再打开，避免内核执行期间嵌套 S timer 中断。
     SIE=0 不能阻止内核非法指令、访存错误等同步异常，也不屏蔽更高特权级的中断，
     因此不能笼统说"不会出现嵌套 trap"。本批仍不支持这些内核异常的嵌套处理。
   - __alltraps 第一条指令无条件执行 csrrw sp, sscratch, sp，其前提是来自 U-mode：
     此时 sp 是用户栈指针，sscratch 保存本任务内核栈顶 K；交换后 sp=K、sscratch=用户sp，
     再将 sp 减272建立 TrapContext。若在这一 S-mode 处理阶段再次 trap，无条件交换就会把
     用户 sp 当作内核栈使用，破坏现场。
     支持 S-mode 嵌套来源需要在入口汇编中区分 sstatus.SPP、保全寄存器并选择正确的栈路径，
     还需管理嵌套栈帧与共享任务表的同步，例如在访问共享状态时屏蔽相应中断。
     不能仅在现有 Rust handler 中加一个分支，就认为支持了内核抢占。


## 11. 作者实现验收与关机差异（2026-10-17）

作者三个timer函数与timer trap实现正确，独立只读复核未发现功能错误。
实际make run无警告构建，两个应用无yield仍被timer抢占，单剩一个任务时timer选回自己，
两个应用各完成三轮并退出；系统QEMU10.2.1的普通输出与伪终端运行均正常退出0。

`.cache/batch09-author-review-yjy0gr0n/` 保留作者算法，仅加既定P0观察：
唯一Running、frame/sp/sepc归属、保存SPP/SIE0、实际SIE0/STIE1、timer前后全34字段保持一致，
正常场景退出0；临时注入SBI编程Err时不启动任务、SIE/STIE保持0，失败关机退出255。
作者os/user源码哈希前后相同，未代改核心或扩展作业。

作者最初贴出的日志在all applications exited后被timeout终止，返回124；
这一现象已通过其交互式PATH选择的QEMU11.1.50开发版复现，而不是忽略为正常关机。
开发版把 `/soc/test@100000` 改为 `/soc/syscon@100000`；
同一内核和固件，仅将开发版DTB该节点名改回test，便能退出0。
这是当前旧固件关机路径的环境兼容点，不影响timer实现正确性的结论。

项目Makefile默认改用 `/usr/bin/qemu-system-riscv64`（10.2.1），支持显式QEMU覆盖。
模拟作者开发版优先的PATH后，原 `timeout 8s make run` 已实际选系统版并退出0。
不修改全局PATH、不升级固件、不把临时DTB修改变成作者作业。
第10节已记录原三题初答与修正版；完成本批梳理后进入第四章地址空间。

参考：[教程抢占式调度](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter3/4time-sharing-system.html)，
[RISC-V S-mode 中断与状态寄存器](https://docs.riscv.org/reference/isa/priv/supervisor.html)，
[ECALL 与保存的返回地址](https://docs.riscv.org/reference/isa/priv/machine.html)，
[SBI TIME 规范](https://github.com/riscv-non-isa/riscv-sbi-doc/blob/v2.0/src/ext-time.adoc)。
代码仍对照固定ch3提交 `9ca4aedb66f626dcc72f835a5946742a82b61c7c`，
实际本地API依据缓存的sbi-rt0.0.2，不升级依赖或照搬后续章节。
