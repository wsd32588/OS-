# BATCH17：分离用户任务操作

状态：2026-11-11作者完成主体迁移和缺口修正后，协作者按明确授权拆为三个实现文件；功能及全部既定P0通过。
共享接口/显式依赖保持，功能/P0及原三题原答/集中审查/修正版均已归档，本批收尾。
汇总`.cache/batch17-final-20261111/summary.json`；未提交推送。

导航：[批次索引](README.md) · [上一批](BATCH16_TASK_TABLE.md) · [模块边界](../TASK_MODULE_DESIGN.md)

## 目标

把用户任务创建发布、FORK、EXEC和退出资源事务从sched.cpp提到无状态的user_task模块。
scheduler保留可信当前任务/用户来源/SIE校验，以及运行选择和重调度；新模块显式接收所需依赖，
不通过extern读取scheduler全局，不反向调用sched_reschedule或公开C调度入口。

可观察结果保持B16：原正常ID2/4/5/3退出0/23/37/0，父子关系、fork快照、EXEC身份、
孤儿收养、先copy后reap、页面回收及500轮压力不变。不是增加新syscall或改调度策略。
TaskTable、Process、MM、arch和内置ELF配置保持现有职责，不新建另一套进程/任务存储或生命周期计数。

## 文件与职责

| 文件 | 本批操作 |
| --- | --- |
| include/task/private/user_task.hpp | 纯C++接口，namespace tinyos::user_task；类型按需要前置声明 |
| kernel/task/user_task_create.cpp | create/fork及私有快照发布、候选清理助手 |
| kernel/task/user_task_exec.cpp | exec及私有候选清理、提交失败处理 |
| kernel/task/user_task_exit.cpp | finish/find_reaper及私有孩子扫描/转交、退出释放与诊断 |
| kernel/task/sched.cpp | 原公开C函数改薄壳，保留当前状态、启动登记、peek/reap权限、调度与S任务创建 |
| Makefile | OBJS使用三个user_task_create/exec/exit对象，原user_task.o不再参与链接 |

### 实现文件按事务再分组（2026-11-11讨论）

作者指出user_task.cpp仍像新的大文件。当前418行包含三条可独立阅读的资源事务；
状态拥有关系已经拆开，现可保持同一份user_task.hpp/命名空间/五个接口，再采用如下实现布局：

| 实现文件 | 定义与私有助手 |
| --- | --- |
| kernel/task/user_task_create.cpp | create、fork；快照发布和fork候选清理助手 |
| kernel/task/user_task_exec.cpp | exec及其候选清理/提交失败停机助手 |
| kernel/task/user_task_exit.cpp | finish、find_reaper；孩子扫描/转交、退出资源释放和日志 |

用户随后明确要求协作者直接拆分；作者已先修SPIE、finish薄壳和reaper<0判断，协作者保留这些修改，
按上述边界移动函数，Make已替换三对象，原实现已移除并在本地证据目录保留拆分前副本。
不增加新的拥有状态类或拆散同一个提交事务，接口与验收不增加。
find_reaper仍为头中声明的模块接口，其他仅本文件用的助手留各自匿名命名空间。

公开sched.h、syscall编号、用户包装和用户镜像均不改。syscall/trap仍调用原公开C接口。
host CMake不直接编译含CSR/fence/切根的user_task.cpp，架构行为在目标/QEMU验证。
不同时拆process/spawn.cpp内置镜像配置；exec只调用已有prepare_builtin_user_space，
不能改为调用process_spawn_builtin而创建另一个Task。

## 内部接口（完整草图）

```cpp
#ifndef TINYOS_PRIVATE_USER_TASK_HPP
#define TINYOS_PRIVATE_USER_TASK_HPP

#include <stdint.h>
#include "task/task_exit_info.h"

struct task;
struct trap_frame;
typedef struct Sv39PageTable Sv39PageTable;
typedef struct UserAddressSpace UserAddressSpace;
namespace tinyos { class TaskTable; }

namespace tinyos::user_task {
int create(TaskTable& table, const Sv39PageTable* kernel_root,
           uintptr_t entry, uintptr_t stack_top, UserAddressSpace& source,
           uintptr_t argument, int parent_id);
int fork(TaskTable& table, task& parent, const trap_frame& frame);
int exec(task& current, trap_frame& frame,
         const Sv39PageTable* kernel_root, uintptr_t argument);
void finish(TaskTable& table, task& current, const trap_frame& frame,
            const Sv39PageTable* kernel_root, int reaper_id,
            TaskExitReason reason, long status);
int find_reaper(const TaskTable& table, int reaper_id) noexcept;
}

#endif
```

引用参数表示已经由可信内部入口取得的对象；不构造一个装入整套scheduler状态的万能Context。
这些函数没有常驻状态，不需要再创建一个持有全局表/根/current副本的UserTask对象。
修改操作的SIE0由薄壳确认；fork/exec/finish还要求当前RUNNING USER、非空Process/root和用户来源SPP，
模块按此前提执行。create允许原受控boot入口，父ID由薄壳按boot/运行期规则解析，用户不能任意传入父身份。

## 每个操作的完整契约与迁移范围

### find_reaper

迁移原find_valid_user_reaper_slot主体，将g_task_table和g_user_reaper_id替换为显式参数。
按公开ID查非UNUSED槽，检查USER、非DEAD、非空Process/root、parent=-1；成功返槽，失败-1。
只读、不切根、不改关系，调用方保持SIE0。scheduler的user_creation_parent_id调用此接口；
finish普通退出分支也复用它，避免再复制一套合法性判断。

### create：借入空间的最后发布

迁移原task_create_user的entry/stack/space/root一致性与内容页存在检查、allocate及最后发布。
kernel_root来自参数，不读取g_kernel_root；parent_id是薄壳已解析的创建者公开ID或受控boot的-1。
所有可失败校验必须在table.allocate之前。成功后填写用户frame的PC/SP/a0/SPIE/gp，
通过slot_of/process_at绑定同槽Process、adopt(source)、设USER及parent；成功返新公开ID。
不改模板根、原任务或外层配置，不在allocate之后增加普通失败分支。

source为借入的待发布空间：普通失败返回-1，source完整保持，由原boot/SPAWN调用者处理；
create不能擅自release它。成功转移后source为空，调用者不再拥有它。
公开C task_create_user仍接受可空指针，薄壳必须先拒绝space==nullptr，再转换成引用。

```cpp
int task_create_user(uintptr_t entry, uintptr_t stack_top,
                     UserAddressSpace* space, uintptr_t argument) {
    if (space == nullptr) {
        return -1;
    }
    int parent_id = tinyos::kNoParentId;
    if (user_creation_parent_id(parent_id) != 0) {
        return -1;
    }
    return tinyos::user_task::create(
        g_task_table, g_kernel_root, entry, stack_top,
        *space, argument, parent_id);
}
```

薄壳解析父身份仍负责bootstrap/current/SIE/reaper规则；它仅只读预检，不消耗ID或改source。

### fork：自己的候选由自己清理

薄壳先用原current_user_task(frame)和SIE检查取得parent，再调用fork(table,*parent,*frame)。
迁移原PC/SP canonical/对齐/用户X与栈RW权限检查、值snapshot、clone、fence.i、快照发布。
私有create_from_frame也移入同文件，显式接收table，不调用scheduler的创建入口。

`const trap_frame&`不是交给孩子共享：clone前仍先保存`const trap_frame snapshot = frame;`，
新Task使用自己的context.frame，按值复制snapshot后设子a0=0，parent_id填parent.task_id。
dispatcher已经推进sepc，模块不再+4；模块不写父frame的a0，父返回新ID仍由dispatcher写回。
FORK自己的candidate在模块内创建，clone或发布普通失败在模块内统一release，失败清理再失败沿原停机策略。
薄壳只返回结果，不再次释放candidate；不会复制Process或Task对象。

### exec：完整迁移替换事务

薄壳取得当前合法USER并确认SIE0，调用exec(*current,*frame,g_kernel_root,argument)。
迁移candidate准备/失败清理、root一致性/页记录/PC/SP/权限检查、fresh frame和整个提交序列。
候选普通失败清理成功返回-1，原Task/frame/root/parent保持，可回原用户代码重试。

提交顺序保持：激活安全kernel root → release旧空间 → adopt候选 → 覆写fresh frame → 激活新root。
进入提交后的错误沿原exec_commit_fail停机，不改为普通return -1；不能返回已失效的旧环境。
成功返0只表示内部操作完成，新frame.a0必须保留用户入口参数，PC保留新入口。
原SYSCALL_EXEC分支只在失败时写-1，不在成功时把a0写0，不额外推进新PC。
身份、父关系、Task/Process/内核栈及frame地址保持，不调用allocate或context_init。

### finish：完成终止后返回，由薄壳调度

迁移原finish_user_task的已通过入口检查之后的内容，以及task_destroy、has-child、reparent等私有助手。
依赖是显式table/current/frame/kernel_root/reaper_id及可信reason/status，不读取current_task等全局。
顺序保持：

1. 若当前为根，检查没有任何直接孩子（含DEAD未收集），有则按原规则诊断停机；无则允许退出。
2. 普通任务验证指定根，转交所有直接活/DEAD孩子，自己的parent不改。
3. 保存REQUESTED/status或FAULT/scause并输出原fault诊断。
4. 标DEAD → 激活内核根 → Process.release_resources；释放失败仍按原规则停机。
5. 输出原id/status或id/cause日志，然后正常返回void。

根本人无孩子退出不无条件查询另一个存活根；普通退出找不到合法根仍诊断停机。
父/退出结果及静态内核栈保留到后续wait/reap；finish不reset Process、不回收槽、不选下一任务。

薄壳形状如下；它同时供原sched_on_exit与sched_on_user_fault调用：

```cpp
static trap_frame* finish_user_task(
    trap_frame* frame, TaskExitReason reason, long status) {
    task* current = current_user_task(frame);
    if (current == nullptr || !supervisor_interrupts_disabled()) {
        return frame;
    }
    tinyos::user_task::finish(
        g_task_table, *current, *frame, g_kernel_root,
        g_user_reaper_id, reason, status);
    return sched_reschedule(frame);
}
```

finish返回时当前已经DEAD且私有root已空；薄壳必须直接使用原frame重调度，
不能再调用要求RUNNING/非空root的current_user_task来重新取得它。current槽及实际旧内核栈尚未切换，
所以不能在这之前recycle该槽；旧context.frame仍在后续reschedule按原流程保存。

## 留在scheduler的内容

- 唯一TaskTable、current_task、idle_task、kernel root和reaper ID配置。
- supervisor_interrupts_disabled、is_boot_task_context、current_user_task_metadata及带frame/SPP校验。
- boot登记、运行期父身份解析，task_peek_exit/task_reap的当前父权限与copy后消费接线。
- S任务入口与创建、sleep/timer/yield/唤醒、选下一任务、保存现场、切换运行根、scheduler_start。
- 原公开C签名及薄壳；不把frame/current整个状态以extern散到新文件。

正常调度选根与用户事务中的安全切根职责不同，原各自顺序都要保留。
用户操作实现可调用MM/Process和prepare_builtin_user_space，但不调用task_create_user或reschedule回到scheduler。
SPIE等新文件需要的常量使用实现内constexpr；不把宏放入公共头污染其他编译单元。

## 原实施顺序（已完成）

先建立头与实现、Make对象，迁find_reaper和create；随后迁快照发布/FORK、完整EXEC，最后迁finish与私有助手。
每部分保留原C薄壳，接线后删除对应旧主体，确保只有一份实际操作。
这是整批迁移顺序，不要求逐行验收；原日志、错误返回与资源归属保持，整批后集中审查。
不同时重命名全部旧类型、重排全仓或调整优化/页布局；B16已有非功能冗余可局部整理，不另开功能任务。

## 验收与分工（协作者承担P0）

| 等级 | 场景与预期 |
| --- | --- |
| P0 | make/宿主通过，无新增诊断或未定义符号；原C头接口保持，正常用户RX4034/原O0一页断言不变 |
| P0 | 原双S/U及spawn/fork/exec/wait链保持ID2/4/5/3、退出0/23/37/0，frame/栈/父关系与五份AS free35/alloc21/净14、reap零PMM保持 |
| P0 | make test-stress默认500轮保持通过：2000个子进程、1000 spawn/1000 fork、125 exec/125 fault、500槽满拒绝、每轮PMM和记录完全回收 |
| P0 | 一个自然五槽孤儿场景保持，转交零PMM，子现场/空间及父结果保持，根实际收集两者后可退出 |
| P0 | 一条准备阶段普通失败保留旧frame/空间/父身份，候选完整清理后能真实重试；适配既有EXEC失败探针，不要求重跑整个历史矩阵 |
| P1 | 更多fork/exec分配失败点、DEAD孤儿重复矩阵、内部停机受控试验，按需要选做 |
| P2 | 多ELF/路径式exec、阻塞wait、多hart、动态栈或完整自动测试平台，不属于本批 |

例行命令`make -j4`、`make test`、`make test-stress`、`timeout 25s make qemu`、`git diff --check`。
探针位置随新模块适配由协作者完成，不交给作者重造框架；不执行make clean或自动提交推送。

## 实现与全部P0验收结果（2026-11-11）

三个实现文件分别152/110/152行。共享user_task.hpp不改，scheduler仍掌握当前执行上下文与调度，
各操作只接收显式参数。拆分前用户代码保存在`.cache/batch17-split-20261111/`；
11个迁移函数主体与拆分前比较，忽略格式和匿名命名空间内多余static后全部一致。
原Spp/退出薄壳/reaper判断已由作者同期修正，协作者没有覆盖这些修改。

| 既定验证 | 结果 |
| --- | --- |
| 构建/宿主 | make -j4、make test-stress中的原宿主及500轮TaskFactory全过，无新增诊断，kernel无未定义符号；正常用户镜像保持原接口与一页布局 |
| 正常主线 | 双S/U链ID2/4/5/3退出0/23/37/0；parent、EXEC身份/新frame及旧frame存储、内核栈保持；五份空间35free/21新alloc/净14，三次reap零PMM，退出后两S ticks |
| 真实500轮压力 | 1根+4worker五USER槽，2000个子进程，spawn1000/fork1000/exec125/fault125、槽满拒绝500；逐轮记录/页面完全回收，runtime alloc18375/free18382含根最后7页 |
| 活孤儿 | 五槽自然SPAWN父exit17，孩子parent2→3；Task/frame/栈/root/四内容页/结果保持，转交零PMM；根真实收父与孩子后退出，后续timer正常 |
| EXEC失败/重试 | 第一次EXEC第4次PMM分配注入NULL，已分配候选3页唯一释放、根最后；无activate，旧Task/parent/frame/satp/页表及全部用户页字节、kernel模板和PMM余额保持。旧用户代码实际验证999/5678/EE并继续；第二次EXEC真实成功，7alloc/7旧free、kernel→new根顺序、fresh USER frame/参数2保持，同ID孩子退出37由父收集 |

汇总`.cache/batch17-final-20261111/summary.json`；normal、orphan、exec-failure子目录保存隔离源码/驱动/日志。
压力证据`.cache/task-table-stress/20261111-230747-30/results.json`，make-test-stress.log和宿主日志副本在汇总目录。
验证期间正式源码哈希保持；只读观察通过新模块的显式table参数完成，没有伪造父关系。
只验证既定单hart/固定五槽范围；未扩展P1/P2故障矩阵，QEMU由外部限时停止，不等同客体关机。

## 功能后的理解收尾（原三题）

1. 新user_task模块为什么不直接读current_task/g_kernel_root？TaskTable、scheduler与用户事务分别维护什么？
2. create借入source、fork/exec自己创建candidate，失败后的清理责任为何不同？为什么EXEC成功不能把a0写0？
3. 为什么finish返回后不能再次校验当前任务为RUNNING？谁负责释放用户页、谁负责回收槽、谁负责下一次调度？

作者原答、集中审查和协作者修正版将保存在本文件，不要求重复答题或追加测试门槛。

### 作者原答（2026-11-11，逐字保留）

```text
读取scheduler全局那不是和没有拆出来差不了多少吗？而且不能为了一时简单就直接把任务，表与内核根直接对全局开放
source不动，fork内部discard_fork_candidate(candidate)清理，exec是exec_commit_fail，因为a0是参数不能随便改
finish之后已经    task_destroy(&current, kernel_root);了，没必要检验了，user_address_space_release释放用户空间，回收槽位是recycle_dead_slot，下一次分配时sched_reschedule
```

### 集中审查与协作者修正版

1. 第一题方向正确。跨文件直接读取scheduler全局会保留隐藏依赖与状态耦合；内部函数接收受控借用，
   不代表把任务表/根指针作为公共全局开放。scheduler拥有当前运行与调度配置，TaskTable拥有固定槽/ID，
   user_task编排具体资源事务，Process/MM维护各自资源，函数签名体现所需依赖，不能再各建一套拥有状态。
2. create在失败时保持借入source，由原调用者决定清理；成功adopt时转移并清源，不能笼统说永不修改。
   fork自有候选由discard_fork_candidate清理，这点正确。
   exec准备失败由显式user_address_space_release(&candidate)或reject_candidate清理并返回-1；
   exec_commit_fail不是候选清理函数，它负责原提交阶段出错后的诊断停机。
   成功的新frame.a0是新程序入口参数，应保持；内部返回0不写成用户a0。普通失败仍回旧程序，以a0=-1返回syscall错误。
3. task_destroy实际标DEAD并释放用户空间，Task记录和内核栈仍保留；finish之后要求RUNNING/非空用户root会拒绝正常完成的退出任务。
   入口的来源/权限检查已经通过，后续按预期后置状态直接把原frame交给sched_reschedule。
   用户空间由Process.release_resources调用user_address_space_release；槽回收由TaskTable.recycle_dead_slot；
   新Task分配是TaskTable.allocate，下一次运行哪个既有READY任务由sched_reschedule决定。
   调度不领取新ID或分配新Task，找不到READY时使用已有idle现场。

理解缺口已集中澄清并归档，不要求重答或新增测试。本批完成，下一方向为不同内置ELF选择与EXEC，
具体完整接口/演示/验证到下一批交付。

## 准备证据与限制

### 2026-11-11实现集中复查（历史，以下缺口均已修正）

作者在协作者读取/编译期间继续修正了create父ID、finish参数/指针/私有调用等初始错误；
本记录以下只列最新稳定源码仍存在的项，旧编译日志不再当作当前缺陷。

1. exec的fresh_frame.sstatus使用kStutasSpp=(1<<8)，错误设置SPP，必须改为kSstatusSpie=(1<<5)，
   保持返回U-mode及SPIE。错误Spp常量可移除；这不是单纯命名问题。
2. sched.cpp的finish_user_task入口检查后仍为空，缺user_task::finish调用及return sched_reschedule(frame)。
   强制重编实际报告reason/status未使用和非void缺返回值；补调用后直接重调度，不再校验已DEAD的current。
3. sched.cpp保留旧exec_commit_fail，已经无调用，实际产生unused警告，应移除重复定义。
4. scheduler原find_valid_user_reaper_slot仍复制完整验证，宜转调user_task::find_reaper(table,id)，
   保持一份收养者合法性逻辑。

当前user_task.cpp经目标强制重编无诊断，scheduler为上述4条warning（2参数、1缺返回值、1旧助手）。
证据`.cache/batch17-preparation-20261111/current-review.json`及本机`/tmp/tinyos-b17-review-forced-build.log`。
没有运行已知存在退出/特权位缺口的QEMU，没有代改作者产品源码。

2026-11-11作者create草稿集中检查：不可拒绝argument=0或可信boot parent=-1；
user_memory_find_page需要&source.user_memory；新实现须包含arch/riscv/trap.h才可访问完整frame；
旧user_stack_top/user_argument改为stack_top/argument，context.sta改为context.frame->sstatus；
SPIE用实现内constexpr，不依赖sched.cpp的局部宏。adopt已清源，额外source={}可省。
本轮只给修正位置，没有代改作者实现或声称新模块已编译通过。

`.cache/batch17-preparation-20261111/results.json`：拟定私有头及全部调用签名按RV64 C++20语法检查无诊断。
独立只读审查确认可空C入口、候选归属、dispatcher PC/a0、finish后直接重调度与根退出边界。
未实现或运行正式新模块，以上是完整迁移任务，不将准备验证视为功能完成。
