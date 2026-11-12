# Task 组成与模块边界

日期：2026-11-12。本文记录架构上下文与Process拆分、接口及模块分配；不是新的功能批次。
编码规则见 [CODE_STYLE.md](../CODE_STYLE.md)，父子关系的功能归档见
[BATCH15](tinyos/BATCH15_PARENT_AND_WAIT.md)。
最近[BATCH16：提取固定任务表](tinyos/BATCH16_TASK_TABLE.md)功能/全部P0、500轮压力及三题集中审查均完成。
最近[BATCH17：分离用户任务操作](tinyos/BATCH17_USER_TASK_OPERATIONS.md)功能/全部P0及三题集中审查归档完成。
最近[BATCH18：不同内置ELF选择](tinyos/BATCH18_BUILTIN_PROGRAMS.md)功能/全部既定P0及三题原答/集中澄清/修正版归档完成。
新builtin_program模块拥有只读镜像配置与候选准备，spawn仅组织准备/发布，user_task::exec保持替换事务；
scheduler不公开全局依赖，Task/Process不增加程序编号或另一套身份。新12/13与旧8/10默认镜像兼容，细节只在当批维护。
镜像配置/装载选择、正常资源链/准备失败重试和原500轮压力均已验证；当前进程管理阶段收尾。
当前[BATCH19：只读内置文件](tinyos/BATCH19_BUILTIN_FILE_SYSTEM.md)完整任务已交付，尚未实现：文件表只管名字与只读字节/read_at，
boot从lookup结果配置现有ELF registry，原任务/进程及syscall生命周期保持；文件系统不操作用户页表或解析ELF。
文件表复制描述并永久借用底层名字/字节，返回记录指针依赖固定实例寿命；FD/打开位置及磁盘后端在后续独立批次处理。

## 当前拆解安排（2026-11-11）

作者在B15功能/P0/理解收尾完成后提出现在开始拆sched.cpp，调整此前等多内置ELF之后再拆的顺序。
第一步只提取Task记录与TaskTable：固定五槽和同槽Process、公开ID、高水位、分配/查找/安全回收。
current/idle/kernel root/reaper仍属于scheduler；调度策略、父权限/收养及fork/exec/退出编排先保持。
后续再按单独批次迁用户操作，再继续多内置ELF选择；完整接口与P0集中在B16，本文不重复列实现。
TaskTable已由作者实现并接线，协作者完成宿主/压力/正常原演示/五槽孤儿验证，执行路径见B16。
第二步采用无状态tinyos::user_task模块，显式接收依赖，迁用户发布/fork/exec/finish，
scheduler保留current/SPP/SIE校验与最终重调度；finish返回后不得再次要求已DEAD任务为RUNNING。
新模块不反查scheduler全局、不反调度；完整候选归属、PC/a0与失败策略见B17。
作者先迁移实现，随后要求协作者直接按事务细分文件，现为user_task_create.cpp(create+fork)、
user_task_exec.cpp、user_task_exit.cpp(finish+关系)，统一私有头不变；正常/压力/孤儿/失败重试全部通过。
以下11月9—10日讨论保留历史语境，旧先后顺序不再作为当前前置要求。

## 当前实现状态（2026-11-09）

Process与sched.cpp接线及B11 REAP已由作者实现。Task保留context/entry/task_id/kind/state/sleep_ticks，
通过可空proc借用同槽固定Process存储；Process私有持有user_space_与exit_info_，无第二套PID或调度状态。
内核任务/idle不绑定Process，进程不隐式析构释放；退出仍由scheduler协调先记结果、切kernel根再显式release。
正常/事务/复用/fault/宽值、82宿主与sched_init初始化/原子拒绝专项全部通过，三题原答/集中审查/修正版也已归档，本批收尾；最新证据只在BATCH11维护。
随后B12的运行期SPAWN、回滚/重试与收集已由作者实现并通过全部P0/三题归档，静态内核栈已增至8192。
B13已在现有边界上增加：MM复制页与映射、Process转交自己的私有空间、scheduler编排当前任务快照和最后发布；
不暴露Process可变空间getter、不新增fork任务表或第二套ID分配器。B13作者实现与全部既定P0已通过，
包含真实父子现场/页快照隔离、失败回滚与28次合法释放/栈观测，三题原答/集中审查已归档，本批收尾，证据只在B13维护。
B14已实现EXEC：复用Process现有release_resources/adopt，不新增空间交换接口或可变getter；
镜像助手只准备候选，scheduler在提交时切内核根/释放旧空间/安装候选与fresh frame/切新根，身份与内核栈保持。
B14全部既定P0已通过：同ID5新镜像退出37、普通准备失败保留旧程序后重试、35次合法释放与8KiB栈观测。
执行路径与原三题原答/集中审查已归档，本批收尾，证据只在B14维护；尚无父子权限或路径式execve。

## 创建服务与自动场景的讨论（2026-11-10，未实施）

作者提出进程工厂：自动取得Process与Task、分配身份，便于测试任意进程关系，而非依赖几个固定演示角色。
现有task_allocate已在运行期分发ID/复用槽，process_spawn_builtin已组织准备与发布；
U0/U1/U2是演示角色，并非内核只认识这些进程。当前限制是静态容量与分散的创建入口。

建议分清三层：

| 部分 | 责任 |
| --- | --- |
| 创建服务（可称工厂） | 准备镜像或接收fork快照、取得一对任务/进程记录、建立初始父关系、最后提交发布；成功返回公开ID，普通失败回收候选 |
| 任务池 | 唯一管理槽存储/复用与ID分发，保持活动Task/内核栈地址稳定；创建服务不另开任务表或PID计数 |
| 自动场景驱动 | 经真实spawn/fork/exec/exit/wait入口构造角色关系、保存返回ID、观察结果并断言；不直接写parent字段假造功能成功 |

当前一进程一用户任务。初版创建服务可复用可配置的固定容量池；自动分配并不等于必须立即用new或动态搬移Task。
真正按需分配Task/内核栈另涉及分配器、稳定地址和栈映射，不能仅给现有接口加“工厂”名字就视为完成。
池满或页面不足应正常报错，成功收集后槽可重复使用；容量表示同时存在的上限，不表示只能创建这些固定身份。

运行期用户创建的父身份仍取当前合法USER，不能给普通用户一个任意parent_id参数绕过权限。
boot建立根是受控启动入口；用户场景中的“A创建B”应让A实际调用创建服务。
父退出收养策略由生命周期规则决定，工厂只登记初始关系，不包办调度、收养或全部资源模块。
fork可共享最后发布阶段，但其输入是运行快照；exec只替换现有进程空间，不能经工厂再分配Task/ID。

场景驱动可用角色名关联实际返回ID，例如：创建R→A→B，令A退出，确认B改挂R，
再由R分别收集A与B。另用已DEAD孩子、非父收集拒绝、创建失败和槽复用等场景验证边界。
宿主脚本负责构建/运行QEMU/事件检查，用户程序执行真实调用；关系只读观测可留在隔离探针，
不为测试增加生产接口的任意改父能力。使用返回ID与因果事件，不将PID2/3/4或固定时延当通用协议。

本批B15仍先实现父关系与收集语义，协作者可复用现有SPAWN/FORK做自动场景，不要求先造通用框架。
统一创建服务适合与随后多内置镜像选择一起完善，TaskTable的整体提取仍放在本阶段完成后。
这是新增设计方向与边界，不自动扩张B15验收或代写核心；具体接口及实现批次到交付时一次给齐。

## sched拆分草案（2026-11-09历史，先后顺序已由B16调整）

作者已确定先完成当前进程管理阶段，再拆sched，替代本节最初建议的“下一批先抽TaskTable”。
当前先B15父子关系/收集/孤儿收养，之后不同内置ELF选择与EXEC，再按稳定行为实施下面的拆分。
接口与验收集中在各批文档，本节不是要求现在迁移文件或同时实现新TaskTable。

作者提出sched是否需要继续拆分。当前sched.cpp为897行，已混合固定任务表、调度与用户任务生命周期；
建议先划清状态归属，再分文件。本节是讨论草案，未交付新批次完整任务，也未代写产品代码。

作者进一步要求先确定下一步功能；若调度改动有限，倾向完成当前进程管理阶段后再拆。
核对后建议可先补父子关系、限父收集/等待及孤儿收集责任；这些会改创建/退出/REAP入口与演示，
但可保持轮转选择、timer抢占和trap汇编。等待先沿用现有三态结果及用户yield重试，不以阻塞等待队列为前提。
rCore第五章也采用立即返回的sys_waitpid与用户库yield循环，阻塞式实现是后续扩展，
见[进程概念及重要系统调用](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter5/1process.html)。
因此拆分不是启动这组功能的强制前提；先完成该组生命周期语义、再统一拆分是可行顺序。
不同内置ELF的选择是当前同镜像EXEC之外的独立缺口，主要涉及镜像准备/选择与用户接口，
不能将上述关系补齐直接等同于逐项完成整章教程。具体功能批次、父退出收养策略及演示仍待完整任务确定。

| 部分 | 建议归属 |
| --- | --- |
| TaskTable | 固定Task数组及同槽Process存储、单调公开ID、高水位、选空槽、按ID查找、创建发布和安全槽回收 |
| scheduler | current槽索引、idle、永久kernel root配置；timer/yield/sleep、唤醒与运行选择、保存/选择frame、调度切根与启动 |
| 用户任务操作 | 从合法当前用户任务执行fork/exec/终止的编排，候选校验/清理、fresh/snapshot frame与资源提交顺序 |
| Process | 保持现有私有空间与退出结果拥有关系、clone/adopt/release/reset，不接管调度或全表管理 |

建议分两个阶段实施：先提取TaskTable与内部Task记录，收拢分配/查找/复用；
然后将用户任务操作移入独立实现，例如task/user_task.cpp。
不能只把tasks、g_process、g_next_task_id与task_count改为extern后散到多个文件。
表存储保持唯一且固定地址；current和idle不再复制进表的另一套状态，也不新增PID分配器。
peek/reap对当前目标的拒绝由薄壳传入排除槽或由薄壳先检查，表不能反查scheduler全局。
同槽Process绑定与用户任务发布提供受控操作，不暴露可变tasks()/processes()整数组getter。

公开C入口可先保留薄壳，内部依赖按需显式传入Task、trap_frame、TaskTable或kernel root，
不把整套调度器状态打包传给每个助手。内部Task定义和可变借用接口仅供内核模块使用，
syscall及外部调用方继续通过窄接口访问，不拿可变任务数组或长期持有槽指针。
具体内部签名与文件清单应在实施任务中一次给齐，本节不把尚未验证的接口草图当既定实现。
第一阶段保留现有spawn.cpp的准备/创建接线，不同时拆内置镜像配置；若第二阶段需要进一步解开
准备助手与创建事务的编译单元依赖，再明确builtin-image与spawn操作的边界，避免迁移范围失控。

fork用户操作准备候选后，请任务表完成最后发布；exec只替换当前Task，不调用表分配。
exit/fault用户操作完成结果记录、DEAD与安全资源释放后返回，由scheduler选择下一任务；
用户操作不回调scheduler的重调度入口。任务表不调用fork/exec实现，避免双向模块依赖。
资源事务中的kernel-root切换仍由事务编排负责，正常任务切换时的选根由scheduler负责。

本次拆分保持B14行为：活动Task/context/内核栈不可复制移动；公开ID与槽索引区分；
分配失败不消耗ID；REAP先copy后消费且不再次释放页；旧用户空间在kernel root下释放；
EXEC提交异常停机、同Task身份保持。先整理边界，不同时增加父子树、VFS、动态任务容器或新检查矩阵。

## 进程关系的后续设计讨论（2026-11-09，未实施）

作者提出父退出时，将孩子重新挂到原父的父节点，倾向内部指针和span视图。
这是祖先收养策略的提案，不是已经采纳的接口或新的实现批次；提出时主线为B14 EXEC。
EXEC将保持身份，也应保持未来的父子关系，不能借换镜像重设归属。

语义应先于容器决定：A→B→C中B退出，将C的父改A；B若仍有未收集退出结果，
仍作为A的DEAD孩子保留到reap。C、A、B的公开ID和物理槽位都不交换。
需指定祖先已退出/不存在时的兜底reaper；不能把DEAD但尚未reap的祖先当作仍存活的收集者。
活孩子与已退出未收集的孩子都需要转交收集责任。关系更改后应有wait-any/枚举/通知使收养者发现新孩子，
并在未来REAP入口核对当前收集者权限；目前任意其他用户任务可按ID收集，尚无这些约束。

对比来源：Linux孤儿收养到init或合适的子收养者，最近祖先必须有subreaper标记，
不是所有祖父都自动承担该角色；Windows ParentProcessId记录创建者，可以指向已经终止的进程，
父终止本身不自动终止孩子，Job Objects提供另一种成组管理机制。
参考：[Linux subreaper](https://man7.org/linux/man-pages/man2/PR_SET_CHILD_SUBREAPER.2const.html)、
[Windows ParentProcessId](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-process)、
[Windows termination](https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-process)、
[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)。

内部Process*可用，但应为非拥有关系，资源仍由Process/任务表负责。现有静态g_process地址稳定却复用身份：
同slot2先ID2、后ID4/5，旧指针未悬空也可能错误命中新任务。可选择严格维护全部链接直到reap前清除，
或使用指针+预期公开ID/受控句柄校验；目前公开ID单次运行内不复用，不需要仅为此立即加入另一计数器。
span只借用连续数组，不拥有节点、不自动更新parent回链/children集合或其他已复制指针，
也不会因数组某个指针条目替换而修正所有关系。若通过槽地址实现双重间接，槽复用仍不能继承旧身份。

协作者建议第一版由明确reaper兜底，再按需求支持标记祖先收养；作者的任意存活祖先策略同样可实现，
但必须另外定义谁收集/如何发现/何时改链/如何防身份复用。此处为建议，未把该策略记作作者已确认。
当前四槽可仅维护parent关系并由管理器扫描孩子，或在需要时另维护一致的孩子集合，span只是访问视图。
将来关系维护必须在退出提交/槽复用前完成，保持树无环/每个节点一个收集父；不移动活Task/context内核栈。

作者随后澄清：对收养策略和指针/ID都开放，前述方案只是供评估的备选，并非硬性偏好。
过去维护父子链表、反复证明指针不悬空的经历造成负担，希望关系管理更集中、清楚。
适合当前四槽规模的候选是保留现有固定任务表，仅在Process保存parent_id，孩子由管理器按需扫描推导；
树的逻辑关系不要求链表表示，不同时维护双向关系就能减少一致性工作。公开ID单次运行内不复用，
查找已reap的旧ID会失败，不把一个旧父引用静默解析成同地址的新进程。
退出时重新指定所有孩子的parent_id、reap权限/结果消费以及槽复用前清关系仍须由同一管理器维护；
改用ID不会自动完成这些语义。未来规模或访问模式需要时再在模块内部引入孩子集合，接口不以链表节点暴露。
这是协作者推荐的简化方向，尚未确认或实施，也不改变B14范围。
下文“当前拆分”图为首次context提取时的历史结构，Process迁移的实际字段以上述状态及实现起点为准。

## 首次context提取时的结构（历史）

`Task` 应是任务状态和资源的聚合入口。判断是否职责过多，要看谁维护不变量、谁决定执行流程，
不能只数成员。当前已有 `UserAddressSpace`、`UserMemory` 两层拥有关系，不能为了减少字段
再加一个只转发操作的资源包装。

这次提取 `RiscvTaskContext`：

```text
task（调度器内部记录）
├── context: RiscvTaskContext
│   ├── frame：指向当前保存的现场
│   └── stack[4096]：本槽稳定的内核栈存储
├── entry：内核任务入口
├── task_id / kind：公开身份与任务种类
├── state / sleep_ticks：调度状态
├── exit_info：终止后保留的内核结果
└── user_space: UserAddressSpace
    ├── root：拥有的私有根
    ├── kernel_root：借用的永久模板
    └── user_memory：内容页集合及映射归属
```

| 模块 | 维护的职责 | 边界 |
| --- | --- | --- |
| `arch/riscv/task_context`（本次提取） | 稳定内核栈、初始 frame 放置/清零、初始 sp/gp | 不读任务表、不分配 ID、不切根或释放用户页 |
| `task/sched`（当前仍包含槽管理） | 状态迁移、睡眠唤醒、选择任务、终止编排及页表激活时机 | 现场构造交给架构模块，页归属交给 MM |
| `task/task_exit_info.h`（本次提取） | 内核结果类型 | 不引入内存描述，也不是用户 wire 协议 |
| `mm/user_address_space`（已有） | 私有根与借用内核模板的关系、整体释放顺序 | 不清任务结果，不选择下一个任务 |
| `mm/user_memory`（已有） | 内容页、映射验证、解除映射与可重试回收 | 不回收外层根，不操作任务状态 |
| syscall（已有） | 参数解释、用户指针验证/复制、调用内核操作 | 不直接遍历任务槽或按用户 ID 下标寻址 |

`sched.h` 对页表和地址空间只用指针，改为前置声明；需要访问字段的实现自行包含完整头。
这减少调度接口向包含者传递整个内存模块的依赖。保留 C ABI，trap frame 布局及汇编偏移未改。

## 生命周期约束

1. 任务槽及嵌入的 context 保持稳定地址，当前栈容量为 8192（B12调整），按 16 字节对齐。
   活任务不能按值复制、移动或整体清零。上下文初始化只用于尚未运行或已安全回收的槽。
2. `context.frame` 指向栈内保存的现场，后续 trap 可以更新该指针。
   架构初始化只设通用 sp/gp；内核、用户和 idle 的入口/特权位仍由各自创建流程设置。
3. 用户终止仍依次记录结果、标 DEAD、切到内核根、显式释放地址空间、选择下一任务。
   `exit_info` 及静态内核栈保留，不能随着 `user_space` 释放一起清掉。
4. `current_task` 仍是槽索引，`task_id` 是公开身份。不能在已改变当前索引但尚未真正切换
   sp 的阶段复用旧栈。B11 的收集只在约定的安全阶段提交。

## 下一轮安排：结合B11拆解sched并建立process模块

2026-11-06用户进一步指定：下次继续写功能时同步拆解`sched.c`，重新组织出`process`模块。
这替代上一版“先完成B11再抽TaskTable、共享需求出现后才建Process”的安排。
当前只记录后续实现边界，尚未实施process，也不代写本轮剩余B11。

暂定职责：

| 部分 | 下一轮目标职责 |
| --- | --- |
| process | 用户进程记录、地址空间拥有关系、创建/退出的资源阶段、退出结果保存与收集 |
| task与scheduler | 可调度执行记录、context、READY/RUNNING/BLOCKED、睡眠唤醒、运行任务选择及切换编排 |
| 私有表管理 | 槽分配、按公开ID查找、高水位与安全回收，由实际生命周期确定归属，不暴露整张可变表 |
| arch/riscv与mm | 保留已提取的现场构造及既有页表/内容页所有权实现 |
| syscall | 参数解释、用户复制，按既定B11协议先复制结果再提交收集 |

先以一个用户进程对应一个可调度执行记录组织；内核任务无需伪造用户地址空间。
此时就可以建立进程资源边界，不要求先有多线程。当前公开`task_id`与槽索引的区分继续成立，
拆模块不额外引入第二套独立PID计数；Process与执行记录如何关联、表存储放在哪里，
在下一轮接口草图中明确后再改字段，避免两边重复拥有身份或生命周期。

process维护地址空间及退出结果的寿命，scheduler维护执行状态及当前现场。
任务终止可以由编排入口协调二者，不能让两个模块分别推进同一个状态或分别释放同一资源。
切到内核根后才释放私有空间、静态栈保持稳定、结果保留到成功收集、先copy后消费、
不能在旧sp尚未切换时复用槽等约束保持。Process资源操作不直接选择下一个任务，
scheduler不实现用户页内部回收。

下一次先集中给出文件划分、最小接口、字段/状态归属和完整退出/收集路径，再由作者推进核心实现，
协作者审查并承担既定P0验证。允许用C++实现私有模块并保留必要C ABI，
但不为拆模块新增堆、引用计数、动态栈或多线程框架，也不额外扩张B11验收作业。

未来文件系统到来后，VFS管路径和文件接口，进程文件表管描述符到打开文件的关系，
打开文件对象管偏移和访问模式；设备/缓存/inode由各自模块维护。
Process组合这些资源的拥有关系，内部实现仍留在各模块；本轮不开始实现VFS。

## B11实现起点：Process基本操作与接线

作者已完成`task_allocate`的选槽/高水位/单调ID草稿，下一步从`Process`开始。
以下是本轮具体实施方案，取代上节尚待确定的存储选项；核心实现仍由作者完成。

### 文件与字段

- 新建`include/process/process.hpp`、`kernel/process/process.cpp`：C++进程资源及结果操作。
- 将`kernel/task/sched.c`改为`sched.cpp`，保留公开`sched.h`及其C ABI。
- Makefile新增`kernel/process/process.o`，调度器仍产出原来的`sched.o`。
- Task移出直接的`UserAddressSpace user_space`与`TaskExitInfo exit_info`，改为借用的
  `tinyos::Process* process`。保留context、entry、task_id、kind、state、sleep_ticks。
- 保留固定Task数组，旁边预留相同容量的固定Process数组。同槽对应存储，
  内核任务及idle的process为nullptr，用户任务指向其同槽Process。
  未绑定的预留存储不代表活用户进程；无第二套分配器、PID或调度状态。
- 可随本次迁移把文件状态命名为`g_tasks/g_processes/g_task_count/g_current_task`；
  保留槽索引与公开ID的原有含义，存储地址稳定，不复制或整体赋值活Task。

`process.hpp`首先声明以下接口（置于`namespace tinyos`，加include guard并直接包含所需头）：

```cpp
class Process {
public:
    constexpr Process() = default;

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&&) = delete;
    Process& operator=(Process&&) = delete;

    void adopt(UserAddressSpace& source) noexcept;
    const Sv39PageTable* root() const noexcept;

    void record_exit(TaskExitInfo result) noexcept;
    int release_resources();
    const TaskExitInfo& exit_info() const noexcept;

    bool resources_empty() const noexcept;
    int reset();

private:
    UserAddressSpace user_space_{};
    TaskExitInfo exit_info_{};
};
```

这轮不增加自动释放析构；现有释放依赖切根且可失败，必须显式处理。
类型不可复制/移动，避免物理页拥有关系被意外复制。

### 基本操作的完整契约

| 操作 | 责任与前提 |
| --- | --- |
| `adopt(source)` | 调用前已完成来源的启动检查、目标为空且结果为NONE；仅转移完整space并将source置为`UserAddressSpace{}`，无失败分支，不分配页 |
| `root()` | 返回借用的私有root，用于选根和用户复制，不交出可变的整个描述 |
| `record_exit(result)` | 保存可信内核结果；调用方构造REQUESTED/status或FAULT/cause，未使用字段置0；不切根、不释放、不调度 |
| `release_resources()` | 调用前已切到内核根；调用既有外层地址空间release，返回原SV39结果；失败保留剩余描述和exit_info，成功仍保留exit_info |
| `exit_info()` | 返回内核只读结果；这是内部记录，不是用户wire格式 |
| `resources_empty()` | 统一判断空间描述已空，不只是root为空；空根/内核模板借用/内部root/记录数量和剩余内容描述一致。内部描述细节在此或MM助手集中处理，不散到scheduler |
| `reset()` | 只有资源已空才把结果清到NONE并返回0；资源未空返回-1且对象不变，不能用它代替释放 |

先写两个只读访问、`resources_empty/reset`、`adopt`、结果保存和资源释放。
这一步在Task接线前可以单独编译Process模块，随后把旧字段使用集中迁移。

### 与昨晚allocate衔接

沿用原选槽和ID耗尽检查，`task_allocate(Task** task_out)`仍可保持同样的输出形式。
找出`slot_index`后，先对`g_processes[slot_index]`调用`reset()`；若失败，Task、ID和高水位均不变。
成功后再初始化该Task，设process为nullptr，准备context，提交ID及必要的高水位增长。
同槽Process重置后没有新的可失败分配，原有“失败不消耗ID”保持。

`task_create_user`继续在allocate前完成现有entry/sp/space检查。allocate成功后，
由已知属于`g_tasks`数组的Task指针取回同槽索引，关联`g_processes[slot_index]`，
调用无失败的`adopt(*space)`、设置USER kind及初始寄存器。成功转移后源space为空。
普通内核创建保持KERNEL且process为nullptr。

boot阶段`sched_init`先确认没有Process仍拥有空间，再重置各Process和全部槽元数据；
不能清空尚有资源的对象，也不能在失败前先改计数或内核根配置。idle单独初始化且不绑定Process。

### sched中其余迁移及B11收集

1. `change_page`：当前Task有关联Process时借用其root，否则使用kernel root。
   活用户Task要求私有root存在；DEAD用户仍可保留Process指针但不再次调度。
2. 当前用户校验包含USER kind、非空Process、RUNNING、有效私有root及原SPP/索引前提。
   root getter通过Process访问，结果类型仍由独立头提供。
3. 终止编排保持：构造结果并保存→标DEAD→切kernel根→显式release→按公开task_id输出结果→reschedule。
   资源释放后Process指针和结果保留到收集完成，不能提前解绑。
4. 沿B11既有接口实现按公开ID查找、`task_peek_exit`及`task_reap`；
   先检查Task身份/种类/状态/非当前目标，再通过Process读结果或判断资源为空。
   成功reap时先reset Process，再清Task元数据/process指针，最后置UNUSED。
   不改高水位、不重新释放物理页。
5. 最后接SYSCALL_REAP、24字节wire、用户封装及U1演示；原“先copy成功再reap”协议不变。

C++迁移时`task_enter`须显式`extern "C"`，公开函数沿用头中的C链接；
C compound literal改为标准C++20初始化，空值用nullptr。不要同时保留同名sched.c与sched.cpp，
避免Make隐式规则选错源。仅替换源文件不需要make clean，显式重建sched.o或make -B即可。

验收沿用B11原P0，协作者承担；先确认拆模块后原双U/idle仍正常，再验证收集/复用。
本节没有增加作者测试框架作业；当前仍是接口任务，不能声称Process已实现或已运行通过。

## 本轮范围与验证

本次实际改变：抽出架构 context 和结果头、减少调度头依赖、接入规范与工具配置。
保留作者 B11 分配器草稿，未代写 peek/reap/syscall，不标记 B11 已完成。

初次构建在作者新增的 `<limits.h>` 处失败：交叉 GCC 的该头引入不存在的 Linux LP64 libc
stub。改用编译器已有 `__INT_MAX__` 表达同一个 int 上限，保留原来的 ID 耗尽检查。
这是当前 GNU/Clang freestanding 工具链的局部适配，不新增目标 libc 依赖。

构建、宿主测试、头自包含与正常/idle QEMU 的证据保存在
`.cache/style-task-context-2026-11-06/`。验证结果：默认构建无诊断、ELF无未定义符号；原82组宿主场景及汇总通过。
三个相关接口头分别独立按目标C11和C++20编译通过，排版配置检查通过。
正式16秒QEMU双U正常结束、两次status0，结束后至少两次S timer且无fault/halt。
独立20秒idle副本只让A有限次输出后返回：两S均返回，idle进入一次，两U仍各三次counter OK并
正常退出，最后退出后15次S timer、零U timer。目标调试布局确认context4112字节、
Task4464字节、frame288字节、stack偏移16，全部五个栈顶及初始frame均16对齐。
QEMU由观察计时器停止，不代表客体自行关机；原B11完整验收仍未执行。
