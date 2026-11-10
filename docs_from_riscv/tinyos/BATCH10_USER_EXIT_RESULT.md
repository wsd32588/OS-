# BATCH10：用户退出码与异常终止原因

状态：功能、全部既定P0及三题原答/集中审查/修正版归档完成，整批收尾。日期：2026-11-05。

导航：[批次索引](README.md) · [上一批](BATCH09_TWO_USER_TASKS.md) · [总路线图](../TINYOS_EVOLUTION.md)

## 目标与可观察结果

当前用户main正常返回、fail(reason)退出和用户fault最终都清理任务，但调度器没有保存三者的结果。让每个用户任务留下终止原因及对应数据：用户请求退出时保存退出码，硬件异常终止时保存scause。之后实现等待/收集结果时才有可读取的记录。

本批只做用户终止结果，沿用已经验证的切内核根、显式释放完整UserAddressSpace和重调度。作者写核心，协作者集中审查并承担P0。

正常演示仍为两S、两U；main返回0，两用户仍各完成三轮计数、时间与数据检查。每个用户释放成功后增加一行内核结果日志，例如：

```text
[task] user exit id=0x0000000000000002 status=0x0000000000000000
[task] user exit id=0x0000000000000003 status=0x0000000000000000
```

这里id是调度器任务槽2/3，不是user_tag的0/1；不要求全局日志顺序固定。显式user_exit(23)应记录23，fault则记录FAULT及硬件scause，而不是伪造一个普通退出码。

## 接口与数据语义

user/user_api.h的C与C++分支一起修改，保留noreturn：

```c
/* C分支 */
_Noreturn void user_exit(long status);
/* C++分支 */
[[noreturn]] void user_exit(long status);
```

目标为RV64 LP64。主动退出码按完整有符号long保存，不截成8位或32位；日志可以用现有uart_put_hex输出其unsigned long位模式，不新增十进制格式化。main仍返回int；其返回值在当前RV64 ABI的a0中符号扩展，而直接user_exit可传完整long。

include/task/sched.h与kernel/task/sched.c同步使用两个用户终止入口：

```c
struct trap_frame* sched_on_exit(struct trap_frame* frame, long status);
struct trap_frame* sched_on_user_fault(struct trap_frame* frame);
```

第一个只表示用户请求EXIT，第二个只表示当前已经识别的用户同步异常。两者在sched.c内调用同一个私有终止助手；用户结果类型和私有助手不需要暴露在公共头中。建议的结果字段如下，命名可沿用这份约定：

```c
typedef enum TaskExitReason {
    TASK_EXIT_NONE,
    TASK_EXIT_REQUESTED,
    TASK_EXIT_FAULT
} TaskExitReason;

typedef struct TaskExitInfo {
    TaskExitReason reason;
    long status;
    unsigned long fault_scause;
} TaskExitInfo;
```

在现有struct task中增加TaskExitInfo exit_info值成员，不改变trap_frame或汇编偏移。

| reason | status | fault_scause |
| --- | --- | --- |
| NONE | 0，占位 | 0，占位 |
| REQUESTED | 用户传来的完整long；0通常表示成功，非0仍属于请求退出 | 0 |
| FAULT | 0，占位，不表示用户成功退出 | 原始frame->scause |

由reason决定哪个字段有意义，不用status的正负或某个特殊值推断是否fault。现有trap日志仍保留scause/sepc/stval，不要求新增更多快照字段。

## 用户到内核的接线

1. user/main.c的fail(reason)先输出原因，再user_exit(1)，正常路径仍return 0。输出失败也要退出，不因诊断失败回到正常路径。
2. user/start.S已有call main后call user_exit，返回值本来就在a0，保留即可。不要在两条call之间把a0清零或换成启动tag。
3. user/syscall.S的user_exit已经只设置a7再ecall，保留a0即可，无须新汇编文件或新syscall编号。
4. kernel/task/syscall.c的EXIT分支先取出(long)frame->a0，再调用sched_on_exit(frame, status)。不要先清a0，也不要把退出码当指针复制。分支局部变量用花括号包住。
5. kernel/task/trap.c已识别的用户fault分支改为sched_on_user_fault(frame)。保留已有详细日志，不经syscall_handle，不额外把fault的sepc加4。S态/未知trap仍按现有规则停机。

## 调度器如何保存并完成终止

私有助手仅组织下列阶段，释放细节继续归现有地址空间模块：

1. 修改任何状态前，检查frame非空、0<task_count<=MAX_TASKS、0<=current_task<task_count、当前状态RUNNING、确为用户任务（持有非空用户空间且frame的SPP为0）。无效调用返回原frame并且零修改，包括NULL输入返回NULL；不能先索引tasks[current_task]再检查上界。两个公开入口复用同一套检查。
2. 保存当前任务id，写入exit_info；REQUESTED填status并清fault_scause，FAULT保存frame->scause并把status置0。不要在无效frame上先取scause；可以在私有助手完成前置检查后按reason读取。
3. 把当前任务标为DEAD，先sv39_activate(g_kernel_root)，再user_address_space_release当前完整外层空间。原有错误检查/停机与失败保留语义继续有效。
4. 完全释放成功后，用保存的任务id与exit_info输出一行结果：请求退出输出status；fault输出scause。这里处于内核，可使用uart_puts/uart_put_hex；用户程序仍只通过user_write输出。
5. 最后调用原sched_reschedule。不要在重调度后使用current_task输出旧任务结果，因为它已经可能指向下一任务。

结果与地址空间是两个不同寿命：空间释放成功后为空，但DEAD任务的exit_info仍保留。不要用整个task={0}去清结果或清仍在使用的内核栈/frame。task_allocate清新槽的exit_info；当前sched_init只在boot阶段调用，在初始化时清所有任务槽及idle的旧结果字段，不改动既有UserAddressSpace所有权流程。初始化全零对应NONE；S任务task_exit保持现状，本批结果只描述用户任务。

fault结果日志例如：

```text
[task] user fault id=0x0000000000000002 scause=0x000000000000000d
```

cause13为用户load page fault。一个用户fault时仍只回收自己7页，另一用户能继续，最后合计恢复14页；内核根和共享表不释放。

## 范围与文件

| 文件 | 作者修改 |
| --- | --- |
| user/user_api.h | C/C++两个user_exit声明增加long参数 |
| user/main.c | fail调用传1，正常main保持返回0 |
| include/task/sched.h | 同步请求退出入口，新增用户fault入口 |
| kernel/task/syscall.c | EXIT取a0的退出码再转交 |
| kernel/task/trap.c | 已识别用户fault转交专用入口，保留CSR日志 |
| kernel/task/sched.c | 私有结果类型/成员、初始化、共用终止流程、结果日志 |
| user/start.S、user/syscall.S | 核对a0已直通，通常无需修改 |

不增加wait、父子关系、getpid、fork/exec、任务槽复用、动态栈或查询syscall；本批结果留在静态任务记录中并以日志观察。不同时迁移整个调度器到C++，不改trap_frame布局、页表/释放算法或buffer检查策略。保留B09同ELF双用户演示；不因测试临时值23或fault而永久替换正常演示。

检索真实接线：

```sh
rg -n 'user_exit|sched_on_exit|sched_on_user_fault' include kernel user -g '*.h' -g '*.c' -g '*.cpp' -g '*.S'
make -j4
timeout 16s make qemu
```

旧批次中的无参接口保留历史语境，不批量改写历史文档。保留已有syscall编号EXIT=3。timeout的124仍表示观察结束，结合成功标记、结果和后续tick判断。

## 验收与分工

作者完成核心和普通make/qemu后集中检查。以下P0由协作者承担，不要求作者写新测试或故障注入框架；P1/P2均选做，不阻塞下一批。

| 等级 | 场景与明确预期 | 学习点及责任 |
| --- | --- | --- |
| P0 | 完整构建无新增警告/未定义；原82组宿主通过；用户代码仍一页，调用签名和trap_frame布局一致 | 协作者例行复验，不新增镜像实现式宿主测试 |
| P0 | 正式双U保留B09标记；两任务记录REQUESTED/status0/cause0，完成释放后结果仍存，两次7页归还且后续S tick正常 | 作者普通观察；协作者核对实际task记录及资源 |
| P0 | 临时只让U0的main返回23，记录id2/REQUESTED/status23（十六进制0x17），U1正常0；不影响另一用户和7+7回收 | 协作者确认main→_start→a0→EXIT完整链路 |
| P0 | 临时让U0直接读取未映射0x40002000，得到FAULT/cause13；U1继续原完整检查，然后显式user_exit(-1099511627776L)，记录REQUESTED和完整负long位模式0xffffff0000000000；仍各回收7页 | 协作者一份临时副本同时验证异常与主动退出区别、幸存者和64位值未截断；不是调用WRITE坏指针来冒充硬件fault |
| P0 | 临时注入NULL frame或current_task==task_count，两个入口均返回原frame，结果/任务状态/拥有描述/PMM/satp零修改；随后恢复现场继续正常执行 | 协作者一条拒绝路径，不给作者追加矩阵 |
| P1 | 用一次fail(reason)验证主动失败码1；更多非法上下文与负的main int返回值 | 选做，可只讨论传值与拒绝逻辑 |
| P2 | 用户查询、wait、父子关系、槽回收、全异常码或全故障矩阵 | 后续独立批次，本批不实现 |

## 功能梳理与理解收尾

实现后集中梳理“main返回或显式退出→a0→EXIT结果记录→切根→释放→结果仍存→调度下一任务”及“用户fault→原始scause→FAULT结果→同一资源释放流程”。默认三题：

1. 为什么main返回23不需要给_start新加一段搬运参数的汇编？a0何时表示启动tag、何时表示退出码？
2. 为什么主动user_exit(13)与cause13不能只存一个数字来区分？FAULT的status=0为何不代表成功？
3. UserAddressSpace已经清空，为什么exit_info还要留在DEAD任务中？它属于谁，什么时候重置，和以后等待收集结果有什么关系？

### 作者原答（2026-11-05）

以下保留作者本轮合并原文：

> 23是传入的参数吧，被a0直接用到了返回值，因为内核已经做好了a0作为返回值的步骤
> 数字只是传入的值，具体执行还需要根据函数规则行事，用户请求和单纯的寄存器内fault作用不同
> UserAddressSpace消失和exit_info存在不冲突，一个是清空使用的空间以及数据，而exit_info和task共存亡，直到完成之后才被task_destory摧毁

### 集中审查与修正版

第一题抓到了a0的复用，但传值顺序和执行者需要纠正。此场景的23首先是用户main的返回值，不是main接收的启动参数；启动参数仍为tag0/1。用户编译器按当前RV64 ABI把main的int返回值放入a0并符号扩展；_start执行call main后立即call user_exit，已有a0自然成为user_exit(long status)的第一个参数。syscall汇编只写a7而保留a0，trap入口汇编再将它保存到frame->a0，内核EXIT读取并记录，不是ecall硬件自动保存全部寄存器。普通C函数返回值遵守ABI，不是内核替main生成了返回值或专门搬运23。显式user_exit(23)则直接由用户调用者把23作为参数放入a0，两条路径在EXIT会合。

第二题理解了请求退出与异常的含义不同，修正版需明确持久记录中的区分依据：user_exit(13)记录reason=REQUESTED/status=13/fault_scause=0；硬件load page fault记录reason=FAULT/status=0/fault_scause=13。13分别属于应用定义的退出码与硬件异常编号，数值相同不表示同一事件。调用函数结束后仍需reason说明哪个字段有效，不能只靠“之前调用哪个函数”或单个数字猜测；FAULT的status0仅为占位，不表示成功。

第三题关于“释放空间与保留结果不冲突”正确，但task_destroy的清理范围需纠正。当前TaskExitInfo是静态tasks数组中task的值成员，与user_space是并列字段；UserAddressSpace release归还用户内容/私有表/根共7页并清零空间描述，不删除整个task，其值成员本身的存储也仍在。task_destroy标DEAD、切根、释放用户空间，刻意保留exit_info及静态内核栈；它不会在稍后再次销毁结果。当前结果在sched_init或task_allocate中清空，没有wait或收集后销毁Task/复用槽的实现。保留记录正是为了未来可由等待者读取结果；未来何时收集及复用另行设计。

本批三题已集中审查并归档，不要求反复重答。上述修正版由协作者解释整理，未冒充作者再次复述。此次仅核对已有源码并更新文档，沿用最终P0证据，未改产品或重跑测试，不自动开启下一批。

## 准备记录

2026-11-04：作者要求继续后，协作者核对当前user_start/syscall/EXIT分发/用户fault/sched_on_exit，确认目前无参EXIT不保存main结果，用户fault共用旧入口。独立设计审查确认签名、结果字段和共用前置检查范围；只交付任务，不代写正式产品。B09仍是当前已验证功能基线，本批尚未实现或通过QEMU。

接口准备验证仅在.cache/batch10-task-preparation-gadqheyf/复制header中将两个user_exit声明改为long参数：实际RV64 GCC的C转交/宽值汇编生成及Clang的C++ noreturn声明语法检查均无诊断，正式源配置前后哈希保持。证据results.json/interface_probe.c/interface_probe.cpp/interface_probe.s；此检查不等于内核接线或本批P0完成。本轮未clean/stage/commit/push。

## 作者要求的前置整理：O3、头文件边界与重复检查（2026-11-04）

作者在开始本批后要求协作者直接帮助改头边界、适当精简检查，并明确先试O3。开始时作者已将cpp_runtime/user_image头改成.hpp并修改实现include，main的fail已传1、user_exit仅C分支带long；协作者保留这些改动，先补test/c_api.hpp的旧include和C++分支签名，再在副本测试O3，不代写本批TaskExitInfo/fault入口。

先验结果：原实现临时副本中内核C、内核C++和用户C均由-O0替换成-O3，完整构建无诊断/未定义；正式双tag标记、时间/数据检查及两次退出通过，退出后8个S tick。独立Release宿主实际16条编译命令均只有-O3 -DNDEBUG，82具体组及汇总通过。证据.cache/cpp-boundary-o3-tfycuwms/o3-baseline/和.cache/o3-host-ye_ps_54/。这一步先于以下逻辑精简。

此次实际修改的范围：

- 沿用作者的两个.hpp：runtime头继续声明汇编/ABI所需C链接，64项注册容量常量移到实现匿名namespace；user_image的声明/定义改为普通C++链接，所有当前消费者均C++。test/c_api.hpp删除统一外围extern C，让每个共享C头自行声明链接。仍有C/汇编消费者的共享头保持现状。
- runtime删除每次先置true再检查的_is_ready冗余状态，重复初始化仍由s_init_started拒绝；finalize零登记时由循环自然执行零次。NULL回调、表满、DSO筛选、清标记再析构和重复finalize语义保留。
- user_image去掉全NULL/全零特判：该输入同样通过后续通用检查，两复制循环零次，仍返回USER_IMAGE_OK==0。其余指针、容量及文件范围检查保留。
- loader只删掉已由逐段检查排除的code/data memory_size==0，以及已由正大小向上除法保证不可能的count==0；C包装、区间、文件范围、容量和槽1限制不变。
- task_create容量判断统一由写入前的task_allocate负责；task_create_user内外根关系直接比较user_memory.root!=space->root，空描述依然拒绝且零修改。固定共享槽0/2改两行赋值。
- Make新增OPTIMIZATION变量，默认仍-O0，三组目标编译flags共用。切换时使用make -B OPTIMIZATION=-O3；Make本身不追踪flag变化，切回默认也要强制重编。

未删除外层归属/共享槽/活动根、内层页记录/映射、用户PTE权限或copy_to跨canonical空洞检查。这些负责不同不变量。UserMemory公开校验入口的数量保护保持，释放的全量预检与unmap→全部flush→内容free→表回收阶段保持，失败重试语义不变。本轮不承诺性能数值提升，多数等价条件原本也可被O3消除，目标是简化维护。

最终复验：精简后实际目标make -B OPTIMIZATION=-O3及默认make -B各28条编译命令分别只有对应优化标志，无警告/未定义；两种级别正式QEMU均通过全部双tag标记与时间/数据检查、两次退出后各8个S tick。当前工作区产物最终重建为默认O0。证据.cache/cpp-boundary-o3-tfycuwms/的o3-final/o0-final日志与results.json。

精简后独立O3宿主82组+汇总通过，含全NULL/0原S4a；16实际命令均-O3 -DNDEBUG且无警告，证据.cache/o3-host-final-3z5d9gf_/。实际O3 runtime探针验证两全局仅构造一次、重复init拒绝、局部析构、NULL回调拒绝、DSO筛选/逆序/重复finalize及entry受控返回无重复析构，证据.cache/runtime-o3-final-npj6zk__/。仅该副本替换main并增加entry核对，3秒runner SIGTERM，未启动用户调度；61正式源配置哈希保持。

精简后O3拒绝探针.cache/simplify-negative-a8xg91tx/通过：code/data memsz0各INVALID、0alloc/free，描述/PMM/satp/shared/任务全状态不变；正式4槽满时task_create及持额外有效7页空间的task_create_user均返回-1且零修改，调用者完整释放额外7页后继续正式双U演示，各三轮/正常退出。副本全目标O3无警告/未定义，61正式源配置哈希保持，16秒runner SIGTERM。故未因移除重复条件而放宽这些边界；无新增作者测试矩阵。

整理中曾遗漏user_image定义外围extern C，构建及时报language linkage冲突；已同步移除并完整重编通过。历史红日志独立保留，不作为当前故障。此整理通过不等于本批退出结果已完成；下一步继续作者的结果记录/退出分发/fault接线。

## 退出/fault草稿集中检查（2026-11-05）

作者已同步user_exit(long)、fail传1、EXIT从frame->a0取long并传sched_on_exit；trap已将已识别U fault交给sched_on_user_fault，fault不加sepc；TaskExitInfo类型和task成员也已增加。_start与syscall汇编保留a0，以上接线正确。

本次只审查，不代改实现。集中待修：

1. sched_on_exit把status==0当无效输入，正常main返回0后直接恢复原frame。0是合法成功退出码，负值也按本批完整long接受，不得以其真假判断参数有效。当前ecall后的用户sepc落到user_exit的j循环，因此任务仍可调度、不执行DEAD/释放。
2. sched_on_user_fault只有诊断和sched_reschedule，没有DEAD、切内核根或完整空间释放。通用调度把原RUNNING重新置READY，下一次恢复同一未推进的故障sepc，重复fault。需要共用终止助手完成资源终止，再返回重调度结果；末尾return sched_reschedule不是完整终止实现。
3. exit_info目前仅有成员，未写reason/status/fault_scause、未在初始化/分配清空、未输出释放后的结果日志。按原任务写入REQUESTED或FAULT，且保留到DEAD记录；类型拼写TASK_EXIT_REQIESTED/resaon应统一为TASK_EXIT_REQUESTED/reason。类型可留sched.c私有，不必公开暴露。
4. 两入口只有task_count==0/current<0/frame空检查，仍缺task_count上限、current上界、RUNNING及用户空间/SPP条件。放在共用助手，先检查索引再取tasks[current_task]，避免两份复制检查。

建议组织仍按原任务：sched_on_exit(frame,status)与sched_on_user_fault(frame)只是给私有助手提供reason/status；助手检查→写exit_info→DEAD→激活kernel→release完整space→输出保存的退出id/结果→sched_reschedule。fault详细诊断目前已移动到专用入口，可保留一次；关键是记录原始scause和完整终止，不要求为了日志位置再次搬动。

make -B -j4实际无警告/链接错误。正式16秒QEMU两end和EXIT日志均各一次，但末次EXIT后仍5个U timer、2个S timer，与status0误拒后用户停在退出汇编循环一致。该默认日志未插页计数，14页未归还为控制流推断，不冒充实测页数。证据.cache/batch10-draft-review-p38rd2vh/（build.log/default-qemu.log/results.json），8个已审源哈希保持；124为timeout。本批尚未进入完整P0，不用旧B09/O3结果覆盖当前草稿回退。

独立临时真实QEMU还复现fault回退：.cache/batch10-fault-draft-fypix00e/含runner/probe/results.json，9秒内同一cause13/stval0x40002000重复6次；每次观察当前task2经调度回到READY，外/内root和4条内容记录保留，PMM未增加。U1仍开始并输出counter OK。仅副本U0开头加入一次实际volatile未映射load、fault函数加只读前后观察；构建无警告，61正式源配置哈希保持，runner SIGTERM结束观察。此探针证明缺少终止阶段，不是本批功能验收。

## task_destroy局部实现（2026-11-05）

作者进一步修正status0拒绝与字段拼写，并在主动退出时保存exit_info，随后明确请求增加task_destory(struct Task*)。协作者仅修改sched.c，沿用实际struct task类型并使用正确拼写task_destroy：

- 私有current_user_task(frame)统一检查NULL、任务数/current上下界、RUNNING、非空用户root和SPP，先检查索引再取tasks元素。
- 私有static void task_destroy(struct task*)只用于校验后的当前用户任务，标DEAD→激活kernel_root→释放完整UserAddressSpace，保留原有失败诊断/停机策略及剩余拥有描述。不会清整个task、释放静态内核栈或清exit_info。
- 主动退出和fault分别保存REQUESTED/完整status/cause0或FAULT/status0/原scause，再调用同一task_destroy，最后sched_reschedule返回下一现场；fault原诊断保留一次。资源helper不负责取用户退出码或重调度。

make -j4实际构建/链接无诊断。正式16秒QEMU两用户start/end各一次、counter OK各三次，Time/Hello/Data/exit各两次；最后EXIT之后无U timer、8次S timer，无失败/故障，124为timeout。证据.cache/batch10-task-destroy-5ob93c64/含sched-before.c、default-qemu.log、normal-results.json。此次只完成作者指定资源helper及必要接线，结果初始化、释放后结果日志和本批其余P0仍待继续，不扩大为整批代写。

独立真实QEMU针对验证.cache/batch10-destroy-probe-fe68zdi5/通过：U0真实load fault仅一次，释放前已DEAD且记录FAULT/cause13，实际satp为内核根；7私有页精确各free一次，根最后且槽1空。U1完整拥有描述、私有表及四内容页哈希保持并完成原演示，记录REQUESTED/status0后再回收7页，两空间空、合14页恢复，两个结果跨后续两次timer仍保留。NULL和current==task_count输入，对两个入口均原样返回，任务全字节/输入frame/PMM/satp及alloc/free/activate计数零变化。副本完整构建无警告/未定义，61正式源配置哈希保持；仅该副本加入fault/只读观察/拒绝调用，16秒runner SIGTERM停止。结果初始化/日志尚未代写，不称本批整体验收完成。

## 作者汇总finish_user_task后的核对（2026-11-05）

作者已亲写finish_user_task串起已有校验、结果赋值、fault诊断、task_destroy及重调度，两公开入口只转交原因和status；两种create也各自清空结果。此次只读核对发现：聚合初始化fault_scause写成frame->sstatus，后面FAULT分支三行赋值又将它纠正为frame->scause，故当前最终结果不受影响，但删除重复赋值前必须先改正聚合初始化。应将正确结果只赋一次，fault分支保留诊断。

正常任务创建的清零目前已存在；建议统一到task_allocate而非两create复制。原任务要求的sched_init清所有槽/idle旧结果与释放后结果日志仍待补，须在重调度前使用已保存任务id。作者只提供timeout尾部，不足以判断完整运行；本轮make显示无需重建，只静态核对，未再跑QEMU或改产品，既有task_destroy探针不冒充新汇总代码的整批验收。

随后作者已完成sched_init所有槽/idle清零、task_allocate集中初始化并删除两create重复，finish_user_task改正scause且删掉重复赋值。最新只读检查仅剩结果日志：unsigned char task_id传uart_putchar会输出槽2/3的控制字节，缩窄类型不等于数字格式化；需用uart_put_hex输出保存的int任务id，并按exit_info.reason输出status或fault_scause，放在task_destroy成功后、重调度前。未改产品/再次验证，初始化不再列为当前待办，日志补齐后集中验收。

## 最终功能与全部既定P0验收（2026-11-05）

作者已补齐保存int任务id和REQUESTED/status或FAULT/scause整行结果日志，输出在task_destroy成功后、重调度前。当前完整路径为：main返回值或显式user_exit(long)由a0进入EXIT，或真实U fault由原始scause进入专用入口；两者转finish_user_task，经统一校验、记录结果、诊断、task_destroy标DEAD/切kernel/完整release，再输出保存id/结果并调度。释放成功只清UserAddressSpace，静态task/内核栈/exit_info保留；初始化/新槽分配清旧结果。TASK_EXIT_REQUESTED不等于成功，是否成功由用户约定的status表达；FAULT的status0仅占位。

协作者本轮只读审查与临时探针，没有改正式产品。完整默认make -B -j4无警告、kernel nm -u为空；user ELF入口0x40000000，RX0x782字节、RW filesz0x11/memsz0x1038，仍一页代码、一页栈、两页数据。make test的82具体宿主组+汇总通过。正式默认16秒QEMU两tag start/end各一次、counter OK各三次，Time/Hello/Data/EXIT各两次，id2/id3 REQUESTED/status0结果各一行；最后结果后8个S tick、无U timer/失败/故障。证据.cache/batch10-final-ap6k83br/含build/host-details/default-qemu日志与结果JSON，61正式源配置哈希保持；runner16秒SIGTERM结束观察，不是客体关机。

独立真实QEMU .cache/batch10-normal-final-vqg1gg7k/含两个可重跑场景：

- normal：真实id2/3结果均REQUESTED/status0/cause0，正式产品结果日志正确。
- main23：仅副本U0最后main返回23，_start/syscall汇编不变，真实id2保留REQUESTED/status23/cause0并输出0x17；id3仍0，确认main→_start→a0→EXIT链。

两场景各保留完整双tag三轮/时间/数据演示。退出先kernel，每次7唯一私有页各free一次、根最后且槽1空；首退幸存者完整描述/私有表/四内容页哈希保持，最后14页归还、双拥有对象空、shared保持，两个结果跨后两次timer仍保留。两场景副本构建无警告/未定义，61正式源配置哈希保持；各16秒runner停止。

独立真实QEMU .cache/batch10-final-fault-wide-dc06i32x/覆盖其余原定P0：

- U0开头实际读取未映射地址，仅一次cause13；scause与sstatus不同、fault PC未推进。真实结果为FAULT/cause13，产品id2故障结果日志在7页释放后输出。
- U1继续全部原检查及三轮计数，末尾直接user_exit(-1099511627776L)，真实结果REQUESTED/完整负long/cause0，产品id3准确输出0xffffff0000000000，未截到32位或8位。
- 两次7页归还、kernel先于free、幸存者保持、14页恢复、双空间空及结果跨timer保留全部通过。
- 初始全部普通槽/idle为NONE；无用户资源的boot阶段两次真实sched_init前仅植入旧结果，均清零；四次真实task_allocate前植入新槽旧结果，实际分配流程均清回NONE。
- NULL与current==task_count两入口均返回原frame，任务全字节/frame/PMM/satp及alloc/free/activate计数零变化。

该副本构建无警告/未定义，61正式源配置哈希保持；仅副本加入fault、宽值退出、结果植入和只读观察，不替换正式终止/分配算法。16秒runner停止。重初始化只在尚无拥有空间时验证，不宣称sched_init可销毁运行中任务；不引入wait、父子关系、槽复用或动态栈，也不扩大故障矩阵。

功能及全部既定P0完成。下一步按原三题集中审查理解，当前尚无作者原答，不标整批理解归档完成；此次未clean/stage/commit/push。前文未初始化/未输出/故障循环等均为历史草稿状态，以本节和文首为准。
