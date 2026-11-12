# BATCH16：提取固定任务表

状态：2026-11-11功能、全部既定P0、追加500轮压力、路径梳理及原三题原答/集中审查/修正版均归档，本批收尾。
主体及接线由作者实现，原高水位/C头问题已修复。完整结果`.cache/batch16-final-20261111/summary.json`；本轮未新增产品修改或提交。

导航：[批次索引](README.md) · [上一批](BATCH15_PARENT_AND_WAIT.md) · [模块边界](../TASK_MODULE_DESIGN.md)

## 目标与顺序调整

作者在B15收尾后提出现在开始拆sched.cpp。原计划把整体拆分排在多内置ELF之后；
现在利用已通过的父子生命周期基线，先提取稳定的任务槽与身份管理，再继续其余拆分。
不同内置ELF选择仍待做，不作为本次提取的前置条件。

可观察结果：原用户输出、公开ID2/4/5/3、退出0/23/37/0、父子权限与页面统计保持，
sched.cpp不再拥有Task/Process数组、下一ID、高水位，分配/查找/回收转入唯一TaskTable。
轮转、timer、trap、用户页算法和syscall ABI不改，不引入堆、动态容器或第二套PID。

## 文件与职责

| 文件 | 改动 |
| --- | --- |
| include/task/private/task.hpp | 搬现有task_state/task_kind/task记录，保持枚举值和字段；增加禁复制移动及clear_metadata成员 |
| kernel/task/task.cpp | 实现task::clear_metadata，供表和独立idle共用 |
| include/task/private/task_table.hpp | 声明tinyos::TaskTable，隐藏五个Task与同槽Process、计数与ID |
| kernel/task/task_table.cpp | 实现分配、查找、复位与安全槽回收；不读CSR或scheduler全局 |
| kernel/task/sched.cpp | 使用唯一永久TaskTable，保留调度/父权限/收养和用户操作编排 |
| Makefile | 增加kernel/task/task.o与kernel/task/task_table.o，保留sched.o |

private目录表示内核内部接口；syscall、boot及用户程序继续使用原sched.h，不直接操作任务表。
本次保留现有小写task/枚举类型名，避免将全面命名迁移混入重构。

TaskTable负责固定存储、公开身份、高水位及UNUSED槽复用。scheduler继续负责：
current_task、idle_task、kernel root、g_user_reaper_id，READY/RUNNING/BLOCKED迁移、唤醒/选任务/切根；
创建者和收集者权限、孤儿转交、fork/exec/退出编排也留在原位置。
Process继续只负责自己的空间/结果/父字段，不接管全表或调度。

## Task与稳定地址

将现有两个enum和struct原样搬到task.hpp，包含架构context头，前置声明tinyos::Process。
task增加默认构造、删除复制/移动构造与赋值，并声明`void clear_metadata() noexcept;`。
字段仍为context、entry、task_id、state、kind、sleep_ticks、proc，栈继续8192字节/16字节对齐。

clear_metadata沿用原助手：清context.frame、entry、task_id=-1、kind=KERNEL、sleep_ticks=0、proc=nullptr；
不改state，不清context.stack，不reset Process，不释放页。调用方按流程最后设置UNUSED或idle READY。
原助手也服务idle，不能只搬成表私有方法导致idle失去入口。

TaskTable全局永久放置，建议`static constinit tinyos::TaskTable g_task_table{};`。
表和Task都不可复制/移动，不可对整个对象或数组执行`={}`、memset或按值传递。
只有对UNUSED槽分配时才调用riscv_task_context_init；清理不用它重建仍可能被引用的现场。
trap_frame作为寄存器快照的复制仍保留，不与禁止复制Task/context混淆。

## TaskTable接口草图

纯C++头包含task.hpp、process.hpp与所需整数头，不使用extern C或公开可变整数组getter。

```cpp
namespace tinyos {
class TaskTable {
public:
    static constexpr int kCapacity = 5;
    constexpr TaskTable() = default;
    TaskTable(const TaskTable&) = delete;
    TaskTable& operator=(const TaskTable&) = delete;
    TaskTable(TaskTable&&) = delete;
    TaskTable& operator=(TaskTable&&) = delete;

    int reset();
    task* at(int slot_index) noexcept;
    const task* at(int slot_index) const noexcept;
    Process* process_at(int slot_index) noexcept;
    const Process* process_at(int slot_index) const noexcept;
    int high_water() const noexcept;
    int find_slot(int task_id) const noexcept;
    int slot_of(const task* target) const noexcept;
    int allocate(task** task_out);
    int recycle_dead_slot(int slot_index);

private:
    task tasks_[kCapacity]{};
    Process processes_[kCapacity]{};
    int high_water_{};
    uint64_t next_task_id_{};
};
}
```

## 各操作完整契约

| 操作 | 行为及失败后状态 |
| --- | --- |
| at/process_at | slot在[0,5)时借用对应存储，否则nullptr；UNUSED槽也可访问。Process预留存储不意味着KERNEL已绑定proc |
| high_water | 原task_count，表示曾使用的连续槽上界，不是活任务数；回收不降低 |
| find_slot | 原按公开ID查找，遍历[0,high_water)，排除UNUSED；负ID或找不到返回-1 |
| slot_of | 比较各槽地址与target相等以返回索引；nullptr或非本表地址返回-1。不对不明来源指针先做减法或关系比较 |
| allocate | 保持原成功返公开ID/失败-1；失败不改*task_out。先找最小UNUSED，再考虑上界追加；检查ID编码上限、空间及同槽Process.reset，成功后才初始化UNUSED现场并提交ID/高水位与输出 |
| reset | 全五个Process先做resources_empty预检；任一忙返回-1，表、Process、ID/高水位均不改。安全提交阶段再reset所有Process、逐槽clear_metadata并设UNUSED，最后清两计数；不切根或清活栈 |
| recycle_dead_slot | 检查索引属于[0,high_water)、槽为DEAD、同槽Process资源已空；reset Process失败则不改槽。成功clear_metadata，最后UNUSED；不改高水位/nextID，不释放页或清栈 |

所有修改表的调用继续处于约定的单hart、SIE关闭、安全boot/trap上下文；scheduler负责这项前提，
表不自行操作CSR、不通过extern读取current/reaper/root，也不调用fork/exec或重调度。
reset全体预检后，在上述无并发阶段Process.reset不会出现普通失败；重要返回值仍须处理，不能忽略。
任务的公开身份只由表写入/清除；其他内部模块借用Task是为了原有状态和现场操作，不另造身份分配器。

recycle_dead_slot只检查表自身的DEAD/资源条件；当前父、非当前槽、USER种类、有效退出原因仍由
task_reap在调用前核对。不要把这些权限/栈寿命条件移到表中再让它反查scheduler。
内核任务虽然没有绑定proc，同槽Process仍必须为空；本轮不增加内核DEAD自动回收入口。

## 接线顺序与注意点

1. 先搬Task记录及clear_metadata，再实现TaskTable。保留公开sched.h全部C签名。
2. 从sched.cpp移除MAX_TASKS、tasks、g_process、g_next_task_id、task_count及旧allocate/ID查找实现，
   统一使用g_task_table。常规循环用high_water；原configure全容量状态检查仍扫描kCapacity。
   at认容量，其他以高水位为边界的查找不因此扩成容量扫描。
3. sched_init先做原kernel_root地址校验，再调用表reset。只有成功后才提交kernel root、current、reaper配置和idle初始化；
   表reset失败时，外层配置与原关系必须保持。idle继续独立拥有存储且proc=nullptr。
4. 所有创建入口原有可失败校验都留在allocate之前。成功后由slot_of及process_at绑定同槽Process，
   继续原无失败发布区间；不以task_id当索引，不通过暴露tasks.data()来恢复旧指针差。
   allocate已保证输出属于表，索引还原不能引入一个会消耗ID后普通return -1的新失败分支。
5. peek仍只读并按父关系扫描；reap做完现有全部权限/退出结果检查后调用recycle_dead_slot。
   不在reap里再重复Process.reset/清metadata，不在表中再次释放用户空间。
6. 将scheduler的下标访问改成受控借用，保留各入口原空值/边界检查、错误返回与停机策略。
   current仍为槽号，reaper仍为公开ID；选任务/切根/保存frame的先后和用户返回寄存器保持。

建议整批完成后集中审查；遇到具体编译错误可直接交流，不逐行追加本来应提前说明的条件。
用户任务操作的跨文件迁移是后续独立一步，本批不同时把fork/exec/finish搬走。

## 验收与分工（协作者承担P0）

| 等级 | 场景与明确预期 |
| --- | --- |
| P0 | make/原宿主回归通过，无新增诊断或未定义符号；公开C接口不变、用户镜像RX仍4034字节/一页断言保持 |
| P0 | 表的越界/空输出/满槽/ID耗尽与资源忙reset拒绝不改原状态；孔洞复用最小槽，公开ID单调不回退；回收保持高水位，不误认旧ID |
| P0 | Task/表不可复制移动；任务、frame、8192字节栈和同槽Process地址稳定。回收只清元数据，UNUSED重新分配才初始化现场 |
| P0 | 正常QEMU保持U0/新spawn/fork-exec/U1退出ID2/4/5/3与0/23/37/0，parent归属、EXEC身份/栈保持；五份空间35free、新alloc21、净恢复14，三次收集零PMM，退出后两次S timer |
| P0 | 一个真实五槽孤儿场景继续通过，确认新表遍历/同槽存储不损坏孩子现场及父结果；不是要求重跑全部B15矩阵 |
| P1 | 额外随机槽复用或更多初始化失败组合，可选学习；不作为继续前提 |
| P2 | 动态池、线程/多hart、调度策略优化或全部历史注入，均不属于本批 |

验证命令仍为`make -j4`、`make test`、`timeout 25s make qemu`、`git diff --check`。
表专项与只读QEMU探针由协作者补齐/适配，不交给作者重写框架，不执行make clean或自动提交推送。
重构目标是行为保持，文件行数不作为验收数字。

## 集中验收与执行路径（2026-11-11）

原定P0全部通过，用户追加压力也已通过；协作者补齐验证，没有增加作者测试作业。

- 公共API13项检查在当前真实表实现全部通过，原尾部回收高水位失败现已修正；容量/借用、预留槽归属、
  ID查找、满槽输出保持、最小孔洞复用、栈保留、重复回收、busy整表/单槽拒绝均保持。
- 另10项宿主边界检查通过：空输出不消费ID，最后INT_MAX合法ID可分配，再分配拒绝且输出/计数保持，
  即使回收出空槽也不重开耗尽ID；显式安全reset才重新从0起。
  高水位以外的预留Process带资源时，reset仍预检全五槽，整表字节保持。
  ID注入只在隔离头副本增加friend观察缝；实际table/task/Process方法保持，context初始化为宿主模拟。
- 真实原双S/双U演示重跑通过，ID2/4/5/3退出0/23/37/0，正常RX4034字节，父关系保持；
  EXEC仍同Task/Process/frame存储与内核栈，五份空间唯一合法free35/runtime alloc21/净恢复14，
  三次reap零PMM、最后两次S-mode timer通过。沿用原B15只读探针，改成通过新表访问，
  不新增next-ID getter；ID增长由真实返回值与500轮压力另证。
- 真实五槽活孤儿场景通过：父2自然SPAWN孩子4后exit17，孩子原Task/frame/栈/root/内容保持、parent转3、
  转交零PMM，父自己结果保留；根真实收集父和孩子后退出0，后续timer正常。
- 正常及孤儿副本编译无诊断，kernel无未定义符号，当次正式源哈希保持；外部停止QEMU不称客体关机。
  宿主500轮2500记录及真实QEMU500轮2000子进程的持久化测试见上节。

证据`.cache/batch16-final-20261111/{normal,orphan,host-boundaries}/`与summary.json；
宿主公共检查日志为`host-boundaries/public-results.log`。本批限定单hart、固定五槽，P1/P2不扩大。
当前重复return及reset后高水位为0的空循环是非功能冗余，不作为新验收门槛。

拆分后的执行路径：

1. sched_init校验内核根，调用表reset全体预检/复位；成功后提交scheduler自己的root/current/reaper/idle配置。
2. 创建入口先完成参数、空间、SIE与父身份校验，再由表选择最小空槽或追加高水位，重置同槽Process、
   初始化UNUSED现场、领取单调公开ID；调用方随后在原SIE0无普通失败发布阶段填写入口或绑定用户空间。
3. at按容量借用槽，find_slot按公开ID在高水位内找非UNUSED，slot_of只判定指针属于哪个槽；
   三者都不决定当前身份或收集权限。Task/Process/栈存储地址固定，trap仍可更新栈内frame指针。
4. scheduler用高水位遍历，继续负责睡眠/唤醒、RUNNING/READY、选择下一任务与切根。
   用户退出仍先处理孩子及结果，再切内核根释放自身空间；表没有接管这些策略。
5. task_reap先核对当前父、目标身份/DEAD/结果及安全栈条件，再调用表recycle_dead_slot。
   表仅对自己拥有的同槽Process执行reset、清Task元数据、最后UNUSED；页面早在退出释放，不重复free。
6. 后续allocate可复用同地址槽，但领取新ID并重建初始frame；回收不缩高水位、不回退ID，
   已回收旧ID不再解析成该地址上的新任务。当前任务/idle/kernel root/reaper仍独立属于scheduler。

## 理解收尾（实现后集中三题）

1. 表的容量、高水位、公开ID和当前槽分别表示什么？为什么回收后高水位不减、ID不回退？
2. 为什么TaskTable不能用整体赋值搬到另一地址？recycle与allocate各自怎样处理context/frame/栈？
3. 为什么reset资源预检失败必须保留外层reaper/current/root配置？为什么父权限与切根顺序仍由scheduler负责？

原答与集中修正继续写本文件，不要求重复作答或追加验证作业。

### 作者原答（2026-11-11，逐字保留）

> 容量是kCapacity,高水位是high_water_,公开ID是g_task_next_id,当前槽是current_task,回收后task还在，只是没东西了，之后还需要参与到重新分配
> 当前有且只能用一个TaskTable承担，移动会导致里面数据结构都悬空, 回收解除物理映射，清空frame,对应内核栈需要置空，重分配要把指针指到栈顶，frame重新弄初始化
> reset失败之后保留可以再次尝试，父权限和切根顺序不清楚

### 集中审查与协作者修正版

1. 容量是最多5个槽，高水位表示已使用过的连续槽上界，当前槽是current_task；这几项方向正确。
   公开ID实际存于每个Task的task_id。原g_next_task_id、现在表内next_task_id_是下一次待发放的ID计数器，
   不能拿它代表正在运行任务身份。例如槽2可承载ID5，下一计数器已是6，高水位仍可为4、容量为5。
   回收保留静态槽存储及当前高水位约定，供后续扫描找UNUSED复用；ID不回退还为了让旧公开ID失效，
   避免同槽新任务被误当成旧任务。只有明确的安全整表reset开始新的ID域。
2. 当前运行内核只有一张拥有任务的表，但禁移动的根因是稳定地址与内部指针，不是类型禁止创建另一测试实例。
   context.frame指向本Task内嵌stack，Task.proc借用同表Process；整个表搬走后，这些指针不会自动改向新位置，
   保存的栈地址/sscratch等也可能仍引用旧存储，造成错误归属或旧对象消失后的悬空。
   回收槽不解除物理映射、不释放页、不清空内核栈字节，也不抹掉旧frame区域内容；用户空间早在exit/fault或EXEC旧镜像释放阶段处理。
   recycle仅reset已无资源Process的结果/parent、清Task元数据（context.frame指针置nullptr）、最后UNUSED。
   allocate时才重新初始化同槽现场：frame放在内核栈顶下方sizeof(trap_frame)处，清寄存器、设置初始sp/gp，
   领取新ID；USER发布再将frame内sp等改为用户入口所需的用户栈顶/参数。frame指针与frame内sp是不同对象。
3. reset失败保留以供重试正确，更根本是失败前后仍为同一套一致系统：旧任务/空间还存在时，不能先改current/root/reaper，
   否则表与scheduler配置不匹配，可能失去正确运行身份、选根或收集责任。先全体资源预检、表reset成功后才提交外层配置。
   父权限需要可信的当前RUNNING USER、请求来源与目标parent关系；表只有存储/身份/槽寿命，不能仅凭目标DEAD就证明调用者有权收集。
   切根顺序需要当前活动satp、内核模板根和资源事务时机，必须在释放用户空间之前切到安全内核根；表既不拥有这些运行上下文，也不该反查scheduler全局。
   当前由scheduler编排；下一批用户操作分离时，可将资源事务连同显式kernel root依赖整体迁移，TaskTable仍只管槽与ID。

原答中的关键缺口已集中澄清，不要求重答或新增测试。本批完成，下一批为用户任务操作模块拆分；
具体文件/接口/验收到下一批一次交付。

## 准备验证与限制

### 2026-11-11 TaskTable主体初次集中检查（历史，接线与高水位已修）

作者已实现全部表方法，Task与表禁复制/移动。目标`make kernel/task/task.o kernel/task/task_table.o`
通过无诊断；真实task/table/Process.cpp的13项局部宿主检查12通过、1失败，唯一失败为尾槽回收收缩high_water。
正常重置、容量边界、预留槽slot_of、公开ID查找、五槽分配/满槽输出保持、最小孔洞新ID复用、
栈内容保留、重复回收拒绝、busy reset与busy单槽回收拒绝均通过。宿主仅模拟架构context初始化。

集中修正项：

1. recycle_dead_slot删除收缩high_water的while，保持本批已约定的高水位语义，并去掉重复return 0。
2. sched.h移除private/task.hpp，改在sched.cpp直接包含。当前C11独立包含sched.h实际因namespace/类语法失败，
   不能把纯C++内部记录通过共用C头传播给syscall/trap等C实现。
3. Makefile尚未把task.o/task_table.o放入OBJS；只单独编译模块不代表默认链接已接入。
   scheduler仍拥有旧数组/ID/高水位和分配器，后续需按原任务统一迁移，不能并行保留两套发布存储。

证据`.cache/batch16-table-review-20261111/{results.json,host-results.log,target-build.log,c-header.log}`。
本轮未代改作者产品源码，未运行已知尚未接通的QEMU；其余表主体未发现新功能问题。

### 作者追加：测试任务工厂与500轮压力测试

作者明确要求反复终止并拉起五个任务、跑几百轮，并在test中加入测试工厂；由协作者实现。
当前五槽包含两S演示且没有S任务回收入口；真实用户根也不能带孩子退出后重新打开bootstrap。
因此分两层实现，正式创建/回收策略不改：

- `test/task_factory.hpp`、`test/test_task_table.cpp`：真实TaskTable/Process实现，宿主模拟context初始化，
  每轮填满五槽、检查满槽失败、逐个标DEAD/记录结果/recycle，全部UNUSED后下一轮；500轮2500个单调ID。
  检查旧ID失效、最小孔洞优先、Task/Process/初始frame/栈地址稳定、16字节对齐、现场重新清零、
  栈非frame区域保留、父关系/退出结果清空及零PMM。这层创建空空间的元数据记录，不声称执行用户程序。
- `test/user_stress/task_factory.{h,c}`、`main.c`与`verify_task_table_stress.py`：只在隔离源码副本省略两S，
  保留boot两个USER，由根先收初始孩子，然后根加4个worker占满五个USER槽，循环500轮，最后根退出。
  交替4次SPAWN/4次FORK，每4轮有一次EXEC和一次预期load fault13；每轮额外测试满槽SPAWN拒绝。
  所有身份和关系经真实接口建立，不直接改生产parent/状态或重开sched_init；fork子直接执行worker并退出。

新增命令`make test-stress`先运行原宿主测试和工厂压力，再运行默认500轮QEMU；
`TASK_STRESS_ROUNDS=1000`可调整QEMU轮数，宿主固定500轮。独立脚本也支持`--rounds/--timeout/--output-dir`。
日志与隔离源码每次保存新目录，拒绝覆盖同名证据；不运行make clean、不自动提交推送。

实际结果：`make test-stress`全过，QEMU500轮约4.24秒（含最终两次idle S-mode timer），
2000个子进程=1000 SPAWN+1000 FORK，125次EXEC、125次FAULT、500次满槽失败；
每轮四个worker均已reap为UNUSED，Process结果/parent为空、对象地址稳定、父数据/用户栈与buffer隔离。
每轮PMM回到基线，活PA账本检查每次free归属及唯一性；运行期分配/回收18375页次，最终根再free7，
总运行期free18382页次。这里是周转次数，不是同时占用18375页。
用户压力RX为0xb8c=2956字节，RW memsz0x1030，沿用-O0/一页代码断言，目标编译无诊断、nm-u空。

最终证据`.cache/task-table-stress/20261111-185955-30/{results.json,make-test-stress.log,host-results.log,qemu.log}`；
概要`.cache/b16-stress-summary-20261111.json`。另有4轮冒烟与此前500轮结果，均保持当次正式源哈希不变。
这轮由协作者添加测试及构建入口，没有代改作者的产品TaskTable/scheduler算法。
真实压力保留一个根直到结束，未实现“五个USER全部退出后由内核再次创建根”的新生命周期策略。

`.cache/batch16-preparation-20261111/results.json`：拟定头接口按RV64 C++20语法通过，无诊断；
constinit全局实例可常量初始化，Task及TaskTable复制/移动被禁止，栈大小仍8192。
这只是接口准备，没有实现表方法或链接/运行新TaskTable，不能当成本批功能已通过。
