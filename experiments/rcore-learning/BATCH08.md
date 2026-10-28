# BATCH08：任务状态与 yield 协作调度

位置：第三章，TaskContext/__switch 之后、timer 抢占之前。
2026-10-16：作者已完成 manager.rs 的四函数并移除 guard，功能验收通过。
实际运行符合第7节的完整调度顺序，最终正常关机退出0；详细记录见 PROGRESS.md。
以下保留整批原实现要求，供梳理状态机、切换和用户恢复路径。

## 1. 目标与范围

让已装载的两个用户应用真正轮流执行，运行顺序由内核管理：

```text
app0 round 0 → yield → app1 round 0 → yield
app0 round 1 → yield → app1 round 1 → yield
app0 done/exit → app1 round 2 → yield 到自己 → app1 done/exit → 关机
```

这次加入你之前提到的任务状态枚举，以及 TaskControlBlock、TaskManager。
应用主动执行 yield 才切换，称为协作调度。没有时钟中断或时间片：
如果某个应用一直计算，不调用 yield/exit，另一应用就得不到运行机会。
timer 抢占留下一批；页表仍按教材到第四章引入，当前没有内存访问的硬件隔离。

用户两个小循环和 yield 包装已提供，用来直接观察调度，不要求另写测试程序。
app0 循环两轮，app1 三轮，刻意覆盖“一个先退出，另一个继续并 yield 到自己”。

## 2. 三种对象分别保存什么

| 对象 | 内容 | 作用 |
| --- | --- | --- |
| TrapContext | 用户整数寄存器、sstatus、sepc | 返回用户态时恢复用户执行现场 |
| TaskContext | 内核 ra、sp、s0..s11 | 续接暂停在 __switch 的内核调用链 |
| TaskControlBlock | TaskStatus + TaskContext | 把任务能否运行与其内核续点放在一起 |

TaskManager 拥有固定的两个 TCB、current 编号、initialized 标志和 had_failure。
两份用户/内核栈仍由 loader 静态提供，TrapContext 仍在各自内核栈顶部。
TCB 不包含整片栈，也不复制用户镜像；它通过 TaskContext.sp 续接对应栈上的调用链。
本批使用任务概念，还没有 fork/exec 等进程管理接口。

TaskStatus 已定义为枚举：

| 状态 | 含义 | 进入时机 |
| --- | --- | --- |
| Uninitialized | 尚未注册为可运行任务 | 静态空表 |
| Ready | 有合法现场，等待 CPU | 初始化完成，或主动 yield |
| Running | 当前逻辑上被选中执行 | 首次运行或调度选中 |
| Exited | 已结束，不再选择 | exit |

枚举表示状态，current 表示任务编号，二者不能互相代替。
初始化后暂时全部 Ready；启动后，每次状态转换完成时恰有一个 Running，
直到最后一个退出，此时全部 Exited、没有 Running。
在切换指令执行前，current 就先更新为将要运行的任务。

## 3. 已经接好的模块

| 文件 | 本批职责 | 作者工作 |
| --- | --- | --- |
| os/src/task/manager.rs | 状态、TCB、选择规则和状态转换 | A/B/C/D 四处 TODO |
| os/src/task/mod.rs | 全局存储、初始化、短借用、切换和日志 | 核心完成后移除 guard |
| os/src/task/context.rs、switch.S | 上一批的构造、保存恢复、bootstrap | 保留作者实现 |
| os/src/main.rs | 装载两应用、准备两现场、初始化并启动管理器 | 已接好 |
| os/src/trap/mod.rs | 推进 sepc、复制参数、在恢复后写回 a0 | 已接好 |
| os/src/syscall.rs | write/exit/yield 分发，使用当前任务检查范围 | 已接好 |
| user/src/syscall.rs、lib.rs | syscall 124 与 yield_now() | 已接好 |
| user/src/bin/hello.rs、hello_peer.rs | 两轮/三轮交替输出，检查 yield 返回 0 | 已提供 |

第七批的独立 boot/demo 教学接线已撤下，调度应用直接复用你完成的 __switch。
旧演示保留在 `.cache/batch07-before-scheduler-lto_ouj5/` 快照，不再重复执行。
没有新增依赖、堆分配、独立系统库或锁。

## 4. 作者完成的四个函数

这些都是普通 TaskManager 方法，不需要写 unsafe 或汇编。
`self` 就是当前管理器实例；`self.tasks[id].status` 是指定任务的状态。
已有前置断言检查内核内部调用约定，不用于替代外部输入验证。

### A. find_next_task(&self) -> Option<usize>

只查找，不修改任何状态。

1. 从 current 的后一项开始，按环形顺序检查恰好 APP_COUNT 项。
2. 候选编号为 `(self.current + offset) % APP_COUNT`，offset 范围为 `1..=APP_COUNT`。
3. 只选择 TaskStatus::Ready，找到返回 `Some(candidate)`。
4. 全部检查完仍无 Ready，返回 `None`。

注意闭区间的 `=`：current 自己是最后一个候选。
比如两任务、current=1，候选顺序是 0、1。find 不会选择 Running 或 Exited。
直接用 for/if/return 即可，无需迭代器组合或泛型。

### B. start_first(&mut self) -> usize

初始化已将 current 设为 START_APP_ID，默认 0。
将它的 Ready 改为 Running，然后返回这个编号。
这里不能用 A 从 current 后面查起，否则会把默认首个任务改成 1。
现有断言要求它处于 Ready；启动包装只调用一次。

### C. suspend_current(&mut self) -> (usize, usize)

已有局部 current 是切换前的编号，必须保留，供包装取得旧任务的 TaskContext。

1. 将旧任务 Running 改为 Ready。
2. 调用 A 选择 next。
3. 将 next 改为 Running，更新 `self.current = next`。
4. 返回 `(current, next)`。

本批没有 Blocked，且刚将自己置 Ready，因此这里一定能找到候选，
可以用 `expect("yield must find at least itself")` 表达内核不变量。
如果只剩自己，仍要把状态恢复 Running；包装看到 current==next 会直接返回，跳过 __switch。
这样不会把相同的 TaskContext 同时作为汇编的保存与恢复对象。

### D. exit_current(&mut self, exit_code: i32) -> Option<(usize, usize)>

1. 将旧任务设为 Exited。
2. 记录是否发生过非零退出：
   `self.had_failure = self.had_failure || exit_code != 0;`。
3. 调用 A；有 next 时设为 Running，更新 self.current，返回 `Some((current, next))`。
4. 没有 Ready 时返回 None。此时所有任务都应为 Exited，可加断言表达该不变量。

`if let Some(next) = self.find_next_task() { ... } else { ... }` 可以拆开这两个分支。
had_failure 要累计，不能被后一个成功退出覆盖。
该方法只改状态，真正切换和最终关机由外层包装执行。
Exited 的记录和栈暂时保留，不释放、不重建，也绝不能重新选中。

## 5. yield 时究竟暂停在哪里

沿 app0 第一次让出的路径追踪：

```text
app0 main → user_lib::yield_now → ecall(a7=124)
  → __alltraps：切到 app0 内核栈，保存 app0 TrapContext
  → trap_handler：app0 保存的 sepc += 4，复制系统调用参数
  → syscall → suspend_current_and_run_next
    → 管理器：0 Ready、1 Running、current=1
    → __switch(&任务0.task_context, &任务1.task_context)
      → 任务1首次：__task_bootstrap → __restore → sret → app1
```

此时 app0 的 trap_handler/syscall 等调用帧留在 app0 内核栈中。
任务0.task_context 保存的是内核 __switch 调用之后的 ra 和相应 sp。
之后 app1 yield，恢复任务0时：

```text
__switch 的 ret 回到 app0 暂停处
  → suspend_current_and_run_next 返回
  → syscall 返回 0
  → app0 trap_handler 把 0 写入 app0 TrapContext.x[10]
  → handler 返回 app0 的现场地址
  → 原 __restore 恢复 app0 用户现场 → sret
  → app0 的 ecall 后续 → yield_now 返回 0
```

__switch 不保存 CSR。app0 恢复后不要从此刻的真实 sepc/scause/sscratch
重新读取自己原先的现场；原先的数据在 app0 的 TrapContext 里，__restore 会据此重写 CSR。
新任务首次才走 bootstrap，旧任务恢复直接续接先前内核调用链。
`ret` 跳 ra，`sret` 跳 sepc 并按 SPP 恢复特权级，职责保持不变。

## 6. 借用、资源与内存范围

提供的包装先在一个 `{ ... }` 中短暂取得 `&mut TaskManager`，
只把旧/新编号带出来；离开作用域后才取静态表字段的原始指针、调用 __switch。
下一任务也要访问同一管理器，因此不能让旧任务持有它的独占引用或锁再暂停。
不能向 TaskContext 的局部副本保存续点，必须取静态表中真正字段的地址。
TCB/TaskManager 不需要手算汇编偏移；只有原 TaskContext 有固定 C 布局。

trap_handler 也在调用 syscall 前结束临时 frame 借用，只保留指针和复制出的参数。
每个 frame 属于独立栈，借用跨暂停本身不必然错误；这里使用短借用，明确避免暂停期间的别名访问。
exit 路径会放弃当前内核调用链，不能在该路径中持有必须释放的资源或尚未释放的锁。
本批只有静态栈和普通值，栈内存一直存在。

sys_write 已从固定 START_APP_ID 改查 `task::current_task_id()`：
app1 的输出必须按 app1 镜像/用户栈范围校验，不能按 app0 检查，也不能允许所有应用的范围。
这只是内核 syscall 的软件检查；没有页表时，用户直接访存尚无此隔离。
START_APP_ID 只决定首次启动谁，之后每次运行的编号来自管理器。

## 7. 完成与预期输出

写完四个 TODO 后，移除 task/mod.rs 的 compile_error，保留布局和状态断言。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
timeout 8s make run
echo $?
```

装载日志后应严格出现以下顺序：

```text
[task] start 0
[user0] round 0
[task] yield 0 -> 1
[user1] round 0
[task] yield 1 -> 0
[user0] round 1
[task] yield 0 -> 1
[user1] round 1
[task] yield 1 -> 0
[user0] done
[kernel] application 0 exited: code=0
[task] exit 0 -> 1
[user1] round 2
[task] yield 1 -> 1
[user1] done
[kernel] application 1 exited: code=0
[kernel] all applications exited
```

5 次 yield，其中 4 次换到另一任务、1 次直接返回；app0/app1 各退出一次，最终 QEMU 退出 0。
真正 __switch 共调用 6 次：首次启动 1 次、yield 跨任务 4 次、app0 exit 切换 1 次。
没有 panic，也不应在 app0 退出后再次运行 app0。

## 8. 必要验证（协作者承担）

P0 固定两次 QEMU，不要求作者新增测试框架：

- 正常：上述完整顺序和次数、yield 返回 0；每次状态转换完成后恰一 Running 且等于 current；
  每次 U trap 的 frame、保存用户 sp 和 sepc 属于当前任务，SPP=0；最后全 Exited、关机 0。
- 拒绝与退出结果：临时 app1 用 write 请求读取 app0 镜像中的 1 字节，返回 -1，
  随后继续正常输出/调度；临时 app0 返回 7，app1 仍完成并退出 0，最终失败关机退出 255。
  验证当前任务范围检查、退出任务不再恢复，以及失败标记不会被后续成功清除。

作者新增测试代码为零。测试学习点是状态机不变量与任务归属，不扩展为寄存器或故障矩阵。
系统内部断言失败导致 panic，与上述有意非零退出码的 SBI 失败关机是不同路径。

2026-10-16 临时 `.cache/batch08-preflight-ha7wu_q1/` 已补齐四函数并加观察，
两条均无警告构建，顺序/运行状态/frame 归属检查通过，QEMU 分别退出 0、255。
作者 os/user 源文件哈希前后相同；只验证相关接口，不宣称官方整章已经通过。

## 9. 完成后集中讨论的三个问题

1. app0 yield 时，用户的 sepc/a0 和内核的 ra/sp 分别存在哪里？
   为什么恢复 app0 后，先返回内核 syscall，再回到用户 ecall 后面？
2. 为什么 yield 要先改 Ready，而 exit 要先改 Exited，再查找下一任务？
   `1..=APP_COUNT` 若少检查自己，会怎样影响只剩一个任务的情况？
3. 管理器为什么必须在 __switch 前结束借用？sys_write 为什么不能继续使用 START_APP_ID？
   如果 app0 不主动 yield/exit，本批的 app1 是否能抢到 CPU？

## 10. 三题回答复核（2026-10-16）

作者已回答第9节三题，以下集中记录需要修正的概念，供后续 timer 批次回看。

### 第一题：用户现场与内核续点的归属

两类现场的保存对象不能反过来理解：

| 对象 | 保存内容 | 实际存放位置 | 恢复者 |
| --- | --- | --- | --- |
| TrapContext | 用户 sepc、a0/x[10]、sp/x[2] 等整数现场及 sstatus | 各任务内核栈顶部的272字节区域 | __restore |
| TaskContext | 内核 ra、sp、s0..s11 | TaskManager.tasks[id].task_context，即 TCB 的字段 | __switch |

用户现场存放在内核栈上，仍然是用户执行的现场；保存位置不决定它属于哪一层执行。
TaskManager 拥有 TCB，但并没有把用户的 sepc/a0 直接放进 task_context。
用户栈内容、内核栈调用帧也没有全部复制到 TCB，它们继续留在各自的栈里。

app0 yield 后再次被选中时，先从 TaskContext 恢复内核 ra/sp，
续接暂停的 __switch 调用链，直到 syscall 返回0；trap_handler 再把0写入
app0 的 TrapContext.x[10]，最后 __restore 根据这份用户现场恢复寄存器和 CSR，
sret 才回到 app0 的 ecall 后面。两层恢复依次完成不同的职责。

### 第二题：主动让出、退出与查找自己

作者对 Exited 不再参与选择、只剩自己时仍需继续运行的理解正确。
需要修正的是触发原因：本批没有时间片耗尽，是用户主动调用 yield 让出 CPU。

yield 将当前任务从 Running 改为 Ready，因为它还要继续执行；
exit 将当前任务改为 Exited，find_next_task 只选 Ready，因此不会再次选中已退出任务。
查找范围 `1..=APP_COUNT` 将自己作为最后一个候选。
只剩 app1 时，它 yield 后先成为 Ready，绕一圈选回自己并恢复 Running，
包装直接返回，不执行自身到自身的 __switch。

本批即使完全不调用 yield，一个应用仍可通过 exit 让另一个继续；
若它一直计算且既不 yield 也不 exit，则另一个应用无法获得 CPU。
定时器抢占会在下一批补上。

### 第三题：借用、锁与当前任务范围

当前项目没有 TASK_MANAGER.exclusive_access()，也没有运行时写锁。
实际使用的是指向静态管理器的短暂独占引用：

```rust
let (current, next) = {
    let manager = unsafe { &mut *addr_of_mut!(TASK_MANAGER) };
    manager.suspend_current()
}; // 管理器的独占借用在此结束，只带出两个编号。

if current != next {
    unsafe { switch_task(current, next); }
}
```

`&mut` 表示独占访问，不是一把会在运行时阻塞其他任务的锁。
如果旧任务仍保有有效的独占借用，另一任务又对同一个管理器进行冲突访问，
就可能违反 Rust 的别名约束并导致未定义行为。
结束借用后再切换，让下一任务能够重新独占访问管理器，清楚界定访问阶段。
仅跨暂停保留各任务自己独立的 frame 引用并不天然错误；这里不能把它与共享管理器混为一谈。

作者描述的“旧任务拿着锁暂停，新任务等同一把锁”的死锁情形，
适用于实际使用锁的实现；不能据此推断当前代码正在等待写锁。
以后若引入锁或动态借用检查，应按具体同步原语分别分析其阻塞或报错行为。

sys_write 必须查询 current_task_id，再按该任务的镜像/用户栈检查完整范围。
START_APP_ID 仅决定首次运行谁，之后 current 会不断变化。
继续固定使用 START_APP_ID，会错误拒绝另一个应用的合法输出，
也无法正确实施“只能读取当前任务缓冲区”的 syscall 范围规则。
这仍是软件检查，当前还没有页表提供的硬件内存隔离。

本次仅补充三题复核，不增加作业或测试；此前功能验收结果保持不变。

参考固定官方 ch3：`9ca4aedb66f626dcc72f835a5946742a82b61c7c` 的
os/src/task/{task,mod}.rs、os/src/syscall/{mod,process}.rs。
官方枚举名 UnInit 在本项目写为 Uninitialized；官方最终总是正常关机，
本项目延续已有 exit code 语义，累计非零退出并用于最终关机结果。
在线说明：[管理多道程序](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter3/3multiprogramming.html)。
