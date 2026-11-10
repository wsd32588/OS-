# BATCH13：复制当前用户空间与执行现场的 FORK

状态：2026-11-08正式实现、演示、全部既定P0与三题原答/集中审查均归档，本批收尾。
最后一处U2收集调用顺序由协作者做单行位置修正，作者完成其余核心实现；修正说明不冒充作者复答。

导航：[批次索引](README.md) · [上一批](BATCH12_BUILTIN_SPAWN.md) · [下一批](BATCH14_BUILTIN_EXEC.md) · [总路线图](../TINYOS_EVOLUTION.md)

## 目标与可观察结果

B12能够从原始ELF启动新任务。本批让运行中的U1调用`user_fork()`，复制此刻的用户内存及保存的用户现场，
两个任务从同一调用点之后继续。父任务得到新公开ID，子任务得到0；失败时只有父任务返回-1。

使用立即复制：代码、用户栈、data/BSS每个内容页都新分配并整页复制；私有页表也独立。
保留借用内核模板槽0/2的方案、静态四槽与8KiB内核栈。共享的内核表不复制、不释放。
这是本项目的最小fork语义，不扩展父子权限、孤儿回收、阻塞wait、exec、VFS、COW、引用计数、堆或多hart。

原B12演示完整保留。U1收集原U2（公开ID4）后才fork，因此正常运行的新任务是：

| 内容 | 预期 |
| --- | --- |
| 复用槽 | slot2 |
| 新公开ID | 5；代码必须使用实际返回值，不硬编码5 |
| 父任务 | 仍是U1，公开ID3、slot3 |
| 父返回值 | `user_fork()`返回新ID5 |
| 子返回值 | 同一调用点的`user_fork()`返回0 |
| 子的user_tag | 仍继承U1的1，不新增tag分支，也不重新进入main入口 |
| 子退出值 | 31，父使用已有REAP收集并核对 |

默认增加两条日志：

```text
[fork child] snapshot OK
[task] user exit id=0x0000000000000005 status=0x000000000000001f
[U1] fork isolation OK
[U1] end
```

子日志在子退出前，父隔离日志在成功收集子之后。父子执行的其他先后顺序由调度器决定。

## 文件与模块边界

不新增第二套任务表、公开ID分配器或Process可变getter，不复制整个Task/Process。
本批复用已有编译单元，无须另建fork.cpp或增加Make对象。

| 文件 | 作者实现内容 |
| --- | --- |
| `include/mm/user_address_space.h` | 增加C ABI clone声明，说明部分输出的归属契约 |
| `kernel/mm/user_address_space.cpp` | 源校验、独立根/内容页复制与建表；保持可在宿主编译 |
| `include/process/process.hpp`、`kernel/process/process.cpp` | 增加只读克隆操作，内部访问自己的私有user_space_ |
| `include/task/sched.h`、`kernel/task/sched.cpp` | 当前用户fork编排、私有现场发布助手 |
| `include/task/syscall.h`、`kernel/task/syscall.c` | FORK9和薄分发 |
| `user/user_api.h`、`user/syscall.S` | 无参数user_fork包装 |
| `user/main.c` | 共用REAP助手和fork快照/隔离演示 |

MM负责页与映射，Process负责访问自身拥有的空间，scheduler负责当前任务与新现场的发布。
`sched_fork_current`是编排入口，不在其中实现页表遍历或整页复制算法。
boot和B12永久ELF配置保持现有路径，fork不读取该配置或重新装载ELF。

## 接口与返回契约

### 地址空间复制

在已有C头的extern C块中增加：

```c
int user_address_space_clone(
    const UserAddressSpace* source,
    UserAddressSpace* output
);
```

- source是完整的活用户空间，允许它正是当前活动根；不能套用release对活动根的拒绝规则。
- output初始必须完整为空，且不能与source为同一对象。空指针、非空目标或无效源在分配前拒绝。
- 成功返回SV39_OK；output拥有全新的私有根、下级表和内容页，只借用相同kernel_root。
- source描述、私有页表及内容、共享内核映射和当前satp均保持。
- **初始为空的output在分配后失败时允许保留已经取得的部分资源，调用者必须调用既有release。** clone不做第二层隐藏回滚；
  每个已取得的物理页必须已在output描述或其页表中可追踪，不能用返回错误丢掉局部裸页。
- 非空目标属于非法参数，拒绝时保持原内容，不覆盖或接管它原来拥有的资源；实际fork调用方始终使用局部空candidate。
- 返回原SV39错误码。发布服务将普通失败转换为-1；外层清理自身失败则诊断停机。
- 只复制当前source自己的有界页记录，不扫描所有Process或全部物理内存。

### Process与调度入口

```cpp
// Process公开成员，保持user_space_私有；只转交给MM。
int clone_address_space(UserAddressSpace& output) const;
```

该函数内部将`&user_space_`和`&output`交给clone，不处理frame、分配ID或选择下一个任务。

```c
/* sched.h：由真实用户syscall处理器调用，sepc已经前进4。 */
int sched_fork_current(struct trap_frame* frame);
```

返回新公开ID或-1。调用时为单hart、SIE关闭、尚未重调度的真实用户trap；
使用既有current_user_task先验证当前槽、USER/Process/RUNNING/SPP0，失败不分配或修改任务。
不要返回Process/Task可变指针给其他模块。

发布助手留在sched.cpp内部：

```cpp
static int task_create_user_from_frame(
    const trap_frame& snapshot,
    UserAddressSpace& candidate
);
```

它接收已经验证的用户现场和完成的候选空间。唯一可能失败的提交准备是既有task_allocate；
失败时不改candidate。成功后只复制现场、设置子a0、关联同槽Process并adopt，无额外可失败操作。
不复用fresh创建接口的stack_top含义来传递当前SP，也不在提交后再按ID查找Task以补现场。

## MM复制：完整源校验与逐页记录

先完成只读源校验，再创建输出根：

1. 校验source根与kernel_root的非空/物理编码/页对齐，之后才解引用；复用同文件已有根/模板/共享槽检查。
2. inner root必须等于source.root，page_count在1..USER_MEMORY_MAX_PAGE，页VA都属于私有用户槽1。
3. 复用validate_pages核对地址、VA与PA重复归属；额外要求所有活动记录PA非空。
4. 对每个活动VA调用query，必须SV39_OK、实际PA等于记录PA并有U，权限合法；保存要复制的flags。
   **validate_mappings允许NOT_MAPPED以支持释放重试，不能单独拿它证明源完整可复制。**
5. 源全部校验通过后调用user_address_space_create(source.kernel_root, output)。

源来自内核维护的完整页归属记录，不支持本批之外的隐藏用户映射、共享用户页或COW记录。
权限检查属于复制边界，不能把RX代码页映射成可写，也不能丢掉用户U位。

逐条记录处理：

1. pmm_alloc_page取得一个新内容页；失败立即返回错误，output保留已有部分。
2. **在任何map操作前先将新页记入output。** 第一次取得内容页时才同时设置inner root与非零page_count。
3. 通过内核恒等映射，将源物理页的4096字节复制到新物理页；包括栈、当前data和已经改写的BSS。
4. 在新根中映射同一VA，权限来自源查询。

归属记录的关键顺序：

```cpp
// new_page已经分配成功；以下赋值之间没有可失败操作。
UserMemory& memory = output->user_memory;
memory.root = output->root;
memory.pages[memory.page_count] = UserMemoryPage{
    source_page.virtual_address,
    new_page
};
++memory.page_count;
// 然后才复制内容、尝试建立映射。
```

尚无内容页时保持inner root为空/count0；不要先造出inner root非空/count0后在分配失败时交给release。
count最多是16条内容页，不把根或下级表塞进这个数组。

query返回flags含V，而map不接受调用者提供V。传给map时使用：

```cpp
const uint64_t map_flags = source_flags & SV39_PTE_LEAF_FLAG_MASK;
```

复制整页可用简单的unsigned char循环；无需4096字节栈buffer，也无需user_copy_from/to。
这里的输入/输出是已经验证、由内核访问的物理页别名，不把子用户VA当作父根下的目标指针。

map失败可能已留下空中间表。因为尝试映射的VA及新页已提前记录，现有UserMemory::release可接受该页未映射，
并通过该VA回收空L0/L1；外层release最后归还根。不要在clone里另写一套页表释放器。
输出根始终不激活。成功clone也不要求逐页刷新未活动的子根，首次调度的sv39_activate已有完整本地刷新。

**user_address_space.cpp属于宿主测试源，不能在其中直接加入RISC-V fence.i。**
复制后的指令同步放在target-only的sched_fork_current，所有复制成功、发布前执行`asm volatile("fence.i" ::: "memory")`。

## 当前现场与最后发布

在sched_fork_current内依次完成：

1. 取得已验证的当前用户Task；核对待恢复sepc canonical/至少2字节对齐且指向U/X映射，
   用户SP非零、canonical、16字节对齐，SP-1指向U/R/W映射。SP是此刻的用户SP，不是启动时的栈顶。
   全部检查在分配/提交前完成，不额外要求PC页对齐。
2. 以局部`trap_frame snapshot = *frame`保存288字节的用户现场，局部`UserAddressSpace candidate{}`接收克隆。
3. `parent->proc->clone_address_space(candidate)`；失败统一释放candidate，成功清理才返回-1。
4. 指令同步，然后调用私有发布助手。
5. 发布失败（例如槽满）仍由外层释放candidate；成功时candidate已转交并清空，返回新ID。

clone失败前可能已有根或内容页，因此不能直接return-1。任何release返回非SV39_OK都必须诊断停机，
保留剩余拥有关系；普通失败不消耗ID，不增加高水位，不改父frame/satp/current/空间。

发布助手先task_allocate(&child)，随后：

```cpp
*child->context.frame = snapshot;
child->context.frame->a0 = 0;
// 按现有用户创建末尾逻辑：同槽Process绑定、adopt(candidate)、kind=USER。
```

必须保留child->context.frame指针指向它自己的静态内核栈，只复制288字节内容。
不能复制父Task、Process、RiscvTaskContext、frame指针或整个内核栈。
trap_restore会依据子frame位置重建sscratch，再恢复继承的用户SP。

保留snapshot的用户ra/sp/gp/tp及其他通用寄存器、合法用户返回sstatus（SPP必须0），只将子a0改0。
不要像SPAWN那样清gp、设置ELF入口或重置用户栈顶；父当前的用户调用栈和寄存器决定子从哪里继续。
scause/stval/reserved是保存的诊断字段，不是新的退出原因，也不影响本批寄存器恢复。

syscall_handle入口已经给父frame.sepc加4，snapshot继承的是ecall后地址；**子不能再次+4，也不能退回ecall**。
服务自身不修改父frame。最后由FORK case把返回ID写到父frame.a0，原caller frame仍是handler返回的frame。
发布过程中不yield/sleep/打开中断/切candidate根/reschedule。
现有allocator先设置READY在此单hart/SIE0窗口内不会被观察为半初始化的可运行子任务。

## syscall和用户API

`include/task/syscall.h`增加`SYSCALL_FORK 9`，继续保持汇编可包含。
`sched.h`增加C入口声明，`syscall.c`仅薄分发：

```c
case SYSCALL_FORK: {
    const int new_id = sched_fork_current(frame);
    frame->a0 = (uint64_t)(long)new_id;
    break;
}
```

不再推进PC，不直接调度，也不创建额外用户输入/输出buffer。

`user_api.h`增加`long user_fork(void);`。`user/syscall.S`增加a7=SYSCALL_FORK/ecall/ret包装，
不要移动已有EXIT ecall紧后的兜底循环。它与SPAWN相比不接收user_tag参数。

## 用户演示与一页代码预算

当前正式RX为0xd98，只剩616字节。在main前增加一个用户私有小助手：

```c
static void reap_requested(int task_id, long expected_status);
```

将原U0、U2以及本批fork三处相同的轮询集中到这里：局部TaskExitResult对象，
reap返回0则检查yield并重试；返回1时核对REQUESTED/expected_status/cause0，其余fail。
调用分别为`reap_requested(2, 0)`、`reap_requested((int)spawned_id, 23)`、fork返回ID与31。
保留原两条U0/U2成功收集日志和所有既有数据检查；删除U2此前多余的第一段BSS全零检查，保留sizeof(buffer)的检查/填充循环。
这只整理用户演示中的重复代码，不推广成通用等待库或修改内核REAP协议。

U1在输出`[U1] reaped U2`后、原end之前：

1. 定义`volatile unsigned long fork_local = 1234UL;`，确保用户栈内有可观察的局部状态。
   此时counter为expected_counter（正常203）、buffer[0]为1、data_message[0]为大写D。
2. `const long forked_id = user_fork();`，负值fail。
3. 子分支forked_id==0：检查counter==expected_counter/fork_local1234/buffer[0]==1/data首字节D，证明继承运行快照；
   改counter为999、fork_local为5678、buffer[0]为0xee，yield并检查返回0及自己的修改保持；
   输出`[fork child] snapshot OK\n`，立即return31，不能继续走父收集分支或再次创建。
4. 父分支按forked_id收集31，检查counter仍expected_counter、fork_local仍1234、buffer[0]仍1、data仍D；
   输出`[U1] fork isolation OK\n`，继续原end/return0。

user_tag是继承的用户程序变量，不为fork分配tag3，也不重新进main(新tag)。
forked_id变量会在父/子分别接收5与0；用户栈的复制加上两个frame的a0差异让同一段代码分支。

临时用户原型已经按上述范围抽助手并编译，RX0xf38（3896字节）≤4096，RW memory size仍0x1038。
这证明存在可容纳的演示写法，不保证任意增加字符串仍能放下；保留链接断言，超限时减少重复代码/重复诊断，不能删除断言。
原型仅在.cache中，不是已实现的正式fork，也未运行新系统调用。

## 建议实现顺序与验证入口

1. 先声明/实现MM clone，重点是完整源预检、每次分配立即记录、失败留下可释放output。
2. 加Process转交方法，保持私有user_space_，不暴露可变getter。
3. 写scheduler私有现场发布助手和当前用户fork编排，所有准备在task_allocate之前。
4. 接FORK9和用户包装。
5. 抽用户REAP助手，再补fork演示，完成后集中审查/复验。

```sh
make -j4
timeout 25s make qemu
```

timeout的124表示外部观察结束。内核仍可能运行S任务，不要求整个QEMU自行退出。
新声明/源文件加入后如编辑器缺编译参数，使用现有make compdb；不通过安装宿主C++头修复目标环境。
本批不需要make clean、whole-repo格式化、提交或推送。

## 验收与分工

作者实现核心；P0由协作者承担，不给作者追加测试框架或穷举矩阵。以下范围现在一次给齐：

| 等级 | 场景、具体预期与学习点 |
| --- | --- |
| P0 | 完整构建无未定义/新增警告，原82宿主保持；最小clone宿主检查覆盖独立页/同VA同权限同内容、无效活源在分配前拒绝；目标RX≤4096 |
| P0 | 保留B12完整链；真实fork父返回5、子0，子从同一后继PC与用户SP继续、继承局部栈/203与已写BSS/D，子修改后父保持；子退出31、父收集一次后退出0，后续S tick继续 |
| P0 | 实际创建期间SIE0/父根/当前槽与父空间、shared保持；子frame在自己静态栈，sepc只前进一次，除a0外用户现场保真，先READY再真实U运行 |
| P0 | 默认克隆7页全部与父私有页不同，VA/权限/初始完整页字节相同，内核槽0/2共享保持；slot2/ID5/highwater4，旧ID2/4拒绝 |
| P0 | 真实trap槽满失败：候选7页完整归还、Task/ID/高水位无提交，父现场/内存/satp保持；释放槽后重试仍取得原定下一ID |
| P0 | 一次受控PMM失败选择首次map的L0分配：root(1)、首内容页(2)、L1(3)已取得，第4次请求返回NULL；partial output保留首VA/PA/count，清理归还3页含空L1、根最后，重试仍ID5。这里不声称实际耗尽RAM |
| P0 | 正常fork新增7页、子退出归还7、reap零PMM；保留B12后四个已发布用户生命周期累计28次合法free，SPAWN+FORK新增14页，最终净归还boot两用户14页；允许生命周期间复用PA |
| P0 | 新trap调用链结合目标.su与实际栈边界/水印确认8KiB余量，不把B12旧链通过直接当作本批证明 |
| P1 | 更多PMM失败点、重复fork/reap多轮、更多寄存器压力/稀疏VA、释放失败后的描述保留：额外临时探针，按学习兴趣选做 |
| P2 | COW、共享用户页、父子权限/孤儿回收、阻塞wait、exec、VFS、动态内核栈、多hart：新机制，留后续独立批次 |

P0一次分配失败/槽满回滚是本批新增核心保证，协作者验证；P1/P2不实现不算本批未完成。
任意回滚自身再次失败仍按显式错误/停机处理，本批不引入通用故障恢复框架。

## 功能梳理与理解收尾

完成后梳理“父ecall→保存已前进PC的用户现场→逐页复制运行快照→最后发布→同一调用点两种返回值→子退出→父收集”。
原三题在本文件保存原答与集中纠正，不追加重复复答门槛：

1. 为什么fork子任务必须继承父当前用户SP和ecall后的PC？为什么SPAWN从ELF入口和初始栈开始？
2. 为什么可以复制trap_frame内容，却不能复制Task/context/frame指针？两个a0和下一次sscratch分别来自哪里？
3. 首次map分配L0失败时，根、首内容页与空L1分别由谁记录/回收？为什么要先记录PA再map，不能等全成功才写output？

## 理解题原答与审查（2026-11-08，已归档）

第一题原答（逐字保留）：

> fork的时候我们的程序不能停，当前可能已经处于一个多层函数调用深处位置，不从父进程继承会很难对齐调用链条，触发异常时的 `sepc`指向的是 `ecall`指令本身。如果不推进到 `ecall` 后的下一条指令（`sepc + 4`），子进程 `sret` 回用户态后会再次执行 `ecall`，陷入无限递归 `fork`

审查：对继承调用链、后继PC与重复执行ecall的判断正确。需要准确区分：
父用户代码在fork trap期间实际暂停，当前单hart/SIE0环境使复制期间源不会继续变化；
“不能停”应理解为不能丢失原执行进度或从入口重启。用户栈内容、当前SP、ra和其余寄存器共同维持调用链，
相同用户VA的新物理页使栈内局部变量/保存地址可继续按原值使用。
syscall_handle已经统一sepc+=4，子snapshot直接继承，不能在fork里再加一次。
若错误返回原ecall，表现为反复trap/fork/sret，并非C调用栈的递归；本项目四槽限制下可能先创建到槽满，
随后持续失败仍执行同一ecall，不能笼统理解为无限成功创建子进程。
SPAWN则装载初始镜像、构建新入口现场与初始用户栈，从_start/main开始，符合启动新程序的目标。

以上为协作者补充，不冒充作者复答；不要求重新回答第一题。

第二题原答（逐字保留）：

> 复制内容到时候还能用，但是复制指针万一指针悬空了呢？而且同时使用一个 `trap_frame` 很容易造成两个任务在同一个地方踩踏，父进程a0来源一传入的参数也就是 `id` ,子进程的是 `fork` 默认的0，下一次 `sscratch` 来自各个任务自己的栈顶

审查：两个任务共用现场会相互覆盖、子a0=0与各自内核栈顶重建sscratch的方向正确。
悬空不是唯一原因：父frame即使一直有效，两个任务共用它也会在trap保存时互相覆盖。
复制Task/context还会复制身份、调度状态、资源关联或指向原栈的frame指针；新任务必须有独立槽/ID/栈与资源。
父a0应准确说为内核分配的新子任务公开ID，由FORK case写入父frame；user_fork(void)无参数，
不存在由用户传入的id参数。子a0=0是发布助手显式赋值的返回约定，不是寄存器自动默认值。
trap_restore按实际被恢复的子/父frame地址计算frame+TRAP_FRAME_SIZE再写sscratch，随后恢复用户SP。

协作者整理的修正版：新任务使用自己槽位的frame指针与内核栈，复制父现场的值，子a0显式置0，
父a0由syscall写入task_allocate分配的新子ID。父子共享的是用户地址数值/调用点，实际私有页和内核现场独立；
sscratch每次恢复用户态时按各自frame位置重建。

第三题原答（逐字保留）：

> 根页表交给 `pmm_free_page` 回收，L1内容在 `user_memory_release` 的时候清除，首页被 `pmm_free_page` 清理回收，先记录PA是为了防止后续清理错误重新清理的的时候找不到对象，**单一所有权移交（Single Ownership）**：一旦分配出内容页 PA，立刻将其登记进 `memory.pages[]` 账本（此时即使页表还没挂上，`unmap` 阶段允许容忍未映射状态并继续执行 `free`），就能把所有已分配物理页和半途生成的中间级页表（L1/L0）全部纳入 `user_memory_release` 的统一清理管线中

审查：立即记录新内容PA、容忍未映射并统一清理的核心正确。精确归属分三处：
首内容页由memory.pages[]记录，UserMemory::release调用pmm_free_page；
空L1由root的PTE链接保持可达，release末尾按记录VA调用reclaim_empty_tables，内部解除上级PTE并归还表页；
根由output.root持有，外层user_address_space_release最后调用pmm_free_page。
在本题第4次分配失败的现场，L0尚未分配成功，因此实际只归还根/首内容页/空L1三页。
先登记是保证首次失败就有完整可达的拥有关系；free成功后立即清PA指针则另外保证后续重试不会重复free。
“清除”不仅是把L1字节清零，必须真正解除引用并归还PMM。内容页账本与根中页表链接共同支持清理，
不将中间表页也误记进memory.pages[]内容页数组。

协作者整理的修正版：每次取得内容页即登记VA/新PA，map失败时该VA仍可指导解除/刷新及空表回收；
统一从candidate外层release进入，先内容与下级表，最后根。clone错误保留部分output，
成功清理才返回-1，清理自身失败保留剩余描述并诊断停机。

本批功能、全部既定P0、路径关系与三题原答/审查/修正版归档完成，不要求重复作答或追加测试。
协作者整理不表示作者已再次独立复述。下一批尚未启动。

## 内核主体检查与临时运行（2026-11-08）

作者保存的MM clone已完成新根、每页先记录归属/复制整页/保留权限map，NO_MEMORY码正确；
Process转交、用户现场/SIE/PC/SP核对、值快照、两条失败统一清理、fence.i与最后发布都已接入。
FORK9/case/user API/汇编包装存在，EXIT兜底保持。独立只读审查未发现当前核心阻塞。

正式make -j4编译链接成功、kernel.elf未定义符号为空；原82宿主场景及汇总通过。
正式user/main.c仍没有user_fork调用，不能通过旧演示推断FORK已执行。
协作者在独立源码副本保留作者当前内核，仅替换user/main为任务交付时的用户原型，完整构建无诊断，
实际25秒QEMU通过原全部B12标记及fork child snapshot/parent isolation标记各一次；
U0(id2)/U2(id4)/fork子(id5)/U1(id3)分别退出0/23/31/0，U1收集子任务后正常结束，
最后退出后13次S tick，未见failure/fault/panic。runner限时停止，不代表客体关机。

证据`.cache/batch13-core-review-2026-11-08/`含正式build/host日志、probe源码副本与构建/QEMU/布局日志、
`probe-result.json`和前后正式源哈希；70项正式源配置哈希保持，未代改产品。
这是当前核心的一次真实正常路径验证，尚未完成本批全部既定P0，尤其不冒充正式作者用户演示已保存。
下一步作者按既定用户演示小节抽共用REAP助手、在U1收集U2后加fork分支，之后集中复验剩余原定P0。

## 正式用户演示集中检查（2026-11-08）

作者加入fork分支与共用REAP助手后请求检查。内核主体保持此前正常临时探针版本，
子快照/修改/yield/返回31与父隔离检查本身正确。集中发现两项功能阻塞与一项构建警告：

1. reap_requested将result.status预置expected_status，却成功后仍比较!=0。应result={0}、
   比较result.status!=expected_status；内核会写回实际结果，预置字段不能替代核对。
2. fork块位于spawn之后、U2收集之前，此时slot2仍被U2占用，即使U2已DEAD也不会自动腾槽。
   把整个fork_local起到父隔离成功日志结束的块移到U2收集/完整数据核对及reaped U2输出之后。
3. U1分支遗留TaskExitResult result={0}未使用，应删除；所需对象已由助手局部管理。

本轮make -j4成功但报告一项unused-variable；用户RX0xfa0≤4096，未超预算。
正式12秒QEMU实际U1打印fork failed并以1退出，U2随后fresh/23退出，未执行fork子分支，
符合槽满原因。因此未开始完整B13 P0，不把构建成功当演示通过。
证据`.cache/batch13-user-review-2026-11-08/{build.log,build-result.json,qemu.log,normal-result.json,user-layout.log,author-main.c}`。
原U2重复BSS检查是已有非阻塞整理项，不新增验收门槛。未代改产品，待作者集中修正上述三处。

后续复查：助手已改result={0}/比较expected_status，未使用对象已删；作者把reaped U2日志移到fork前，
但真正的reap_requested((int)spawned_id,23)仍留在fork后（本轮行283），因此槽未腾出。
应把这一条调用移到spawn成功日志后、U2数据检查与reaped日志之前，删除原位置调用避免二次收集。
仅日志位置改变不会消费任务。此同一已知顺序阻塞未修，未重复构建/QEMU；
MM文件较上次核心验证只移除了不用的user_access.h include，无其他内核功能变化。

## 正式实现与全部P0验证（2026-11-08）

作者修正助手、删除未使用对象并将实际U2收集移到fork前后，协作者将这一条既有调用再前移到
U2数据检查与reaped日志前，补齐此前确认的顺序；未修改内核实现。随后正式源/配置70项哈希在验证期间保持。
此前两处用户阻塞均已关闭，不将上节历史失败当作当前状态。

| 验证 | 实际结果 |
| --- | --- |
| 正式构建与宿主基线 | make -B -j4无诊断，nm -u kernel.elf为空；原82宿主场景及汇总通过 |
| 用户镜像 | RX0xf8a（3978字节）≤4096，RW memory size0x1038仍两页，链接断言保持 |
| 正式25秒QEMU | 原B12全部标记保留，start/end各一次、counter各三次、Time/Data/Hello各两次；id2/4/5/3分别退出0/23/31/0；U2退出后才打印reaped，fork子快照/父隔离各一次；U1退出后13次S tick，无失败/fault/panic |
| clone宿主新增检查 | 独立测试副本新增两场景，原82+新2=84个场景及汇总通过，无诊断。RX/RW两内容页样本：同VA/完整4096字节/所有flags一致，新root/L1/L0/内容与源不同；模拟活动源仍可clone，source/shared/satp保持，子修改不影响父；各归还5页且唯一/rootlast。缺失源映射时零分配拒绝、output空/源和计数保持 |
| 真实fork与现场 | SIE0/父root/父内核栈/current保持；发布slot2/id5/READY/highwater4，旧2/4查不到。子frame在自己栈，a0=0，其他现场字含PC/SP/gp/tp/status与已前进PC的父快照一致；父a0=5/sepc仅+4/其他寄存器保持，之后子实际U-mode/自己root与trap栈运行 |
| 完整页与隔离 | 真实clone返回、发布之前逐字节比较四个内容页和全部VA/查询flags一致；新私有7页与父私有7页逐一不同，父Task/Process/完整内存、shared/satp/current保持 |
| 真实槽满失败 | 在U0尚未reap时发起一次FORK（此时nextID4），候选7页唯一归还/rootlast，返回-1；Task/Process/ID/count/current、父现场、全部活用户表与内容/shared/satp保持 |
| 首次map的L0分配失败 | 后续U2收集后在FORK第4次PMM请求受控注入NULL，观察到output新根/首VA和PA/count1/链接的空L1，首内容已复制、无L0映射；实际release唯一归还3页含空L1且rootlast，无ID提交或泄漏；立即重试仍id5/slot2/count4，子31/父0 |
| 资源生命周期 | U0/U2/fork子/U1各归还自己的7页，每次先激活内核root、root最后，幸存父对象与完整空间保持；三次reap均零PMM。四生命周期累计28次合法free，SPAWN+FORK新alloc14，最终比boot两用户占用基线恢复14页并跨两次timer保持 |
| 实际8KiB栈 | 真实FORK watermark底部4760字节未触及，最深采样SP距底5472字节，含观测包装开销；target.su fork736、clone320、validate_pages784、私有发布80、syscall112、trap64、Process转交32，另有288字节trap frame |

证据根`.cache/batch13-final-2026-11-08/`：正式`build.log/host-tests.log/normal-qemu.log/user-layout.log`、
`normal-result.json`、验证前后源哈希与总`summary.json`；独立`host/`、`failures/`、`runtime/`
各有`results.json`、可复现脚本/源副本/完整日志，runtime另有`stack-usage.json`。
宿主新增两场景在隔离副本中验证，未加入正式test目录，不把正式make test的场景数误写成84。

全部原定P0通过。限制：PMM失败为一次受控NULL，不声称实际耗尽RAM；宿主活动root是CSR模拟，
真实trap/root切换由独立QEMU探针覆盖。栈观察只覆盖当前单hart/四内容页调用链，不能证明任意未来链，
也不声称此前4KiB栈实际溢出。各QEMU由runner在观察结束发送SIGTERM，不代表客体自行关机。
原loader二次回滚历史限制仍在，本批新clone保留部分输出并显式清理，清理自身异常诊断停机。
未提交/推送/clean。P1/P2仍选做，本批进入原三题理解收尾，不追加测试门槛。

## 准备证据与限制（交付时与实现过程记录）

2026-11-08入口草稿检查：非空目标的拒绝条件应逐项OR，不能等所有字段均非空才拒绝；
source/output空指针和同对象先拒绝，再检查目标三个root、count及整个pages数组为空。
validate_pages/validate_mappings是0成功的错误码接口，!result会在成功时为true；
它们验证描述/映射可接受性，不证明目标完整为空，因此不用于替代此处的空目标判断。
作者草稿尚未实现复制主体，不编译/运行未完成函数，也不标记clone通过；未代改产品。

同日源检查草稿：root非空不足以证明编码/对齐合法，查询前先复用valid_root及源布局/归属助手，
inner root必须等于outer root；先validate_pages再严格逐页query，不能只依赖允许缺映射的validate_mappings。
直接output.root=source.root会浅拷贝拥有指针，父子共用根；应在全部源预检完成后调用
user_address_space_create(source.kernel_root,output)取得新根，只借用相同kernel_root。
只有所有内容页复制映射完成后才能返回SV39_OK，草稿提前返回不代表复制已经成立。

同日调度入口提问：进入S-mode trap时硬件令SPIE接收旧SIE并将当前SIE清0，当前入口到syscall没有重新开启。
任务身份复用current_user_task(frame)核对RUNNING/USER/Process/root及来自U的SPP0，CPU当前在S-mode并不会改变Task.kind。
若显式守卫当前SIE，读取实时sstatus的bit1，不能仅用保存的frame.sstatus推断之后的实时状态；
无需通过额外关中断来掩盖错误调用上下文。实际草稿scher_fork_current拼写需与头中sched_fork_current统一。
参考：[RISC-V Supervisor ISA](https://docs.riscv.org/reference/isa/priv/supervisor.html)。

同日fork编排草稿检查：内联汇编占位符误写csrr &0，应为%0；clone与发布失败均直接return-1，
会丢失candidate部分拥有关系，须共用显式release/失败停机清理路径。snapshot当前为父frame引用，
按本批设计改为clone前的const trap_frame值副本（引用本身不构成独立快照，并非当前必然损坏现场）。
发布前缺fence.i、分配前缺原定PC/SP canonical/对齐/映射权限检查。
实际MM clone已完成源预检/新根/逐页申请并记录，但尚无整页复制和map；申请失败错误码应NO_MEMORY。
未运行或标记该半成品通过，未代改产品。

同日作者修正fork编排后进入syscall接线：实际源已改%0、实时SIE/PC/SP与权限检查、值snapshot、
共用candidate清理和fence.i，私有发布助手/Process转交也存在；尚未运行FORK。
FORK9/case/user API/汇编包装仍待接；MM clone实际仍缺字节复制和map且NULL错误码未改，
不能因分发接通就认为可运行。继续按既定任务完成后集中验证，未追加验收项。

独立只读设计检查确认现有release支持未映射内容页及失败map留下的空L1回收；
两个内部协作者分别核对事务/模块边界和用户现场/栈规则，没有代写正式功能。
临时接口RV64 C11/C++20语法检查通过，用户演示原型链接通过且RX0xf38；
证据`.cache/batch13-preparation-2026-11-08/{preparation-results.json,user-prototype-build.log,user-prototype-layout.log}`。
70项正式源码/配置哈希与B12验收后保持相同；本轮只交付任务及文档。
没有实现MM clone/内核FORK，没有运行FORK，不把上述准备编译当作本批P0完成。
