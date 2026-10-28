# BATCH22：独立地址空间下的时钟抢占，第四章功能收尾

2026-10-25：作者已完成A/B/C，实际工作区功能验收通过，三个理解题已集中复核并记录。
本轮第四章按既定范围收尾，rCore保留供按需查阅，工作重心回TinyOS；有一条未使用导入警告可清理。
本批完成后集中梳理第四章并做三个理解题，随后回TinyOS，不自动进入第五章。

## 1. 目标与范围

B21已经可以选择两个独立用户空间，但切换由应用主动调用yield触发。
这批让两个应用持续进行整数计算、完全不调用yield，再由S timer中断触发相同的任务选择。

```text
task0在自己的页表下计算
  → timer到期，硬件进入S-mode
  → 原跳板保存完整用户现场，切公共内核表/内核栈
  → 返回同一个内核事件循环
  → 识别timer，设置下次截止时间，把当前任务设回Ready，选择task1
  → 原恢复入口切task1用户表、恢复它的用户现场、sret
```

本批复用B21的拥有者、环形Ready选择与状态转换，分页汇编保持原样。
继续使用公共内核循环和启动栈，没有需要分别挂起的阻塞内核调用链。
旧第三章Bare双应用仍在临时分页演示结束后运行；不在本批迁移整个旧调度器或永久开启内核分页。

第四章本轮收尾范围是：独立地址空间、ELF装载、用户Trap、分页系统调用、分页协作与时钟抢占的必要闭环。
文件系统、fork/exec、动态创建、每任务内核调用链和扩展习题均不作为回TinyOS的前置作业。

## 2. timer与ecall的现场处理有什么不同

| 项目 | 用户ecall | S timer中断 |
| --- | --- | --- |
| scause | 8，最高中断位为0 | 最高中断位为1，原因号为5 |
| 触发时机 | 用户明确执行这条指令 | 用户可能正在任意普通指令附近运行 |
| 保存的sepc | ecall指令地址 | 中断后应恢复执行的位置 |
| 返回前sepc | 普通调用完成后加4 | 保持不变 |
| 保存的a0 | 写入系统调用返回值 | 保持不变 |
| 任务状态 | 普通write继续Running；exit改Exited | Running→Ready，再选择下一任务 |

RV64的S timer原因是`(1usize << 63) | 5`，不能只比较低位等于5。
原始值5代表同步异常，不是timer。本批把这一点作为唯一拒绝探针。

timer没有处理一条“应当跳过的系统调用指令”，也没有需要交给用户的系统调用返回值。
若照搬`prepare_resume`，就会跳过用户指令并覆盖当时a0中的计算数据。
用户程序还可能使用两字节压缩指令，不能把任意中断位置当作四字节ecall。

## 3. 时钟与中断作用域

沿用B09已经实现的接口：

- `timer::read_time()`读取时基计数。
- `timer::set_next_trigger()`计算当前时间加TIMER_INTERVAL，再通过SBI设置绝对deadline。
- 当前QEMU时基为10 MHz，TIMER_INTERVAL为100000 ticks，即10 ms；它不是CPU主频。
- timer路径必须在选择下一任务前重新设置下一次触发，否则旧pending事件可能使用户一恢复就再次进入内核。

框架只置起`sie.STIE`，内核运行时仍保持`sstatus.SIE=0`。
U-mode运行时，S级中断的递送不受`sstatus.SIE`值限制；还需满足中断委派、相应pending与`sie`使能。
所以本批不需要改B19的初始SPIE清零逻辑，也不打开内核中断嵌套。
依据见[RISC-V Supervisor规范的sstatus与中断寄存器说明](https://docs.riscv.org/reference/isa/v20240411/priv/supervisor.html)。

本批只在启动演示、旧timer初始化之前短暂运行：

```text
构造并核验全部拥有者
  → PagedRuntime启用公共内核表
  → 核对SBI TIME可用，安排首次deadline，打开STIE
  → 用户计算 / timer抢占 / 用户ecall处理
  → 所有任务退出
  → 清STIE，设置deadline=u64::MAX撤销本批定时事件
  → 恢复原8项CSR/Bare并同步
  → 释放临时空间和现场页
  → 后续旧timer::init重新设置自己的deadline
```

清理不能只恢复satp而把本批timer留着。停止定时事件的SBI结果必须处理，失败应明确报错，
不能输出“已完整恢复”的正常结论。框架先完成CSR/Bare恢复，再报告停止失败。
这里不承诺恢复某个先前运行系统的硬件deadline；进入时旧调度定时器尚未初始化。

## 4. 三处作者核心

核心集中在[os/src/mm/paged_preempt.rs](os/src/mm/paged_preempt.rs)。
构造拥有者、开启/停止timer、共享运行作用域、用户样例和必要验证由框架提供。
B21的作者代码与普通协作演示保留，新路径继续复用B20的系统调用处理。

### A：decode_cause

输入原始`scause`，返回`Result<TrapKind, &'static str>`。

- 精确等于8：`TrapKind::UserEcall`。
- 精确等于`INTERRUPT_BIT | 5`：`TrapKind::Timer`。
- 其他原因：返回Err，不把它们冒充成系统调用或timer。

`INTERRUPT_BIT`由框架定义，使用当前usize最高位，避免把RV32/RV64中断位混淆。
本批不实现新的用户fault恢复策略，驱动收到不支持的真实原因会报告失败。

### B：preempt_current

输入B21的`PagedTaskManager`可变借用，返回`Result<(原任务, 下一任务), &'static str>`。

1. 先调用`crate::timer::set_next_trigger()?`。
2. 成功后复用`manager.schedule_after_trap(ScheduleEvent::Yield)`。
3. 将返回的转移结果交给框架；None是错误，因为被抢占的任务仍可运行。

调用Yield状态转换的意思是复用Running→Ready规则，并不意味着用户执行了yield系统调用。
这段不调用`prepare_resume`，不改现场中的sepc/a0，也不重新构造初始现场。
若重新武装失败，应返回Err；不要先改任务状态再尝试设置timer。

### C：handle_ecall

输入管理器及一份独立的`PagedTrapContext`副本，返回框架的`EcallAction`。
当前任务下标为`manager.current`，用户空间在`manager.tasks[current].image.memory`。

先调用B20的`paged_syscall::dispatch`：

- `Outcome::Return(result)`：用`prepare_resume`推进副本的sepc、写返回a0，返回`EcallAction::Resume`。
  当前任务继续保持Running。
- `Outcome::Exit(code)`：用B21的Exit事件转换状态，返回`EcallAction::Exit { code, transition }`。
  不恢复这个任务，退出值由驱动检查，transition允许为None，表示没有Ready任务。

框架负责写回普通调用后的Copy现场，结束所有页内容引用，再进入汇编。
不要在本函数复制用户内存、另写write或把退出值当作系统调用号。
本批样例只调用write和exit，不新增yield系统调用分支。

三处完成后移除本文件的`compile_error!`守卫，再整批验证。

## 5. 如何保证真的发生了抢占

两个任务装载同一个`preempt_demo` ELF，拥有各自的data、用户栈和现场，仍为35页。
用户只进行整数计算、volatile私有计数和停止标志轮询，不调用yield，也不在忙循环里调用系统调用。

为避免依赖“某次百万次循环刚好耗时多少”，框架提供明确的示例停止控制：

1. 各任务的私有data中包含id、stop与计数器。
2. 驱动观察到实际timer双向切换后，在task0被暂停时写它自己的stop，允许它恢复后正常结束。
3. task1必须在task0已Exited后，真正收到一次timer并选回自己，才收到自己的stop。
4. 用户检查自己的数据和计算进展，输出一条done消息，返回0，经既有运行库exit。

stop是本批验证样例的控制字段，不是新增通用系统调用或生产调度策略。
驱动只在用户暂停、内核页池恒等映射有效时写入对应空间的自有页，不把用户VA直接转指针。
用户循环有较大的有限上限，超限非零退出；驱动另有Trap硬上限和外层8秒限制。

## 6. 固定P0与资源边界

仍为一张公共内核表11页，以及每任务5表页+6内容页+1现场页，共35页。
timer/ecall都复用现有用户页与现场，不每次中断重新申请。

P0由协作者承担，提前限定为同一次正常启动：

- 两个不调用yield的分页用户任务，实际观察timer 0→1与1→0，私有计数有进展并保持隔离。
- task0退出后，task1至少收到一次timer 1→1，之后正常结束；Exited任务不会再次运行。
- timer处理中完整用户现场副本不变，尤其sepc/a0；普通ecall仍正确推进和返回。
- 两个任务各一次write与exit，共4次U ecall；timer次数给下限和硬上限，不要求精确固定次数。
- 唯一拒绝：`decode_cause(5)`返回Err，原硬件与资源状态不变。无需主动制造致命用户fault。
- 各轮帧数与堆用量不增长；结束先停timer，再恢复8项CSR/Bare，最后归还35页，free=3008、in_use=0。
- 同次启动保留B14—21及旧Bare双应用正常运行、正常关机退出0。

P1建议：只读追踪timer中断时为什么不能跳过sepc，新增代码0行。
P2可选：更多抢占时机、优先级、阻塞和动态任务，不属于本章收尾前置任务。
本批不扩测试矩阵，不因未做选项延迟回TinyOS。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make user-preempt-elf
# A/B/C完成并移除守卫后：
make build
timeout 8s make run
echo $?
```

交付骨架保留编译守卫，直接构建内核会按预期停在BATCH22提示。
正常完成应主动退出0；运行中的timer次数、具体sepc和计数数值可能变化。
timer次数要求至少5次，驱动在达到128次的硬界限时拒绝继续，避免错误逻辑无限运行。
若timer计数为T，timer后恢复T次，write后普通恢复2次，另有两个任务各自首次进入。

关键新增日志示例（timer次数只是一轮观测）：

```text
[paged-preempt] tasks=2 isolated=true in_use_delta=35
[paged-preempt] cause5=Err state/resources_unchanged=true
[paged-preempt] timer 0 -> 1 context_unchanged=true
[paged-preempt] timer 1 -> 0 context_unchanged=true
...
[paged-preempt0] done
[paged-preempt] exit 0 code=0
[paged-preempt] timer 1 -> 1 context_unchanged=true
[paged-preempt1] done
[paged-preempt] exit 1 code=0
[paged-preempt] timers=5 timer_resumes=5 U_ecalls=4 ecall_resumes=2 writes=2 exits=2
[paged-preempt] bidirectional=true self_timer=true private_progress=true contexts_preserved=true
[paged-preempt] restored: timer=cancelled satp=Bare CSR=true free=3008 in_use=0 heap_restored=true
```

## 7. 收尾的三个理解题

1. ecall与timer都经过同一跳板，为什么只有普通ecall返回时需要修改sepc/a0？`scause=5`与中断位加5有何区别？
2. 重新设置deadline为什么放在选择下一任务之前？内核SIE保持0，为什么用户运行时仍能收到S timer？
3. 从当前用户计算到恢复另一用户，特权级、satp、sp、完整现场和任务状态怎样变化？结束时为何先停timer，再恢复CSR/地址环境并回收？

### 原回答、集中审查与修正版（2026-10-25）

**第1题原答：**

> 如果不改spec会一直陷入ecall和当前栈，但是timer的话我们还没执行完，只是被强制切换了，当然不需要切换sepc，因为下次过来还要用，最高位为0是异常为1则是终端，如果是最高中断位 | 5那就是主动timer终端否则就是异常退出

**审查：**正确理解了重复ecall与被抢占后继续运行。寄存器名是sepc；未推进会再次执行同一条ecall，
并非反复进入某个栈。这里应说“中断”，timer是被动异步中断，不是用户主动发起。
最高位区分中断与同步异常，不表示这次异常必然使应用退出。原答还需补充a0的处理。

**修正版：**普通ecall保存的sepc指向ecall本身，请求处理后加4跳过它，保存的a0写入返回值。
timer保存的是被打断用户继续运行所需的位置，必须保持sepc和用户a0等计算现场，不能把它改成系统调用结果。
RV64 scause最高位为1表示中断、为0表示同步异常；`(1usize << 63) | 5`才是S timer中断。
原始5是同步异常，本批分类器拒绝它；异常之后恢复、终止应用或报告内核错误由内核策略决定。

**第2题原答：**

> 先设置deadline可以保证每个时间片都相同，不然在程序里可能会在不同的地方设置deadline，虽然不接受全局中断，但是在Umode其实是一直开着的，至于在Smode才会起作用选择是否全局中断

**审查：**抓住了SIE在U/S下作用不同，但需要区分sstatus.SIE和单个源的sie.STIE。
先重设deadline是为了重新武装并撤掉旧到期事件，不能保证实际用户CPU时间片严格相等。
设置deadline后仍有内核处理、恢复与模拟器执行等开销；本批由内核统一设置，用户不会自行选择调用位置。

**修正版：**timer到期后，先用now+interval设置新的未来截止时间，再调度和恢复用户，
避免旧pending事件导致用户刚恢复就再次进入内核，也为下一轮抢占安排事件。
interval固定的是相对当前时基的间隔，不是每个任务严格相等的用户CPU运行时长。
sstatus.SIE=0禁止在S-mode递送S级中断；在U-mode时这个全局位被忽略，但相应中断仍需委派、pending及sie.STIE使能。
因此不是所有中断始终开启，也不是关闭了所有特权级中断；当前内核保持不可被S timer抢占，用户计算仍可被它打断。

**第3题原答：**

> 先打断时钟中断功能，再把硬件内容清理一下，隔开硬件与内存，最后清理现场

**审查：**清理的大方向正确，需要把“硬件内容清理”具体化为中断源、CSR与地址翻译使用期。
本次也不只释放现场页，还释放公共内核表、各用户表与用户内容页。

**修正版：**先清STIE并成功撤销本批timer，避免结束演示后再进入旧的中断路径。
随后在有效的低地址代码/栈上恢复原CSR/Bare并执行地址翻译同步，使CPU不再通过即将回收的根表取指或访存。
最后按所有者顺序销毁公共内核表、各用户表及其数据页和现场页，避免留下指向已释放内存的活动或借用映射。
这是协调硬件使用期与软件所有权，不是把硬件和内存物理隔开。停止timer失败也应先恢复地址环境，再明确报告失败。

补充完整切换路径：timer使CPU从U进入S，硬件本身不换satp/sp；
跳板交换sp、保存完整用户现场，切到公共内核表和栈。内核把当前任务设Ready、选中者设Running，
恢复入口再切所选用户表、恢复其整数寄存器/sstatus/sepc，最后sret回U。
35页在调度期间保留，切换不会搬运整份用户内存。

三题已集中讲解，修正版包含协作者补充，不把补充计作原答独立覆盖。
本轮第四章按第1节的学习范围收尾，接下来回TinyOS，由作者主导完整功能与有学习价值的测试实现。

完成本批功能与问答后，按既定安排整理第四章收尾边界并返回TinyOS。
回去先审查当时的ELF装载草稿与测试，由作者主导完整功能和有学习价值的测试实现。

## 8. 交付验证记录

批前真实快照：`.cache/batch21-before-paged-preempt-wicyhil0/`，52个原有源码配置。
临时预检：`.cache/batch22-preflight-hlnngywu/`。只在副本补齐A/B/C并移除守卫，
一次`timeout 8s make run`无警告、QEMU主动退出0，全部既定P0通过。
本次观察5次timer及5次timer恢复、4次U ecall及2次普通返回恢复、两个write/exit0，
双向timer与最后自抢占、私有计数和完整现场保留、35页/8项CSR/Bare/堆恢复均通过。
timer次数没有固定为5；框架使用事件与进展条件控制样例正常结束。
同次启动原B14—21和旧Bare双应用正常，预检前后作者工作区源码哈希一致。

独立只读审查无必修问题，核对了timer停止失败的恢复顺序、旧无timer路径等价性、
受限共享映射与引用边界、所有者在成功/部分构造失败时的释放顺序。
批前52个原文件只有Makefile、user/Cargo.toml、mm/mod.rs、paged_task.rs、paged_user.rs发生预期接线变更。
B21作者A/B/C函数体与原演示、B19初始化/演示、B20作者核心/演示、分页汇编、旧timer/SBI/调度器与用户运行库均保持原样。
本批新增ELF构造入口和受限共享模块可见性，未替换已验收作者实现。
补全内容、源码前后哈希和日志位于预检目录；工作区仍保留三处TODO与编译守卫。
实际工作区`make build`只有预期BATCH22编译守卫错误，无其他错误或警告。
临时副本成功只证明骨架可用，不等于作者已经完成本批，也不提前宣称第四章收尾完成。

**作者功能验收（2026-10-25）：**A已精确匹配8/TIMER_CAUSE，B先重新武装timer再做Yield状态转换，
C普通返回正确准备sepc/a0，Exit保留允许为None的转移结果且不修改退出现场；独立只读复核无必修问题。
实际工作区一次`timeout 8s make run`主动退出0，全部既定功能P0通过：
本次观察5次timer/5次timer恢复、4次U ecall/2次普通恢复、两个write/exit0，
双向抢占及最后timer1→1、私有进展与完整现场保留、唯一cause5拒绝、35页/8项CSR/Bare/堆恢复，
原B14—21及旧Bare双应用正常。timer次数仅是本轮观测，验收仍使用范围和事件条件。
编译有一条未使用`TaskStatus::Exited`导入警告，可删除该导入；不影响本批功能结论。
证据`.cache/batch22-author-review-l9k_umtl/`，54项源码配置运行前后哈希一致；未代改实现或追加测试矩阵。
相对交付，仅paged_preempt.rs与paged_syscall.rs改变，后者只删除一行空白。
第7节三个理解题随后已集中复核归档，第四章本轮收尾完成，回TinyOS，不准备第五章。
