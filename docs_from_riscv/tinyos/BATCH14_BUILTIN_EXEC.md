# BATCH14：保持任务身份并替换内置用户镜像

状态：2026-11-09作者实现与全部既定P0验证通过。EXEC保持同Task/ID5，真实新入口收到tag3，
退出37后由父按原ID收集；准备失败保持旧程序并可重试。原三题原答、集中审查与协作者修正版已归档，本批收尾。

导航：[批次索引](README.md) · [上一批](BATCH13_EAGER_FORK.md) · [总路线图](../TINYOS_EVOLUTION.md)

## 目标与可观察结果

B13的fork复制当前用户内存和现场。本批让这个子任务调用`user_exec(3)`，
保持自己的公开ID和Task槽，丢弃旧用户镜像，从内置ELF入口重新执行。
本批用同一ELF的tag3分支展示替换机制；镜像仍来自永久内置字节，不接路径、多镜像选择或用户ELF。

保留原U0/U1、SPAWN的U2，以及fork子快照/修改/yield检查。
fork子输出原`[fork child] snapshot OK`后调用EXEC，新入口收到tag3，检查counter7/BSS0/data首字节d，
输出`[exec] fresh image OK`、yield后返回37。父U1仍按fork返回的ID收集，现在预期37，验证自己的数据保持。

```text
[U1] reaped U2
[fork child] snapshot OK
[exec] fresh image OK
[task] user exit id=0x0000000000000005 status=0x0000000000000025
[U1] fork isolation OK
[U1] end
```

正常公开ID仍是U0=2、U2=4、fork子=5、U1=3。EXEC不产生ID6，也不新增一个任务或一次旧程序退出事件。
tag3是新镜像入口参数，不是公开ID，也不表示一个新Task。
成功后旧调用点不继续；普通准备失败返回-1，旧程序仍可继续运行。

采用现有单hart/SIE0、共享Supervisor内核映射、四槽静态池和8KiB内核栈。
本批不增加COW、父子权限、阻塞wait、VFS、argv/envp、用户字符串复制、动态内核栈或多hart。

## 文件与模块分工

本批沿现有编译单元实施，无须新增exec.cpp或Make对象：

| 文件 | 作者实现 |
| --- | --- |
| `include/process/spawn.hpp` | 声明C++共享镜像准备助手，直接包含/声明所需类型 |
| `kernel/process/spawn.cpp` | 从现有SPAWN提取新根与ELF装载阶段，复用唯一私有配置；SPAWN改调用该助手 |
| `include/task/sched.h`、`kernel/task/sched.cpp` | 当前任务EXEC编排、局部fresh frame、提交替换与显式切根 |
| `include/task/syscall.h`、`kernel/task/syscall.c` | EXEC10及成功不覆盖新a0的薄分发 |
| `user/user_api.h`、`user/syscall.S` | 带一个数值参数的user_exec包装 |
| `user/main.c` | 共用tag2/tag3 fresh分支，fork子调用EXEC，父收集37 |

Process已有release_resources/adopt足够：不新增可变空间getter、交换整个Process或第二套ID分配器。
MM继续管理页与映射；镜像准备助手组织create/load；scheduler控制活动根、当前Task与frame的替换。
boot保持已有configure_builtin_spawn调用，不新增或复制一份EXEC配置。

## 先提取共享的镜像准备阶段

在spawn.hpp的tinyos命名空间增加：

```cpp
int prepare_builtin_user_space(
    UserAddressSpace& output,
    uintptr_t& entry_out,
    uintptr_t& stack_top_out
);
```

头文件应自包含；可直接包含mm/user_address_space.h。C接口process_spawn_builtin仍在spawn.h，
不能让syscall.c包含C++的spawn.hpp。

契约：

- output由调用者提供局部`UserAddressSpace candidate{}`，前置条件是完整为空。
- 借用spawn.cpp内已有的g_root/g_image/g_elf_layout/g_memory_layout/g_stack_top。
  保留configure_builtin_spawn现有名称与一次配置规则，不暴露这些可变状态，也不新增配置副本。
- 未配置返回SV39_ERR_INVALID_ARGUMENT，零分配，不改变数字输出。
- 先user_address_space_create(g_root,&output)，再按原参数调用user_memory_load_elf。
- 成功返回SV39_OK，output完整拥有新空间，最后才写entry_out和stack_top_out。
- 普通失败返回原SV39错误码，output可能持有根或部分资源，调用者负责统一release。
- 原loader返回SV39_ERR_PAGE_FREE_FAILED时，保留原先的诊断停机处理。
  其他清理异常不能被声称已完全恢复；调用方candidate清理失败也必须停机。
- 助手不发布Task、不分配ID、不切root、不调度，不读调用者已经改写的用户页。

本质是将process_spawn_builtin当前“创建根→装载ELF”的代码移入助手，不重写loader。
loader已有copy后的fence.i，提取时保留该调用链；UAS宿主算法中不加入目标汇编。

SPAWN随后变成：局部candidate/entry/stack_top → 共享助手 → 最后task_create_user → 返回ID。
准备失败和task_create_user失败都清理尚未转移的candidate，cleanup失败停机；
成功后candidate已转交。保留原SPAWN参数0合法、未配置拒绝、失败不提交ID的行为。
不要在助手里清理一次、SPAWN又假定部分输出不存在，保持明确的一层候选清理归属。

## 当前任务EXEC入口与两阶段执行

sched.h的C接口增加：

```c
int sched_exec_current(struct trap_frame* frame, uintptr_t user_argument);
```

内部返回0表示新frame已安装，-1表示普通准备失败且旧程序可继续。
此内部返回值不等于用户新入口的a0。user_argument是任意数值，不由内核限制为3或拒绝0。

### 准备阶段：仍在旧用户根

1. 复用current_user_task(frame)，检查真实用户Task/RUNNING/Process/root与SPP0；读取实时sstatus确认SIE0。
   CPU已在S-mode，但Task仍是USER。整个过程不yield/sleep/reschedule或重开中断。
2. 创建局部candidate{}、entry{}、stack_top{}，调用共享准备助手。
3. 普通失败先释放candidate，清理成功才返回-1；此时旧Process、Task元数据、旧内存、当前root及传入frame保持。
   任何candidate清理错误都诊断停机，不能丢掉拥有关系。
4. 在任何不可逆操作前确认新空间完整，尤其candidate.kernel_root必须等于g_kernel_root，
   candidate.root非空且不等于模板、inner root等于candidate.root；新entry/stack有效且属于候选内容记录。
   可以把task_create_user现有的启动前置检查提成私有只读助手复用，不能为了EXEC绕过模板匹配保护。
   新PC应canonical/至少2字节对齐并有U/X映射，新stack_top非零/canonical/16对齐，stack_top-1有U/R/W映射。
5. 构造局部`trap_frame fresh_frame{}`，只设置新sepc、初始用户sp、a0=user_argument、sstatus=SSTATUS_SPIE。
   其余用户寄存器为0，包括gp/tp/ra；不继承旧程序调用栈或寄存器。

EXEC替换旧用户现场，**不要求旧PC/SP像fork一样仍能作为后继现场继续使用**。
旧frame的地址与来源需要可信，新入口/栈则必须完整验证。
源镜像在永久内核内存中，准备期间不需要先激活candidate，也不需要复制用户路径或旧栈内容。

### 提交阶段：不能再返回旧程序

所有准备完成后按以下次序执行：

```text
激活永久g_kernel_root
→ 当前Process.release_resources()释放旧空间
→ 当前Process.adopt(candidate)安装新空间并清空candidate
→ *frame = fresh_frame，原位替换保存的用户现场
→ 激活当前Process.root()的新用户根
→ 内部返回0，由trap_restore恢复到新入口
```

- 首次激活内核根后进入提交路径，任何激活/旧空间释放异常都诊断停机。
  旧空间可能已部分释放，不能再返回-1并试图继续旧程序。
- 释放旧空间失败时，Process保留旧的部分描述，candidate仍持有完整新空间；停机不丢掉两者的归属。
- 释放成功后Process空间为空、exit_info仍为NONE；adopt满足原有空目标前提，没有新的可失败分配。
- 不调用task_destroy/finish_user_task：旧镜像结束不是任务退出，Task不能变DEAD或产生REAP结果。
- 不调用task_allocate/task_create_user；ID、槽位、高水位、下一ID、kind=USER、state=RUNNING保持。
- 不调用Process::reset或riscv_task_context_init，不复制/清零整个Task/context/内核栈。
  现有context_init实际只初始化frame等字段，但其契约用于未运行/安全回收槽，EXEC不通过它重建活动context。
- 保留当前frame指针和静态内核栈。当前C调用链使用共享映射中的内核栈，释放旧用户栈不会破坏内核调用链。
  只将局部fresh_frame的288字节写入当前frame；trap_restore按这个位置重建同一Task的sscratch。
- 切新根必须显式执行。EXEC不会通过调度器选择另一个任务，不能在kernel root下直接sret到用户入口。
- 提交中的RUNNING Process短暂root为空窗口在单hart/SIE0/不调度约束下不可观察，adopt后恢复完整关系。

## syscall与用户包装

syscall.h增加`SYSCALL_EXEC 10`，保持汇编可包含。
syscall.c当前入口已经统一将旧sepc前进4；EXEC分发先保存原a0参数，再调用服务：

```c
case SYSCALL_EXEC: {
    const uintptr_t user_argument = (uintptr_t)frame->a0;
    if (sched_exec_current(frame, user_argument) != 0) {
        frame->a0 = (uint64_t)(long)-1;
    }
    break;
}
```

**成功时不写frame->a0=result。** 服务已经放好了新入口参数3，若再写0，会让新程序走错分支。
成功时fresh sepc为精确ELF入口，不是entry+4；失败则保留原sepc+4和旧程序现场，仅返回值a0=-1。
handler最终继续返回当前frame，不调度，不返回新任务的frame。

用户API增加：

```c
long user_exec(uintptr_t user_argument);
```

它不能标记noreturn，因为普通失败会返回。汇编包装为a7=SYSCALL_EXEC/ecall/ret，
成功靠保存的PC替换跳到新入口，失败才执行原包装的ret；已有EXIT后的兜底循环保持。

## 正式用户演示与一页代码预算

当前B13正式RX0xf8a，只剩118字节。本批通过共用已有fresh-worker控制增长：

1. 将main最前面的tag2分支改为`user_tag == 2 || user_tag == 3`，仍置于拒绝tag>1之前。
2. 共用counter==7/data首字节d/完整BSS初始零检查与BSS填充，删除此前重复的第一段BSS检查。
3. 保留每个worker修改自己的数据及yield返回值检查。tag2输出原`[U2] fresh image OK\n`并返回23；
   tag3输出`[exec] fresh image OK\n`并返回37。可按tag选择两条const字符串及其长度，共用write检查。
4. fork子原来的snapshot/修改counter999、fork_local5678、buffer[0]=0xee/yield保持检查和日志都保留。
   将原return31改为以下控制流：

   ```c
   /* 成功不返回，任何返回都报告错误。 */
   user_exec(3);
   fail("exec returned\n");
   ```

   这里刻意将任何返回当作演示失败，不忽略失败后继续旧程序；系统API仍允许其他调用者正常处理-1。
5. 父仍使用实际forked_id调用reap_requested，预期退出值从31改37，父自身counter/局部栈/buffer/data保持检查不变。
   原fork isolation成功日志与U1 end/return0继续保留。U2仍先reap、检查数据并打印reaped，之后才fork。

成功EXEC期间ID5保持且没有新增用户Task，父按原ID收集的是替换后镜像最终的退出值。
不需要GETPID或新wire结构；内核退出日志及P0可以核对身份。

临时用户原型实际编译RX0xfc2（4034字节）≤4096、RW memory size仍0x1038。
这证明按上述范围可容纳，剩余62字节不能保证任意新增字符串仍能放下；保留一页链接断言，
超出时优先复用代码/诊断，不通过删断言、强行切全局O3或本批扩多页代码解决。
原型只在.cache，不是正式EXEC实现或运行证明。

## 建议实现顺序与命令

1. 先提取prepare_builtin_user_space，SPAWN改用它；保持原清理/最后发布语义。
2. 编写sched_exec_current的准备检查与局部fresh frame，再实现上述提交顺序；复用Process现有接口。
3. 接EXEC10和用户API/包装，成功分支保留新a0。
4. 共用tag2/tag3 fresh-worker，fork子调用EXEC，父收集37；完成后集中审查/验证。

```sh
make -j4
timeout 25s make qemu
```

timeout的124是外部观察结束，不要求S任务或QEMU自行退出。不需要make clean、全仓格式化或提交推送。

## 验收与分工

作者实现核心，协作者承担全部P0；P1/P2选做，不给作者追加框架或矩阵。

| 等级 | 场景与具体预期 |
| --- | --- |
| P0 | 完整构建无新增诊断/未定义，原82宿主保持；正式RX≤4096，原B12 SPAWN/U0/U1检查和B13 fork快照/隔离保持 |
| P0 | 正式一条fork→exec链：同slot2/ID5、count4/nextID6/RUNNING/Process对象/context.frame/内核栈保持；默认四槽全部占用时EXEC仍成功，证明不依赖空槽或分配ID |
| P0 | 新镜像实际从精确ELF入口/初始用户SP开始，a0=3/SPP0/SPIE1、其余用户GPR0；原ecall后代码与exec returned标记不执行；fresh counter7/BSS0/d，退出37，父按原ID5收集，后续S timer正常 |
| P0 | 准备阶段仍用旧用户根，新增7页与旧子/活父各7页逐一不同；旧程序完整内存/Process/现场与父/shared保持；旧7页只在内核根下释放且根最后，完成后显式切新根再返回用户态 |
| P0 | 一次普通PMM失败（例如准备第4次请求，root+2内容已取得）：候选3页完整归还，旧程序内容/Task/ID/current/satp和服务输入frame保持；失败syscall只更新原后继PC/a0=-1，旧程序可检查自身数据再重试成功 |
| P0 | 未配置共享准备助手零分配拒绝；镜像模板与scheduler模板匹配检查在提交前；普通失败不会先释放旧空间，不允许清理失败后普通返回 |
| P0 | 正常链4个用户任务、5份地址空间生命周期：U0、U2、fork旧镜像、exec新镜像、U1各7页，总35次合法free；运行期新增SPAWN/FORK/EXEC各7页共21，最终净恢复boot两用户14页，三次REAP零PMM，不把EXEC替换误算一次Task退出 |
| P0 | 真实EXEC调用链结合target.su与实际8KiB栈边界/水印检查余量；不拿旧SPAWN在另一调用链通过代替EXEC栈验证 |
| P1 | 更多PMM失败点、重复EXEC、更多入口参数/多镜像目录、提交阶段异常诊断探针：按兴趣选做，不阻塞本批 |
| P2 | 路径/argv/envp、VFS/execve、COW/共享用户页、多线程EXEC、动态内核栈、多hart：独立后续机制 |

正常统计允许不同生命周期复用PA；失败探针额外分配/释放另行计数，不混进正常35次统计。
本批不要求“槽满导致EXEC失败”，因为EXEC不创建Task；也不自动追加完整历史故障矩阵。
loader自身回滚再失败的历史限制仍需独立说明，不能宣称任何异常都能恢复原状态。

## 功能梳理与原三题

实际路径如下：

1. fork子ID5在旧程序调用user_exec(3)，包装将10放入a7后ecall。trap进入当前任务自己的内核栈；
   syscall先推进旧sepc，再将数值参数3交给sched_exec_current。
2. 当前仍用旧用户根，SIE关闭。共享prepare助手从永久内置ELF准备完整candidate，
   不修改当前Process；检查新入口/用户栈后，构造只含entry、SP、a0=3、SPIE的零初始化fresh frame。
3. 提交先切永久kernel root，再释放Process旧空间的7页，adopt新空间，原位写入fresh frame，最后显式切新root。
   Task槽、ID、RUNNING状态、Process对象、frame地址和内核栈保持；没有旧镜像的Task退出事件。
4. 服务内部返回0，dispatcher保留fresh a0=3和精确ELF入口。trap_restore从同一frame恢复到新用户入口，
   用户启动代码使用全新用户栈调用main(3)，不会回到旧user_exec后的代码。
5. 新镜像检查counter7、BSS0及data首字节d，输出fresh日志并最终返回37。
   这时才发生ID5的真实退出，释放新空间，父U1按原forked_id收集37。

准备阶段普通失败只清candidate，返回旧程序的ecall后继，a0=-1；旧内存和任务身份保持。
提交开始后旧空间可能已部分释放，错误必须诊断停机，不能再尝试普通返回旧程序。

下面保持本批最初三题；原答、集中审查和协作者修正版继续保存在本文件，不要求重复复答：

1. 为什么EXEC可以保留ID，却必须更换用户PC/SP/内存？它与FORK、唤醒各有什么区别？
2. 为什么必须先准备成功、再释放旧空间？提交开始后释放失败为何不能再普通返回-1？
3. EXEC内部返回0时，为什么syscall不能再写frame.a0=0？当前内核栈/frame指针为什么可以保留，而旧用户栈可以释放？

### 三题原答与集中审查（2026-11-09）

以下原答逐字保留；审查与修正版由协作者整理，不冒充作者再次作答，也不要求重答或追加测试。

**第一题原答：**

> fork得来的是父进程的 `pc`, `sp`, `memory`, 不能直接用, fork是创建空间，是进程创建准备阶段，exec是装填有效内容，让进程可以被启动，唤醒是调度器的事情

**审查：** 唤醒属于调度状态转换的判断正确；fork继承父运行现场与内存的方向也正确。
但fork完成后子进程已经拥有可运行的程序与调用栈，不需要再exec才能运行。
若“不能直接用”指不能直接共用父的物理页或trap_frame，在当前立即复制设计中成立；
若指复制得到的内容不能执行，则不成立：复制的正是父当时有效的运行快照。
子进程PC/SP数值可以与父相同，PC位于fork的ecall后继；同一栈VA在子页表下指向自己的PA，
配合复制的栈内容及寄存器就能延续原调用链。父a0获得新ID，子a0由内核显式设0。

**协作者修正版：** fork创建一个新身份，从父的运行快照继续；exec保留当前身份，
将用户内存、PC/SP与寄存器现场替换成新程序的初始状态；唤醒使满足条件的BLOCKED任务转为READY，
之后由调度器选择运行，保留原程序和现场。exec可以由已经运行的进程直接调用，不以fork为前提。

**第二题原答：**

> 先释放旧空间失败了救不回来，而且需要旧空间内想新空间转移内容，释放失败的话，直接返回-1会内存泄漏

**审查：** 已认识到旧空间释放后可能无法恢复，这一点正确。
但即使先释放完全成功，随后新镜像准备失败，也会失去能返回的旧程序；先准备完整candidate才可在普通失败时保留旧程序。
本批新程序从永久内置ELF装入，既不复制旧用户栈，也不把旧counter/BSS迁入新空间；数值参数已保存在内核中。
将不能返回的原因只归为内存泄漏不准确：旧release可能已解除部分映射或归还部分页面，
旧PC/SP仍在也无法保证代码、数据及栈完整，返回可能页错误或访问已经释放的页面。
当前释放失败时Process仍保留部分旧归属、candidate仍持有新空间；停机也不能称作正常完成清理。

**协作者修正版：** 准备阶段仅建立候选，失败清理候选后可返回-1；准备全部成功后才开始提交。
提交开始后错误不能再普通返回，因为旧执行环境可能已被不可逆地破坏；当前采用诊断停机。
候选清理自身失败另有归属与泄漏风险，同样不能当作普通准备失败忽略，但与旧程序不可恢复是不同问题。

**第三题原答：**

> 因为frame的a0接收到的不是0，旧用户栈和内核栈不在一个地方，而且U-mode无法操作S-mode的内核栈

**审查：** 入口参数需要保留、用户栈与内核栈分开、U-mode不能访问Supervisor栈映射的判断正确。
需补充：返回0是内核服务的成功状态，fresh frame.a0是新程序的入口参数3；两者含义不同。
不能仅靠“不是0”决定是否覆盖，因为接口也允许入口参数本来就是0。
权限隔离保护内核栈，却不能独自证明栈在释放后仍存活；关键是其归属和映射。
当前内核栈是Task context内的静态存储，经共享Supervisor内核映射在旧根/kernel根/新根下都可访问；
旧UserAddressSpace释放的页不包含这块存储，EXEC也不重置或移动当前context。
frame保存在该内核栈内，地址可保持，但其保存的用户寄存器内容必须原位换成fresh frame。

**协作者修正版：** 成功时dispatcher保留新frame.a0的入口参数，不能用服务返回0覆盖。
当前C调用链及frame仍位于存活且持续可访问的同一内核栈；旧用户栈属于被替换的用户空间，
内核不再使用它恢复旧调用链，因此可以释放，再用新用户SP恢复新入口。

功能、全部既定P0、执行路径与三题原答/集中审查已完成；修正版是协作者解释，不作为作者复答记录。

## 最终实现与P0验证（2026-11-09）

作者已修正父收集预期37并恢复U0/U1 start日志。协作者只将已补的candidate清理失败诊断
移入实际release失败分支，避免普通准备失败被误报；随后所有探针均在隔离源码副本运行。
正式源码/配置70项哈希从该局部修正后到验证结束保持一致。

证据入口：`.cache/batch14-final-2026-11-09/summary.json`。
正常构建、用户布局与25秒运行分别见build.log、user-layout.log、normal-result.json、normal-qemu.log；
宿主结果见host-tests.log；失败与运行时探针分别保存在failures/和runtime/，均保留脚本、副本及日志。

| 验证 | 实际结果 |
| --- | --- |
| 正式构建/宿主 | make -B -j4无诊断，nm -u为空；原82个具体宿主场景及汇总全部通过 |
| 用户ELF预算 | RX=0xf9a（3994字节）≤4096，RW memsz=0x1038；入口0x40000000 |
| 正式演示 | U0/U1 start/end各一次，counter各三次，Time/Data/Hello各两次；SPAWN、fork snapshot、EXEC fresh、父isolation各按预期出现 |
| 正常退出顺序 | ID2/4/5/3分别退出0/23/37/0；U2先退出再reap，随后fork→exec→ID5退出→父隔离检查；U1退出后仍有12次S timer，无失败/fault/halt |
| EXEC身份与现场 | 四槽均占用时同slot2/ID5、count4/nextID6/RUNNING、Process/context/frame指针/内核栈保持；精确新PC/SP/a0=3/SPIE，其余frame字为0 |
| 准备与切根 | 准备仍在旧根；新7页与活父及旧子各7页无重叠；新内容完整页字节/权限符合原始镜像，父/旧子内容及旧frame与共享映射保持；旧7页在kernel root下释放、root最后 |
| 新入口恢复 | dispatcher成功保留a0=3/精确entry，新根已激活；实际tag3的U-mode trap验证同内核栈/同ID5，旧调用点未继续 |
| 未配置拒绝 | 共享助手返回非法参数，零分配/释放；空输出、entry/stack哨兵与Task/Process/idle/shared/satp均保持 |
| 一次准备失败 | 真实ID5 EXEC第4次PMM请求注入NULL；已取得root+两个内容页，恰好3页唯一归还且root最后，未切根；旧服务输入frame/完整活页表及内容/Task/Process/ID/current/satp保持 |
| 真实返回与重试 | 旧用户收到-1，检查counter999、局部5678、buffer[0]=0xee、data首字节D仍在，再调用EXEC成功；同ID5的新程序退出37，父隔离/退出0正常 |
| 生命周期统计 | U0、U2、fork旧镜像、exec新镜像、U1各7页，共35次合法free；运行期新分配21页，最终净恢复boot两用户14页，三次REAP零PMM |

模板相等守卫及其位于提交前的顺序由静态审查确认；未另增运行时模板不匹配变体。
提交阶段三处错误均诊断停机，普通candidate清理失败同样不能返回旧程序；未声称注入过所有提交错误。

真实EXEC链在8KiB内核栈中测得底部3424字节未触及，最深采样SP距栈底3456字节。
target .su对应exec768、loader2224、prepare80、syscall128、trap64字节；每函数用量不能简单相加当所有分支的深度。
水印与采样包含探针/包装的观测开销，仅证明当前调用链和镜像布局，不保证任意未来扩展。

25秒结束由外部runner终止QEMU，不是客体自行关机；一次受控NULL不等于实际耗尽物理RAM。
loader自身回滚再次释放失败的历史归属限制仍在，本批不宣称任意异常都能恢复。
正式用户代码仍有一段重复BSS零检查，属非阻塞整理项；失败探针仅在隔离用户副本移除此段以容纳额外调用，
没有因此修改正式用户程序、扩展代码页或改变优化等级。

## 历史准备与审查记录（以下待修项已按上节解决）

2026-11-09集中复查：提交顺序已为内核根→释放旧Process空间→adopt→原位freshframe→新根，
三处提交错误调用exec_commit_fail停机；SYS_EXEC成功不覆盖a0，包装ret仅供错误路径，静态未发现核心阻塞。
正式make -j4无诊断/nm-u空，RX0xf12≤4096/RWmem0x1038。正式16秒QEMU实际fork子日志→
exec fresh日志→同ID5退出37；没有exec returned，父随后unexpected U0 exit result/status1。
错误原因是reap_requested(forked_id,31)未改37；U0只是通用助手的旧文案，不是U0本身失败。
共用fresh初始化对所有main入口有效，但重构误删U0/U1 start消息，应恢复到tag>1拒绝之后、counter循环前。
第三处是准备失败且candidate清理也失败时当前无诊断直接wfi，补日志后继续停机即可。
独立只读复查同样确认上述三处，没有追加正常路径问题或新验收矩阵。
证据`.cache/batch14-core-review-2026-11-09/{build.log,undefined.log,qemu.log,normal-result.json,user-layout.log}`
及70项正式源哈希前后保持。未代改产品、未执行全部B14 P0；待作者修用户预期/日志与异常诊断后复验。

2026-11-09助手草稿检查：成功末尾输出entry/stack_top正确，SPAWN已调用助手再创建Task。
需按原契约保留create/load的SV39错误码，普通失败不在助手内释放output，由SPAWN/EXEC调用者统一清理。
当前两层release通常因成功清空/idempotent而第二次无操作，不应误称当前必然double-free；
修改是为了保持明确的部分输出契约和单一清理责任。未配置用SV39_ERR_INVALID_ARGUMENT表达。
loader已有带memory clobber的fence.i，助手末尾重复无clobber的fence.i可删除。
EXEC服务尚未实现，未执行本批P0或代改产品。

后续EXEC草稿检查：current_user_task结果未判空；prepare失败和模板不匹配分支释放后未return，
会继续使用已清空candidate；结尾误调用task_create_user，把替换写成新建且最终无条件返回0。
需沿原任务补候选root/inner关系与entry/stack_top在新根的启动校验、局部零fresh_frame，
再按kernel根→Process.release_resources→adopt→原位frame→新根提交；提交异常诊断停机，
准备拒绝清理后-1，成功才内部0。共享准备助手现已保留create/load原错误且普通失败不内清理。
FORK/EXEC身份区分集中解释，未代改产品或运行半成品。

作者随后明确请求给候选校验加补丁。协作者仅在sched_exec_current的候选检查段补充：
根/模板/inner关系与非零有界内容计数、entry的canonical/非零/2对齐、stack_top的canonical/非零/16对齐，
并用candidate.root分别query新entry的U/X和新stack_top-1的U/R/W。
该段共用局部reject_candidate清理，释放失败打印并停机，成功清理返回-1。
检查对象是新镜像输出，不是旧frame或旧Process根。make kernel/task/sched.o实际通过、无诊断；
尚未实现frame后面的切根/释放旧空间/adopt/切新根提交，不运行EXEC或宣称P0完成。

两个内部协作者分别检查事务/模块接口与frame/栈/演示设计，确认现有Process release/adopt足够，
不必增加交换接口或复制整个Process。仅只读设计，没有实现正式EXEC。
临时新增接口按RV64 C11/C++20语法检查无诊断，临时用户原型链接通过RX0xfc2；
证据`.cache/batch14-preparation-2026-11-09/{preparation-results.json,user-prototype-build.log,user-prototype-layout.log}`。
70正式源配置哈希与B13验收后相同。本轮只创建任务文档/更新索引，不修改正式产品，不运行EXEC或宣称P0完成。
