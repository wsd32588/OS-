# 第三章总结：让多个应用轮流使用 CPU

整理日期：2026-10-24。覆盖 [BATCH06](../../experiments/rcore-learning/BATCH06.md)、
[BATCH07](../../experiments/rcore-learning/BATCH07.md)、
[BATCH08](../../experiments/rcore-learning/BATCH08.md)、
[BATCH09](../../experiments/rcore-learning/BATCH09.md)。

本章的结果是：两个应用同时留在内存中，各自保留栈和执行现场；内核既能响应用户主动
`yield`，也能靠时钟中断暂停计算中的应用，选出另一个任务，之后再恢复原任务。
恢复的目标是上次暂停的位置，因此循环能够向前推进，而不是每次重新运行入口。

本章核心主线已完成，功能与集中问答记录保留在四份原批次文档中。
`get_time` 系统调用和教材扩展练习未实现；本总结不把它们补记为完成，也不新增练习。

## 1. 从单应用到分时执行，分别增加了什么

| 阶段 | 增加的能力 | 尚未包含的能力 |
| --- | --- | --- |
| BATCH06 | 两份应用镜像、两套用户栈/内核栈/初始现场同时存在 | 此时只选择一个应用启动 |
| BATCH07 | 用 `TaskContext` 和 `__switch` 暂停、续接内核调用链 | 不负责选择下一个应用 |
| BATCH08 | 状态枚举、TCB、任务管理器、`yield` 与退出后的调度 | 一直计算的应用仍能独占 CPU |
| BATCH09 | 定时器中断、时间片触发的抢占 | 不支持内核任意位置被抢占、每任务分页调度 |

这四层有明确依赖：先有可以保留的应用，再有切换机制，再有选择规则，最后增加不用
应用主动配合的触发来源。`__switch` 是切换机制，调度器决定切谁，定时器决定何时触发一次
抢占请求，不能把三者都概括为同一个“切换函数”。

第二章“复用同一装载窗口、按顺序覆盖并执行应用”的批处理循环，按作者当时选择略过。
第三章是在原有 U/S Trap 基础上增加多应用常驻，不表示这个被略过的循环已经实现。

## 2. 模块之间怎样合作

以下均指 rCore 学习子项目，不是根目录暂停中的 C TinyOS 实现。

| 模块 | 当前职责 |
| --- | --- |
| [loader.rs](../../experiments/rcore-learning/os/src/loader.rs) | 嵌入两份 flat bin，检查编号/镜像范围，复制到固定槽位，提供静态栈，构造用户初始现场 |
| [task/context.rs](../../experiments/rcore-learning/os/src/task/context.rs) | 定义 112 字节 `TaskContext`，构造空记录与首次启动记录 |
| [task/switch.S](../../experiments/rcore-learning/os/src/task/switch.S) | 保存当前内核续点、恢复下一续点，通过普通 `ret` 转交执行 |
| [task/manager.rs](../../experiments/rcore-learning/os/src/task/manager.rs) | 保存 TCB、任务状态和当前编号，按环形顺序选择 `Ready` 任务 |
| [task/mod.rs](../../experiments/rcore-learning/os/src/task/mod.rs) | 管理共享存储的短借用，调用状态转换、打印调度原因、取真实字段指针并切换 |
| [trap/context.rs](../../experiments/rcore-learning/os/src/trap/context.rs) 与 [trap.S](../../experiments/rcore-learning/os/src/trap/trap.S) | 定义、保存和恢复 272 字节用户整数现场 |
| [trap/mod.rs](../../experiments/rcore-learning/os/src/trap/mod.rs) | 区分 U `ecall` 和 S timer，采取不同的返回策略 |
| [syscall.rs](../../experiments/rcore-learning/os/src/syscall.rs) | 分发 `write=64`、`exit=93`、`yield=124`，按当前任务校验写出缓冲区 |
| [timer.rs](../../experiments/rcore-learning/os/src/timer.rs) 与 [sbi.rs](../../experiments/rcore-learning/os/src/sbi.rs) | 读 `time`，计算下一绝对截止值，通过 SBI TIME 编程，启用 `sie.STIE` |
| [main.rs](../../experiments/rcore-learning/os/src/main.rs) | 按顺序完成初始化、装载、现场准备、任务登记、定时器设置和首次启动 |
| [用户 syscall 包装](../../experiments/rcore-learning/user/src/syscall.rs) | 把调用号/参数放进约定寄存器，执行 `ecall` 并取得返回值 |

启动阶段顺序为：

```text
clear_bss
  → trap::init：SIE=0、sie=0，设置 stvec
  → 内存初始化和临时演示（第四章后来增加；结束时恢复 Bare）
  → loader::load_apps
  → 每个应用 prepare_app，取得各自 TrapContext 指针
  → task::init：所有任务 Ready
  → timer::init：设置首个未来 deadline，再打开 STIE
  → task::run_first：首个任务 Running
  → __switch → __task_bootstrap → __restore → sret
  → 首个应用在 U-mode 执行
```

`trap::init` 会清空 `sie`，所以不能先开 timer 再调用它。所有现场和任务状态也必须先准备好，
再进入可能被时钟打断的用户代码。

## 3. 应用常驻意味着保留哪些内存

本章没有用页表把两份应用放进相同虚拟地址；采用的是两个不同的固定 RAM 槽位：

| 对象 | app0 | app1 |
| --- | --- | --- |
| flat bin / 链接入口 | `0x80400000` | `0x80420000` |
| 槽位大小 | 128 KiB | 128 KiB |
| 用户栈 | 独立静态 8 KiB | 独立静态 8 KiB |
| 内核栈 | 独立静态 8 KiB | 独立静态 8 KiB |
| 初始 `TrapContext` | 自己内核栈顶部的 272 字节 | 自己内核栈顶部的 272 字节 |

应用链接地址与复制目的地址相对应。当前没有通用重定位支持，不能只改复制地址就假设
程序中形成的地址也跟着改变。镜像长度是实际文件字节数，128 KiB 是预留上限，二者不同。

`load_apps` 在启动时装载一次，`prepare_app` 创建初始现场。暂停后再次选中任务，不重新
复制镜像、不清零栈、不重新构造入口，否则就破坏了它已经形成的运行状态。

用户栈保留应用局部变量与用户调用链；内核栈保留处理该应用 Trap 时形成的 Rust 调用链。
这些栈中的内容不会被完整复制到 TCB。暂停时只需让它们继续留在原来的内存里，并保存
之后能重新接上它们的寄存器值。

当前 `START_APP_ID` 只决定首次选谁，后续实际执行者由管理器的 `current` 指定。
`sys_write` 必须按后者检查应用槽位或用户栈，不能一直按首次应用检查。
这种 syscall 软件检查并没有给 Bare 下的应用直接访存建立硬件隔离。

## 4. 两类上下文：保存用户现场，还是保存内核续点

| 属性 | `TrapContext` | `TaskContext` |
| --- | --- | --- |
| 保存的执行层 | 被 Trap 打断的用户执行 | 主动调用 `__switch` 的内核执行 |
| 字段 | `x[32]`、`sstatus`、`sepc` | `ra`、`sp`、`s[12]` |
| RV64 大小 | 34 × 8 = 272 字节 | 14 × 8 = 112 字节 |
| 当前存放位置 | 各任务内核栈顶部 | `TaskManager.tasks[id].task_context` |
| 保存/恢复入口 | `__alltraps` / `__restore` | `__switch` |
| 恢复后的下一步 | `sret` 回用户指令位置 | `ret` 续接内核调用位置 |

“保存在内核栈上”描述的是存放位置，不代表它保存的就是内核现场。用户 `sepc`、用户
`a0/x10` 和用户 `sp/x2` 在 `TrapContext`；内核 `ra/sp` 在 `TaskContext`，不能反过来。

Trap 可以在用户计算的任意指令边界发生，用户编译器没有在这里安排一次普通函数调用，
因此入口需要保存被中断执行所需的整数寄存器和返回控制状态。`x0` 槽记录为零，用户
`x2/sp` 通过 `sscratch` 另外保存。

`__switch` 则是内核主动执行的普通 C ABI 函数调用。编译器知道普通调用会改写
caller-saved 寄存器，跨调用仍然需要的值会由编译器安排保存。汇编额外保留 `ra/sp`
及 callee-saved 的 `s0..s11`，就能恢复这条内核调用链。完整用户现场此前已经由 Trap 入口保存，
不必再复制进 `TaskContext`。

`TaskContext.s[0]` 是 `s0/x8`，`s[1]` 是 `s1/x9`，`s[2]` 是 `s2/x18`，并不对应
`TrapContext.x[0..12]`。本项目尚未提供浮点/向量任务现场和各任务独立 TLS，不能把
112 字节记录当成支持所有架构扩展的通用上下文。

## 5. `__switch` 怎样让一次调用很久以后才返回

接口方向明确为：

```rust
fn __switch(current: *mut TaskContext, next: *const TaskContext);
```

进入时 `a0` 是旧记录的可写地址，`a1` 是新记录的可读地址。汇编先把实际旧 `ra/sp/s`
写入 `a0` 指向的记录，再从 `a1` 恢复新 `ra/sp/s`，最后 `ret`。不能交换两参数，也不能
把旧状态写进临时副本后丢掉。

第一次启动某任务时，`goto_restore` 预先设置：

```text
TaskContext.ra = __task_bootstrap
TaskContext.sp = 该任务的初始 TrapContext 地址
TaskContext.s  = 全零
```

`__task_bootstrap` 执行 `mv a0, sp; j __restore`，适配本项目 `__restore` 从 `a0` 接收
现场地址的接口。这里的 `ret` 只是跳到预先准备的内核入口；最后的 `sret` 才切至 U-mode。

任务之后主动或被动暂停时，`__switch` 写入的 `ra` 已经是这次普通调用之后的位置，
`sp` 是带着 Trap/Rust 调用帧的内核栈指针。再次恢复它，会使上一次的 `__switch`
调用终于返回，之后顺着原来的调用链退出到用户态。

```text
第一次：人为准备 ra → bootstrap → 用户入口
再次运行：上次 call 形成的 ra → __switch 后续 → handler 后续 → 用户暂停处
```

`ret` 使用 `ra`，不改变特权级，也不读取 `sepc`。`sret` 使用保存的 `sepc/sstatus`
恢复 Trap 执行状态。切换内核栈可以保留调用链，但单独换 `sp` 不足以保留全部
callee-saved 状态；两种上下文也不能互相替代。

## 6. 调度状态和环形查找

`TaskControlBlock` 当前只有状态与 `TaskContext`。管理器拥有固定两个 TCB、
`current`、`initialized` 和累计退出失败标志 `had_failure`。

```text
Uninitialized → Ready → Running
                  ↑       │
                  └───────┘ yield / timer
                          │
                          └→ Exited
```

完成一次状态转换后，仍有任务运行时应恰好一个 `Running`，编号等于 `current`；全部退出时
没有 `Running`。更新发生在真正切换之前，因此短暂更新阶段不应插入依赖旧 Running 的查询。
`current_task_id()` 的断言用于检查内核内部状态不变量，不负责检查外部用户输入。

`find_next_task` 从当前任务的后一项开始，检查恰好 `APP_COUNT` 个候选：

```text
candidate = (current + offset) % APP_COUNT
offset = 1..=APP_COUNT
```

只选择 `Ready`，自己是最后一个候选。挂起与退出的区别决定了自己能否再次入选：

- `yield` 和 timer：原任务 `Running → Ready`，选中者 `Ready → Running`。若只剩自己，
  绕一圈仍选回自己，包装直接返回，跳过实际 `__switch`。
- `exit`：原任务 `Running → Exited`，因此永远不会再被查找选中。有下一任务就切走；
  没有 Ready 时打印全部结束，并根据累计失败标志关机。

如果查找区间少了最后的自己，只剩一个任务时一次正常让出就会被误判成没有任务。
如果退出时仍设为 Ready，就可能恢复已经结束的调用链。

当前没有 `Blocked`，退出栈也未动态回收。`had_failure` 使用逻辑或累计非零退出，
不能因为后一个任务正常返回 0 就抹掉之前的失败。

## 7. 一次协作式 `yield` 的完整往返

```text
app0 调用 yield_now
  → U ecall，a7=124
  → 硬件 U→S，保存 sepc，跳 stvec（硬件不自动交换 sp）
  → __alltraps 交换 sp/sscratch，建立 app0 的 TrapContext
  → trap_handler：保存的 sepc += 4，复制 syscall 参数
  → syscall → suspend_current_and_run_next
  → 管理器：app0 Ready、app1 Running、current=1
  → __switch(任务0记录, 任务1记录)
  → app1 首次 bootstrap，或续接它原先暂停的内核调用
  → __restore → sret → app1 用户执行
```

app0 的现场和 Rust 调用帧仍留在 app0 内核栈上。以后 app1 让出，调度器再次选择 app0：

```text
__switch 恢复 app0 TaskContext，ret 到原内核调用后续
  → suspend_current_and_run_next 返回
  → syscall 返回 0
  → trap_handler 把 0 写到 app0 TrapContext.x[10]
  → 返回原现场指针
  → __restore 恢复用户寄存器、sstatus、sepc
  → sret → app0 的 ecall 后续 → yield_now 返回 0
```

这就是先恢复内核续点、再恢复用户现场的两层恢复。`__switch` 不保存 CSR；任务回来后，
不能用此刻实际 CSR 的内容代替原任务保存的 `TrapContext`，因为中间已经运行过别的任务。

## 8. 共享管理器的访问必须在切换前结束

当前实现使用单 hart、S 执行期间 `SIE=0` 的前提，以短暂 `&mut TaskManager` 访问
`static mut` 管理器，没有运行时锁。包装把编号带出作用域，再取静态字段的裸指针：

```rust
let (current, next) = {
    let manager = unsafe { &mut *addr_of_mut!(TASK_MANAGER) };
    manager.suspend_current()
}; // 此处结束独占借用

if current != next {
    unsafe { switch_task(current, next); }
}
```

旧任务暂停期间，新任务还要访问同一个管理器。若旧的独占引用仍然有效，新的冲突访问
可能违反 Rust 别名规则；这不等于当前实现正在等待一把写锁。

如果以后改用 `RefCell`，未释放的动态借用可能使新任务再次借用时失败；若改用实际互斥锁，
旧任务拿着锁暂停、新任务等待同一锁，则可能死锁。必须按具体原语分析，不能把借用、
动态借用检查与锁都当成一种机制。

陷入处理也先复制参数、结束临时现场借用，再调用可能暂停的 syscall。每任务独占的现场
与全局共享管理器不同，不能笼统说任何引用都不能跨暂停；这里明确缩短共享状态的访问期。
退出路径永不续接，所以也不能指望退出任务栈上尚未释放的资源守卫以后自动完成清理。

## 9. 定时器：速率、间隔和绝对截止坐标

| 名称 | 当前值与单位 | 含义 |
| --- | --- | --- |
| `CLOCK_FREQUENCY` | 10,000,000 ticks/s | 当前 QEMU `virt` 的 `time` 时基，已核对 DTB；不是 CPU 主频 |
| `TICKS_PER_SECOND` | 100 | 本项目期望每秒安排的 timer 事件数量 |
| `TIMER_INTERVAL` | 100,000 ticks，约 10 ms | 两次安排事件所采用的时间差 |
| `deadline` | `now + TIMER_INTERVAL`，单位 ticks | 传给 SBI 的绝对 `time` 截止坐标 |

若 `now=2,000,000`，下一截止是 `2,100,000`。直接传 `100,000` 表示绝对坐标十万，
不是“从现在起再等十万”；如果现在已经超过它，事件就已经到期。

timer 是这个 hart 的时钟事件，不属于某个任务的私有计时器。`yield/exit` 可能在截止前
已经切换任务，因此不能把 deadline 称为“下一进程保证运行到的终点”。日志、内核处理
和主机调度也会影响观察时序，不能用输出轮次推出每个任务精确获得多少 CPU 时间。

进入 timer Trap 不会自动给下次事件续期，也不等于自动清除了 timer pending。当前实现
先通过 SBI 设置未来截止，再调用调度切换：

```text
timer 到期 → Trap 保存现场 → set_next_trigger → 再调度 / __switch
```

若放在 `__switch` 之后，设置操作随旧任务调用链一起暂停，要等它再次恢复才执行；
新任务回 U-mode 时可能因旧 pending 立刻再陷入。设置失败时应报告并失败关机，不能忽略错误。

## 10. 从用户计算被打断，到继续计算

`scause` 的 RV64 最高位区分中断，低位表示来源编号：

```text
U ecall：     0x0000000000000008
S timer：     0x8000000000000005
```

只匹配数字 5 不能识别完整 timer 中断原因。timer 路径是：

```text
app0 持续计算，没有 yield
  → 到达 deadline，S timer 满足进入条件
  → 硬件 U→S，保存返回 PC，SIE 清 0，跳 stvec
  → __alltraps 保存 app0 整数现场
  → 重新安排下一 timer
  → app0 Ready，app1 Running
  → __switch 保存 app0 内核续点，恢复 app1
```

app0 再次选中后，先续接自己的 timer 处理分支，handler 返回自己的现场地址，
`__restore` 才恢复用户寄存器和 CSR，`sret` 回到原计算位置。

| Trap 来源 | 保存的 `sepc` 怎么处理 | 保存的用户 `a0/x10` 怎么处理 |
| --- | --- | --- |
| 可返回用户 `ecall` | 加 4，跳过已经处理的 32 位 `ecall` | 写入约定的 syscall 返回值 |
| 异步 timer | 保持原值，作为恢复位置 | 保持原值，没有 syscall 返回值 |

`sepc` 是保存的指令地址，不是异常次数计数器。`ecall` 即使返回 -1，也已经处理了请求，
应避免返回后重复执行同一条；`ecall` 固定为 4 字节，启用压缩指令并不改变它。
timer 随意加 4 会跳过需要执行的指令，甚至破坏指令边界。
决定是否调整 PC 的是 Trap 的具体语义与返回策略，并不是“有没有报错”。

## 11. 为什么内核关闭 SIE，用户仍然可以被抢占

| 位 | 所属 CSR | 当前用途 |
| --- | --- | --- |
| STIE，bit 5 | `sie` | 允许 S timer 这一中断来源 |
| SIE，bit 1 | `sstatus` | 当前处于 S-mode 时，S 级中断的全局使能 |
| SPIE，bit 5 | `sstatus` | 保存原 SIE；进入 S Trap 时 `SPIE←SIE`，`sret` 时 `SIE←SPIE` |
| SPP，bit 8 | `sstatus` | 记录 Trap 之前的特权级，控制 `sret` 返回 U 还是 S |

STIE 与 SPIE 同在 bit 5，但属于不同 CSR。它们不是同一个开关。

S 级中断能否在 U-mode 触发，不受 `sstatus.SIE` 屏蔽；仍需对应来源使能、委托和 pending
等条件满足。因此，内核保持 S 执行期间 `SIE=0`，仍能让用户态被 timer 抢占。
不能把 `SPIE=1` 称为 U 接收 S timer 的必需“总开关”。

硬件进入 S Trap 后清 SIE。恢复汇编写回的保存 `sstatus` 也必须保持 SIE 为 0，避免
尚在 S-mode 恢复寄存器时就发生 S timer 嵌套。初始用户现场清 SPP/SIE、置 SPIE；
执行 `sret` 时才按 SPIE 更新 SIE 并回到 U-mode。

现有 `__alltraps` 无条件交换 `sp/sscratch`，前提是从 U-mode 进入：原 `sp` 是用户栈，
`sscratch` 是对应任务的内核栈顶。如果在 S 处理阶段再次无条件交换，可能把用户栈指针
当作内核栈使用。因此当前没有给内核主动打开 SIE，也没有实现内核抢占或 S 嵌套入口。
SIE 为 0 不能阻止同步异常和更高特权级中断，所以也不能笼统宣称“内核绝不会再次 Trap”。

## 12. 已验证到哪里，如何解读运行结果

历史验收已经验证：两套镜像/栈/现场独立；内核切换能第一次进入也能多次续接；
协作调度能交替运行、选择自己并排除退出任务；无 `yield` 的两个计算应用能互相 timer 抢占。
P0 还观察过当前任务与现场归属、唯一 Running、timer 不修改保存现场、定时器设置失败拒绝启动。
这些是原批次验收记录，本次整理没有重跑构建或 QEMU。

目前 [hello](../../experiments/rcore-learning/user/src/bin/hello.rs) 和
[hello_peer](../../experiments/rcore-learning/user/src/bin/hello_peer.rs) 都进行三轮有限计算，
不主动 `yield`。它们应各打印三轮、各退出一次，出现两个方向的 timer 调度，最后正常关机。
精确抢占次数、交错顺序和退出先后可以变化。只剩一个任务时，`timer 1 -> 1` 是允许选回自己，
不代表又执行了一次实际的自身 `__switch`。

原 BATCH08 的两轮/三轮 yield 程序属于当时的协作验收场景，不是当前默认程序的输出要求。
`yield` 包装和内核调度入口仍然保留，不能因当前样例改成计算循环就认为它们被删掉。

历史运行使用过：

```sh
cd /home/asus/tinyos/experiments/rcore-learning
timeout 8s make run
echo $?
```

项目 [Makefile](../../experiments/rcore-learning/Makefile) 默认明确使用
`/usr/bin/qemu-system-riscv64`，已验证的系统版本是 10.2.1。正常最终退出码为 0；
124 表示 `timeout` 结束了尚未退出的进程，不能仅因为前面输出全部结束就说关机成功。
曾经 PATH 选到开发版 QEMU，旧 RustSBI 因 DTB 设备节点兼容差异未能完成关机；定位后采用
项目局部的已验证默认值，没有改全局 PATH 或把关机环境问题误判成调度算法错误。
详细证据保留在 [BATCH09 第11节](../../experiments/rcore-learning/BATCH09.md#11-作者实现验收与关机差异2026-10-17)。

## 13. 与当前第四章进度的接口

到 BATCH19，启动阶段已经临时让一份新的用户 ELF 在独立 Sv39 页表下运行，再经首次
`exit` Trap 返回启动代码，并恢复 Bare。之后才装载这里的两个旧 flat bin 应用，启动
第三章调度器；**这两个任务目前仍在 Bare 下轮转**。

因此已经分别具备“多任务调度”和“单 ELF 分页执行”两项能力，但尚未把每任务地址空间、
分页 Trap 返回与现有 timer/yield 调度合并。不能把 BATCH19 单应用的返回桥当成
现有两任务已经在各自页表下抢占运行的证据。

当前也没有 fork/exec/wait、父子进程树、动态任务创建与销毁、阻塞队列或多 hart 调度。
本章完成的是单 hart 下固定两个任务的协作与用户态抢占核心，资源布局和中断前提都保持明确。

## 14. 回看资料

- 批次来源：[BATCH06](../../experiments/rcore-learning/BATCH06.md) → [BATCH07](../../experiments/rcore-learning/BATCH07.md) → [BATCH08](../../experiments/rcore-learning/BATCH08.md) → [BATCH09](../../experiments/rcore-learning/BATCH09.md)。
- 官方第三章对照固定提交：`9ca4aedb66f626dcc72f835a5946742a82b61c7c`，不是自动跟随上游分支。
- 教材：[多道程序放置与加载](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter3/1multi-loader.html)、[管理多道程序](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter3/3multiprogramming.html)、[分时多任务系统](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter3/4time-sharing-system.html)。
- 架构与接口：[RISC-V Supervisor ISA](https://docs.riscv.org/reference/isa/priv/supervisor.html)、[SBI TIME 规范](https://github.com/riscv-non-isa/riscv-sbi-doc/blob/v2.0/src/ext-time.adoc)。
- 实际历史验证与当前边界：[PROGRESS.md](../../experiments/rcore-learning/PROGRESS.md)。
