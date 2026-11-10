# BATCH12：用户请求创建内置镜像任务

状态：作者实现、全部既定P0、执行路径梳理与原三题原答/集中审查于2026-11-08归档，本批收尾。
修正版为协作者整理，不冒充作者复答或要求重复作答。任务交付日期：2026-11-07。

导航：[批次索引](README.md) · [上一批](BATCH11_REAP_AND_REUSE.md) · [下一批](BATCH13_EAGER_FORK.md) · [总路线图](../TINYOS_EVOLUTION.md)

## 目标与可观察结果

B11已经有“退出释放空间→保存结果→复制后消费→同槽新ID”。本批增加缺失的入口：
正在运行的U1通过syscall请求内核，用内置ELF创建一个全新的用户任务。

保留原两S、U0/U1及其完整检查。U1收集U0之后调用`user_spawn(2)`，保存返回的新公开ID，
再通过已有`user_reap`收集该任务，确认REQUESTED/status23/cause0。默认应观察到：

```text
[U1] reaped U0
[U1] spawned U2
[U2] fresh image OK
[task] user exit id=0x0000000000000004 status=0x0000000000000017
[U1] reaped U2
[U1] end
```

`spawned U2`与U2自己的首条日志可能因抢占交换先后，不要求固定顺序；U2退出必须早于成功收集消息。
原U0/U1的start/end、各三轮counter及原Time/Data/Hello检查保留。U1最后仍return0。

任务使用同一份ELF的tag2分支，重新初始化代码/data/BSS/用户栈；不复制U1的运行时内存。
当前固定槽容量仍为4，新任务实际复用已收集的slot2，取得新公开ID4；程序应保存spawn的返回ID再收集，
不能把slot2或tag2当作返回ID。

作者实现核心，协作者集中审查并承担P0。无父子权限、fork/exec、路径/VFS、用户传入ELF、
动态内核栈、引用计数或第二套PID。这里的“创建”是在运行期登记新任务，存储仍来自静态槽池。

## 先沿执行路径区分两类任务

| 项目 | 内核任务 | 用户任务 |
| --- | --- | --- |
| 调度记录/context/内核栈 | 都有 | 都有 |
| 初次返回的SPP | 1，回S-mode | 0，回U-mode |
| 初始sp | 内核栈顶 | 用户栈顶；另保留内核trap栈 |
| Process关联 | nullptr | 关联同槽Process |
| 私有用户空间 | 无 | Process拥有 |
| 请求内核服务 | 当前就是S-mode代码 | ecall进入S-mode的trap处理 |

用户任务进入syscall时，CPU处于S-mode，但satp仍通常是调用者的私有根。
内核代码、物理RAM和静态栈通过共享内核分支可访问。创建候选地址空间无需先激活候选根。
服务返回时继续恢复原调用者；新任务先变READY，何时运行由scheduler选择。

当前没有独立高跳板页：B08用户根借用内核槽0/2，包含相同VA/PA/权限的内核代码和栈，
trap_init将stvec直接指向trap_entry。U-mode trap不自动更换satp，S-mode仍可通过共享内核映射进入处理函数。
这与rCore用户表仅保留跳板/现场等必要Supervisor映射的隔离方案不同；后者用共同VA跳板保证切表前后连续取指。
此处是已有设计选择，不是SPAWN前漏掉的必需模块。若未来改为最小用户表，需要整体迁移trap入口/现场/栈/satp路径，
本次提问不自动变更B12范围。sched.cpp的task_trampoline是内核任务首次入口包装，与跨页表跳板页用途不同。
参考：[B08共享映射](BATCH08_PRIVATE_ADDRESS_SPACE.md)及[rCore官方跳板机制](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/6multitasking-based-on-as.html)。

两个a0属于不同现场：U1本次syscall的a0返回新公开ID；新任务初始frame的a0保存传入的tag2。
CPU的特权级由CSR/SPP恢复决定，Task.kind是软件用于维护和核对语义的记录。

## 模块与接口

新增`kernel/process/spawn.cpp`组织创建事务，不把装载/分配流程堆进syscall.c或sched.cpp。
现有boot两空间准备与转移流程保留，本批只在调度开始前配置一次运行时使用的内置镜像。

### C运行接口：include/process/spawn.h

```c
#ifndef TINYOS_PROCESS_SPAWN_H
#define TINYOS_PROCESS_SPAWN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int process_spawn_builtin(uintptr_t user_argument);

#ifdef __cplusplus
}
#endif

#endif
```

返回非负的新公开ID，失败返回-1。不向C调用方暴露Process、ELF内部描述或模板类型。
`user_argument`是传给新程序的不透明数值，内核不限制它只能为2；tag2是演示程序的选择。
成功创建仅允许在既定的安全boot阶段，或当前用户syscall内且尚未reschedule/SIE关闭时调用。
未配置时返回-1且无分配、无Task修改。

### C++配置接口：include/process/spawn.hpp

头文件包含`<span>`、`process/spawn.h`、`mm/user_loader.h`和`user/user_elf.h`并加include guard。

```cpp
namespace tinyos {

int configure_builtin_spawn(
    const Sv39PageTable* kernel_root,
    std::span<const unsigned char> image,
    const UserElfLayout& elf_layout,
    const UserMemoryLayout& memory_layout
);

}
```

配置返回0/-1，只在boot、timer/调度开始前调用一次。boot提供的镜像与布局已通过既有解析和两次装载。
基本检查包括未配置、root非空且物理范围/对齐合法、image非空、两LOAD布局等；
布局的完整装载校验继续复用loader，不另建一套解析器。

配置成功后保存到spawn.cpp私有静态状态：

- kernel_root永久借用；内置image的字节也永久借用，`span`只复制指针和长度。
- elf_layout、memory_layout必须按值复制，不能保存boot局部对象的引用/地址。
- 用现有地址类型检查stack_address+SV39_PAGE_SIZE的溢出，可保存已算好的stack_top。
- 所有可能失败的检查在配置提交前；配置成功标志最后生效。再次配置拒绝并保持原配置。
- 此配置不分配页，不负责释放永久模板或镜像，不在运行时改变。

在`boot_start_user`完成现有两用户创建后、`boot_start_scheduler`之前配置，参数为当前kernel_root、
`std::span<const unsigned char>(user_elf_start, elf_size)`、`*layout`和已有memory_layout。
配置失败沿用boot_fail。不要让syscall.c包含C++配置头或boot_private.h。

## 创建事务与失败状态

`process_spawn_builtin`按以下顺序组织，具体实现由作者完成：

1. 配置未就绪直接返回-1。
2. 局部`UserAddressSpace candidate{};`，调用`user_address_space_create`建立私有根。
3. 调用原`user_memory_load_elf`，使用静态配置中的镜像/布局引用，输出到candidate.user_memory。
4. 全部准备成功后，调用`task_create_user(entry, stack_top, &candidate, user_argument)`。
5. 成功时candidate已被转移清空，返回新ID；没有后续可失败的准备步骤。
6. 装载普通失败或task_create_user失败时，完整释放尚未转移的candidate，再返回-1。

所有页面准备都在任务ID提交之前。当前task_allocate只在成功选槽并初始化后消耗ID，
其后adopt不失败，因此普通失败不得消耗公开ID或增加高水位。
槽满时可以先装载、随后创建失败并归还候选页；复用已有容量判断，不增加另一个公共容量查询接口。

候选创建/销毁期间保持调用者satp、当前槽及其已有空间内容；不要调用sv39_activate(candidate.root)，
不要在后端直接运行新main，不yield/sleep/打开中断/reschedule。
已有私有根的创建/加载/清理都能通过共享内核映射访问物理页。

候选释放失败时诊断停机，不能返回-1后丢掉局部拥有关系。既有loader的回滚自身再次失败仍有历史限制；
遇到SV39_ERR_PAGE_FREE_FAILED等清理异常，不得把它当作已完全回滚的普通失败继续运行。
本批只要求普通分配失败/槽满能够正确清理，不宣称解决任意二次清理失败或引入通用恢复框架。

可将本模块重复的候选释放/失败诊断抽成私有助手；不调用task_reap来清理尚未发布的candidate。

## 内核栈容量

本批把`TINYOS_KERNEL_STACK_SIZE`从4096U调整为8192U，仍是静态内核栈，用户栈仍为一页4096字节。
不修改trap frame布局或汇编偏移，不移动活context，也不新增栈分配器。

准备阶段对当前实际-O0目标编译的栈用量记录显示：load_user_elf自身2224字节、C包装96字节，
还要叠加trap frame、syscall、创建后端和页表/PMM调用。8KiB是为运行时装载调用链留余量的配置选择，
不是声称已经观测到旧4KiB栈溢出。不要在后端重复按值复制大布局或临时配置，直接借用私有静态配置。
协作者将验证真实SPAWN trap及调用链用量，不要求作者额外实现栈测试机制。

## syscall与用户封装

`task/syscall.h`新增`SYSCALL_SPAWN=8`，已有编号不变。
`syscall.c`只包含`process/spawn.h`，分发新增：

```c
case SYSCALL_SPAWN: {
    uintptr_t user_argument = (uintptr_t)frame->a0;
    int new_id = process_spawn_builtin(user_argument);
    frame->a0 = (uint64_t)(long)new_id;
    break;
}
```

沿用入口已有sepc推进，服务不主动调度。参数不是用户指针，不需要copy_from或新wire结构。

user_api.h新增：

```c
long user_spawn(uintptr_t user_argument);
```

user/syscall.S沿用a0参数、设a7=SYSCALL_SPAWN/ecall/ret。EXIT的兜底循环继续紧跟EXIT ecall。

## 用户程序的两个分支

### tag2：新任务

在main入口先判断tag2并转入一个短的独立worker函数，其余tag仍走原U0/U1路径。
不要让tag2落入旧的`user_tag > 1`拒绝或U1创建分支。

worker检查counter初值7、buffer全部为0、data_message[0]为原始小写d；
随后修改自己的counter/buffer/data_message，输出一次`[U2] fresh image OK\n`，
调用user_yield并检查返回0，最后return23。数据检查失败仍调用已有fail并以1退出。

这证明从原始ELF建立新空间：U1此时counter已改变、BSS buffer已填充、data_message首字母为D。
新任务的PA必须与仍活着的U1私有页不同，但允许PMM复用已释放的U0页面。

### tag1：创建者

保留B11收集U0的循环和消息。其后：

1. 调用`long spawned_id = user_spawn(2);`，负值走fail，保存返回ID。
2. 输出一次`[U1] spawned U2\n`。
3. 用返回ID调用原user_reap，0时yield重试，-1失败，1时验证REQUESTED/status23/cause0。
4. 重新检查自己的counter、完整buffer填充值和data_message首字母仍保持。
5. 输出一次`[U1] reaped U2\n`，再执行原U1 end及return0。

新ID由内核返回，不用user_tag推算；默认4只由协作者根据当前boot顺序核对。
本批未建立父子树或等待权限，演示中的“创建者/新任务”不改变B11任意其他已结束用户可收集的策略。

## 文件范围与建议实施顺序

| 文件 | 作者工作 |
| --- | --- |
| include/process/spawn.h、spawn.hpp | C服务与C++配置接口、借用/复制和调用上下文契约 |
| kernel/process/spawn.cpp | 私有配置、候选创建/装载/转移及失败清理 |
| Makefile | 增加kernel/process/spawn.o |
| kernel/boot/private/boot_stages.cpp | 原流程完成后一次性配置内置镜像 |
| include/arch/riscv/task_context.h | 静态内核栈容量8192，用户栈不变 |
| include/task/syscall.h、kernel/task/syscall.c | 编号8与薄分发 |
| user/user_api.h、user/syscall.S | user_spawn声明与封装 |
| user/main.c | tag2短worker、U1保存ID并收集/验证 |

先写两个头与配置，再写后端事务，然后接boot/Makefile/栈容量，再接syscall和用户演示。
不重写既有调度算法、Process资源类、MM和boot旧回滚流程。

```sh
make -j4
timeout 25s make qemu
```

timeout的124仍表示观察结束。用户代码仍须通过一页RX链接断言，不能为容纳演示直接去掉断言。

## 验收与分工

P0由协作者负责，作者实现后普通运行观察即可。本批不追加作者测试框架或穷举作业。

| 等级 | 场景与预期 |
| --- | --- |
| P0 | 完整构建/无未定义、原82宿主通过、用户RX一页；原U0/U1标记保持，实际SPAWN/U2 fresh/23/收集完整链及后续S tick通过 |
| P0 | 创建时caller仍是U1根，调用前后satp/current槽/父空间保持，新Task READY且真实以U-mode运行；新frame a0=2，调用者a0返回新ID4 |
| P0 | slot2复用、公开ID4、高水位仍4，旧ID2不指向新任务；新空间root/内容页与U1私有页隔离，shared保持，fresh data/BSS通过 |
| P0 | 槽满时一次失败：候选页归还、任务表/ID无提交、调用者保持；收集旧任务后重试仍得到ID4 |
| P0 | 一次普通PMM分配失败：候选清理且无Task/ID提交，再重试成功；未配置时-1且零分配 |
| P0 | 默认新任务占7页、退出归还7页、reap不再次free；三次已发布用户生命周期各7页归还，累计21次合法free、最终净恢复初始两用户占用的14页；允许不同生命周期复用PA |
| P0 | 真实trap创建/失败清理执行，结合目标栈用量/边界观察确认8KiB内核栈余量；不把boot装载成功当作trap栈验证 |
| P1 | 重复创建/收集多轮、更多分配失败点、配置重复调用拒绝 |
| P2 | fork/exec、父子权限/孤儿进程、用户路径与多镜像选择、动态内核栈或多hart创建 |

正常容量/分配失败允许临时分配再清理，不要求零PMM调用；要求可用页数恢复及活任务内容保持。
失败清理自身异常停机的历史限制单独记录，不混同于已成功返回的普通失败。

## 正式实现验证（2026-11-08）

作者已接入boot的一次配置、SPAWN8/C接口与用户封装、候选空间创建/装载/最后发布、
普通失败回收及清理异常停机；静态内核栈已改8192U。U2从原ELF初值检查开始、写入自己的数据、
yield后return23；U1收集U0再spawn并按返回ID收集U2、核对自身数据。

协作者未修改正式功能源码。当前通过：

- `make -B -j4`无诊断，`nm -u kernel.elf`为空。
- 宿主82个场景及最终汇总通过。
- 用户RX为0xd98字节，仍在一页内；RW memory size为0x1038，仍为两页。
- 正式25秒QEMU：原U0/U1全部标记保持，start/end各一次、counter各三次，Time/Data/Hello各两次。
  U0公开ID2退出0→U1收集→新任务公开ID4 fresh image→退出23→U1收集→U1公开ID3退出0。
  U1退出后观察到15次S-mode timer，未见failure/fault/panic。观察结束由runner终止QEMU，不代表客体关机。

证据`.cache/batch12-final-2026-11-08/{build.log,undefined.log,user-segments.log,host-tests.log,normal-qemu.log,normal-result.json}`。
全部既定P0随后经独立副本探针通过，正式源码/配置70项哈希保持：

| 验证 | 实际结果 |
| --- | --- |
| 未配置 | 返回-1、零PMM调用，Task/Process/idle/shared/satp保持 |
| 真实U1 trap中槽满 | U0已DEAD但未reap，四槽仍占用；候选7页全部唯一归还、根最后。返回-1，任务/公开ID/高水位/current、活用户页表与全部内容/shared/satp保持 |
| 一次普通分配失败 | 在收集U0后第二次SPAWN的第4次PMM请求注入一次NULL，根和两个内容页已申请；真实loader与外层清理归还3页、根最后，返回-1，无Task/ID提交或页泄漏 |
| 失败后的重试 | 后续原有user_spawn(2)仍得到公开ID4、slot2/highwater4，分配7页；U2实际fresh运行、退出23并由U1收集 |
| 真实现场 | 创建期间SIE0、U1根与U1内核栈、current槽3保持；新Task先READY，新frame a0=2，调用者a0=4/sepc+4，其余寄存器保持，无重调度；随后U2实际U-mode/自己根与trap栈运行 |
| 空间与回收 | 新空间七页与存活U1七页逐一不同，共享映射保持；U0/U2/U1各归还自己的7页，每次先切内核根、根最后，幸存U1对象/页表/内容保持；两次reap均零PMM |
| 三次生命周期合计 | 合法free21次，SPAWN新增7页，最终比boot两用户占用基线恢复14页；不同生命周期允许复用PA，结果跨两次timer保持 |
| 实际8KiB栈 | 成功SPAWN watermark底部4128字节未触及，最深采样SP距底4176字节，含观测包装开销。目标.su中loader2224、SPAWN368、C包装96、syscall112、trap64，另有288字节trap frame |

失败探针证据：`.cache/batch12-final-2026-11-08/failures/`，含`results.json`、`run_probe.py`、源码副本及构建/QEMU/布局/哈希日志。
运行时探针证据：同根`runtime/`，含`results.json`、`stack-usage.json`、脚本/源码副本及日志。
总结果：同根`summary.json`。所有required checks通过，正式功能源未由协作者修改，无提交/推送/clean。

限制：普通分配失败为受控NULL注入，不是实际耗尽物理内存；栈观测仅覆盖当前单hart正常SPAWN链，
不能证明任意未来调用链安全，也不声称此前4KiB栈已实际溢出。旧loader回滚自身再次失败的历史限制仍在，
本批用诊断停机处理该异常，不宣称通用恢复。P1/P2仍选做，不阻塞本批理解收尾。

## 功能梳理与理解收尾

2026-11-08功能/P0通过后已交付以下梳理和原三题；作者原答及集中审查见下一节。

1. boot调用configure_builtin_spawn：借用永久kernel_root和内置ELF字节，复制两个布局，保存检查后的栈顶，最后提交g_root表示配置完成；不创建新任务或分配页。
2. U1执行user_spawn(2)：a0=2/a7=8后ecall，进入S-mode、切到U1内核trap栈，satp仍为U1私有根。共享的Supervisor内核映射使服务及物理页访问可达；不激活candidate根。syscall_handle将调用者sepc前进4。
3. 后端局部candidate先拥有新根，再由loader从永久ELF准备代码/栈/data/BSS与映射。默认7页为1根+2私有下级表+4内容页；静态Task内核栈不计入这7页。不复制U1已改写的运行内存。
4. 全部准备成功后task_create_user复用slot2、分配公开ID4，准备新frame(entry/SP/a0=2/SPP0)，将candidate转交同槽Process并清空来源，Task为READY。syscall把4写到U1原frame.a0；创建本身不重调度，新任务尚未因此执行main。
5. 调度器之后选择该任务、激活其私有根并恢复新frame，sret回U-mode，从ELF入口经_start调用main(2)。fresh检查通过、改自己的数据/yield后return23，用户启动代码将其交给EXIT。
6. EXIT保存REQUESTED/status23，标DEAD、先切永久内核根，再显式归还U2七页，留下静态Task/context和Process内退出记录。U1按返回ID4调用REAP，活时0、退出后先copy结果到U1空间，成功才消费记录并置槽UNUSED；reap不再次free。普通创建失败则由candidate路径回收后-1，清理自身失败诊断停机，发布前不消耗公开ID。

原答及集中审查后续仍在本文件保存：

1. U1调用SPAWN时CPU在哪个特权级、使用谁的页表和栈？为什么不需要先激活新根？
2. 调用者得到的新ID和新任务收到的tag2，分别放在哪个frame的a0？为什么SPAWN成功不等于新任务已经运行？
3. 槽满/装载失败为什么不能消耗新ID或丢掉candidate？同一ELF的新任务与复制U1运行中内存有什么不同？

## 三题原答、集中审查与修正版（2026-11-08）

作者一次提交三题答案，以下逐字保留，不把协作者补充写成作者已经复答。

### 第一题原答

> U1执行SPAWN先使用自己的页表和内核栈，在内核状态直接操作物理页，而且是恒等映射状态下没必要激活，而且可以防止地址冲突

审查：U1私有根/内核trap栈和恒等映射访问新物理页的方向正确。需补充CPU处于S-mode，普通访存仍经过satp页表翻译；物理页指针在当前恒等内核映射中VA数值恰好等于PA，并非切到S-mode就绕过分页。
不激活candidate的直接原因是现有U1根已经有共享Supervisor内核映射，足以访问新表和新内容页；不是靠不切根消除用户VA冲突。U1和U2允许使用相同用户VA，由各自页表映射到不同PA。loader写新分配页的内核地址，不能在U1根下把目标用户VA当作新任务的目标指针，否则会命中U1的现有映射。

协作者整理的修正版：SPAWN在S-mode/U1内核栈/U1私有根下执行，共享内核映射与恒等物理内存窗口允许直接构造candidate的页表和内容；无需激活新根。同VA隔离依赖不同根指向不同私有页，candidate仅在以后调度运行新任务时激活。

### 第二题原答

> 2在U2的frame,4在调用的用户frame，返回ID时候，不一定U2以及被启动了，中间还要U1中间运作并且ret不出错

审查：两个frame/a0区分正确，创建成功不保证新任务已执行也正确。原因应落实到状态与调度：task_create_user仅准备现场、转移空间并使Task READY，SPAWN分支不调用sched_reschedule；后续定时器抢占或yield等调度机会才可能选中U2、切根/恢复新frame/sret。
无需等U1完成某段用户逻辑或某个ret才允许U2运行。U1恢复用户态后可能立即被抢占，因此U2首条日志可能早于U1的spawned日志；在调用者实际观察到返回值时，新任务也可能已运行。

协作者整理的修正版：参数tag2写U2新frame.a0，返回公开ID4写U1原frame.a0。发布时U2是READY且尚未因创建操作直接执行；是否已经运行取决于后续调度时机，不能用U1的ret或日志顺序作保证。

### 第三题原答

> 可能导致内存泄漏，当前进程未创建成功，随意消耗ID可能导致id稀缺造成空洞，因为U2复制到的是初始值7而不是U1的counter

审查：candidate丢失导致资源无人负责/泄漏以及原ELF初值7判断正确。公开ID空洞本身不是通用错误，ID也不要求连续；本批有意选择“准备完成后才提交Task/ID”的事务约定，让失败不改下一ID/任务表/高水位，便于失败后重试，不把ID稀缺作为唯一理由。
更精确的来源是从永久内置ELF复制.data初值，并为.BSS清零；不会从U1的运行时用户页复制counter。若是fork继承当前内存快照，才会先看到U1此刻的203。

协作者整理的修正版：candidate是发布前的临时资源拥有者，普通失败必须完整释放后再返回-1；释放自身失败则诊断停机。任务槽和公开ID在全部页面准备成功后的创建阶段提交，因此失败不消耗ID。新任务按原ELF重新初始化，counter=7、BSS=0，和U1已修改的运行数据独立。

本批功能、全部既定P0、执行路径梳理、原答及集中纠正均已归档。不追加复答或新测试门槛，不自动启动下一批。

## 配置草稿检查（2026-11-07）

作者已新增接口和配置草稿。root用PhysAddr验证物理编码/页对齐；code/data/stack起点用VirtAddr验证
canonical/页对齐，try_add得到checked stack_top。entry和image字节起点不要求页对齐；
固定两LOAD的count先确认等于最大支持数，段内完整检查由loader承担，不在配置重建解析器。
可以用g_root==nullptr表示未配置，在所有检查与数据保存后最后提交非空g_root，无需额外重复ready布尔。
需保存image span、两布局副本及栈顶；现存全局空candidate指针应改为实际spawn中的局部对象。
头与实现拼写spwan须统一为spawn。当前未实现后端事务或运行SPAWN，未修改作者源码。

## 准备记录与限制

2026-11-07后续源码检查：spawn.o、syscall和用户封装已接入；后端已有配置保存、未配置拒绝、
创建根/装载/发布及两处分支释放检查。U2和U1的spawn/轮询reap演示已保存。
make -j4确认目标已最新，kernel.elf无未定义符号；尚未强制重编或运行SPAWN/P0。
当前仍缺boot调用configure_builtin_spawn、原定静态内核栈8192U，以及loader内部
SV39_ERR_PAGE_FREE_FAILED的独立停机处理。U2首段重复BSS检查可合并到已有sizeof循环。
本次仅审查与记录，正式功能由作者继续修改。

2026-11-07：作者在B11归档后要求继续。协作者对实际Process、loader、task_create_user及boot路径做设计审查，
选择内置镜像SPAWN作为下一批，只交付任务，不代写核心。
临时C/C++头接口按目标编译无诊断；实际loader/VM/PMM使用-fstack-usage做静态用量记录，
证据.cache/batch12-preparation-2026-11-07/。这些不代表新服务实现或运行验收通过。
仍为单hart、静态槽和内核栈、单一内置ELF、ASID0与本地刷新；不自动扩展rCore教程或VFS。
