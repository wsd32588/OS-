# BATCH18：选择不同的内置 ELF

日期：2026-11-11—12。状态：功能、全部既定P0、执行路径梳理及原三题原答/集中澄清/修正版均归档，本批收尾。最终结果见第12节；此前待修项按历史过程阅读。

前置：[B17 用户任务操作](BATCH17_USER_TASK_OPERATIONS.md)的功能、P0和三题已收尾。
本批回到功能推进：保留现有创建、替换、父子和回收事务，让 SPAWN/EXEC 能选择两个真正独立的程序。
核心由作者实现，协作者集中审查并承担下面预先确定的 P0。

## 1. 要观察到什么

| 程序编号 | ELF | 初始数据 | 本批用途 |
| --- | --- | --- | --- |
| `USER_PROGRAM_DEMO = 0` | `user/build/demo.elf` | `counter = 7` | 原双用户演示及运行中的父程序 |
| `USER_PROGRAM_WORKER = 1` | `user/build/worker.elf` | `counter = 123` | 通过新 SPAWN 创建、通过新 EXEC 替换进入 |

两个 ELF 共用启动汇编、syscall 包装和链接布局，各自链接自己的 `main`。
WORKER 是独立的 `user/worker.c`，不是 DEMO 换一个 tag 后重新编译。
它们可以使用相同的入口 VA、栈 VA 和数据 VA；各自装入私有物理页。

正常演示保持两 S、两个 boot USER：U1收集U0后创建 WORKER，传参数3，子退出23；
随后 fork 出来的孩子先验证 DEMO 快照，再 EXEC 到 WORKER，传参数17，同一孩子退出37。
父进程继续保持自己的 DEMO 数据。正常公开 ID 及退出顺序仍为2/4/5/3，状态0/23/37/0。

本批只用整数编号选择内置 ELF。路径、VFS、argv/envp、动态程序发现、阻塞 wait 和 COW 留在后续。

## 2. 先分清三个整数

- `program_id` 选择只读 ELF 描述，0和1合法，不是任务身份。
- `argument` 传给新程序入口，保存到 fresh frame 的 `a0`；0也合法。
- 返回的 `task_id` 是 TaskTable 分配的公开身份，EXEC 保持它，SPAWN 新建它。

不向 Task/Process 加 `program_id`，不增加 PID 分配器。
FORK 复制当前页和现场，不能重新查镜像表装初始 ELF，否则运行中的数据与调用栈会丢失。

### 共享编号与用户接口

新增 `include/task/builtin_program.h`，使用 include guard、`<stdint.h>`，仅提供共享常量：

```c
#define USER_PROGRAM_DEMO UINT64_C(0)
#define USER_PROGRAM_WORKER UINT64_C(1)
```

`include/task/syscall.h` 保留1—11，新增：

```c
#define SYSCALL_SPAWN_PROGRAM 12
#define SYSCALL_EXEC_PROGRAM 13
```

`user/user_api.h` 新增以下声明；原有 `user_spawn(argument)`、`user_exec(argument)` 保留。
共享编号头可由调用者直接包含，用户 API 头不引入内核镜像类型。

```c
long user_spawn_program(uint64_t program_id, uintptr_t argument);
long user_exec_program(uint64_t program_id, uintptr_t argument);
```

`user/syscall.S` 各增加一个 `li a7, 编号; ecall; ret` 包装。
C ABI 已经把两个实参放到 `a0/a1`，不需要搬寄存器。
SPAWN成功返回新公开ID，失败-1；EXEC成功从新程序入口执行，失败回旧程序并返回-1。

## 3. 内置镜像表：配置归自己，运行状态仍归 scheduler

新增 `include/process/builtin_program.hpp` 和 `kernel/process/builtin_program.cpp`。
这个模块拥有一次性配置，不读取 scheduler 的任务表、current 或 reaper 全局。
配置完成后只读；单 hart、boot配置阶段、SIE关闭是调用前提，不引入锁或动态内存。

头文件接口如下，指针/引用涉及的类型只需前置声明：

```cpp
#ifndef TINYOS_PROCESS_BUILTIN_PROGRAM_HPP
#define TINYOS_PROCESS_BUILTIN_PROGRAM_HPP

#include <span>
#include <stdint.h>

struct Sv39PageTable;
struct UserAddressSpace;
struct UserMemoryLayout;

namespace tinyos {
struct BuiltinProgramImage {
    std::span<const unsigned char> bytes{};
};

int configure_builtin_programs(
    const Sv39PageTable* kernel_root,
    std::span<const BuiltinProgramImage> images,
    const UserMemoryLayout& memory_layout);

int prepare_builtin_user_space(
    uint64_t program_id,
    UserAddressSpace& output,
    uintptr_t& entry_out,
    uintptr_t& stack_top_out);
}

#endif
```

这三个现有类型都有 struct tag；不要为前置声明复制类型定义。
实现文件再包含 ELF、loader、地址类型和地址空间的完整头。
旧 `include/process/spawn.hpp` 的配置/准备声明迁到新头，更新全部包含后删除旧空头；C接口 `spawn.h` 保留。

### configure：所有检查通过后再发布

本批固定两条记录，输入顺序就是程序编号，要求 `images.size() == 2`。
内部记录保存 `bytes` 和自己的一份 `UserElfLayout`，另存共同的 memory layout、stack top及借用的 kernel root。
记录类型和数组只在 `.cpp` 中可见，不向用户 ABI 暴露 ELF 元数据。

1. 已配置则拒绝；检查 kernel root 的物理地址范围、页对齐，共同 code/data/stack 起点非零、canonical、页对齐。
2. 用 `VirtAddr::try_add(SV39_PAGE_SIZE, ...)` 得到栈顶，处理溢出和非 canonical。
3. 先在局部两条记录中逐个检查非空字节 span，调用 `user_elf_parse`。
   本批要求每个镜像 entry非零、program header非零、LOAD数恰为 `USER_ELF_MAX_LOAD_SEGMENTS`（2）。
4. 全部解析和基本检查成功后，复制局部记录、共同布局和栈顶，最后赋全局 root，作为配置完成标志。

配置不申请 PMM 页。第二个镜像坏掉、重复登记或非法布局不能留下第一条已登记的半配置。
这里的“最后 root”是在既定单 hart/SIE0前提下提交配置，不是多核原子发布协议。

解析成功仅表示 ELF 格式通过和上述基本条件满足。段地址与共同布局匹配、权限、大小、区间等详细装载条件
继续由 `user_memory_load_elf` 检查，不能写成“配置成功保证以后装载必成功”，也不要为了验证而临时分配用户页。

`images` 数组可在 boot 栈上；模块复制其中的 span 和解析布局，不能保存数组元素或局部解析结果的引用。
span 底层的 ELF 字节来自内核只读 incbin 区，整个运行期有效；kernel root及共享映射同样必须持续存活。

### prepare：先选择，再准备一个调用者拥有的候选

调用者传入空 `UserAddressSpace`；普通失败可能留下部分候选，由调用者统一 release。

1. 先检查已配置以及完整 `uint64_t program_id < 2`，再下标访问和分配。
   不先转 `int/uint32_t`；`UINT64_MAX`、`0x100000001`都要拒绝，不能截断为合法编号。
2. 未配置或编号非法时，output、entry_out、stack_top_out全部不变，零 PMM 调用。
3. 使用选定记录：create私有根 → loader装入选定ELF。
4. 保留原 loader 回滚失败 `SV39_ERR_PAGE_FREE_FAILED` 的诊断停机策略。
   普通错误返回，不擅自消费调用者候选的所有权；调用方负责清理。
5. 仅完整成功后写 entry_out 和 stack_top_out。两者来自选定布局/共同栈配置。

现有 loader 已执行带 memory clobber 的 `fence.i`，本批不另造指令缓存模块。

## 4. 接回 SPAWN、EXEC 和 syscall

### SPAWN

`include/process/spawn.h` / `kernel/process/spawn.cpp` 的 C入口改为：

```c
int process_spawn_builtin(uint64_t program_id, uintptr_t argument);
```

`spawn.cpp` 先包含自己的 `spawn.h`，仅保留编排：
空 candidate → prepare(id, ...) → 原四参数 `task_create_user` → 返回公开ID。
准备/发布普通失败都清候选；清理失败仍诊断停机。成功 adopt后 source为空，不重复释放。
镜像表、配置和解析移出本文件，不把它们再搬进 scheduler。

### EXEC

以下两个接口加入编号，声明、定义、全部调用者同步更新：

```cpp
// include/task/sched.h：原 extern "C" 边界内。
int sched_exec_current(struct trap_frame* frame, uint64_t program_id, uintptr_t argument);

// include/task/private/user_task.hpp：原 namespace内。
int exec(task& current, trap_frame& frame, const Sv39PageTable* kernel_root,
         uint64_t program_id, uintptr_t argument);
```

scheduler 薄壳保留 current/SPP/SIE 检查，将编号和参数显式传下去。
`user_task_exec.cpp` 改为包含新镜像头，准备阶段按编号选镜像；保留现有候选验证、失败清理和提交顺序。
提交仍为：激活内核根 → 释放旧空间 → adopt新空间 → 安装fresh frame → 激活新根。
fresh `sepc=entry`、`sp=stack_top`、`a0=argument`、`sstatus=SPIE`；Task/ID/parent/内核栈保持。
提交开始后的不可恢复错误仍由 `exec_commit_fail` 停机。

### syscall 分发约定

| 调用号 | 读取原现场 | 内部选择 | 成功后写什么 |
| --- | --- | --- | --- |
| SPAWN8 | a0=旧argument | DEMO0 | a0=新task_id |
| EXEC10 | a0=旧argument | DEMO0 | 保持fresh frame |
| SPAWN_PROGRAM12 | a0=program_id，a1=argument | 显式编号 | a0=新task_id |
| EXEC_PROGRAM13 | a0=program_id，a1=argument | 显式编号 | 保持fresh frame |

旧8/10在 dispatcher 显式补 `USER_PROGRAM_DEMO`，保持旧二进制调用语义。
新的EXEC case在调用前读出两个值，避免替换frame后再读到新现场：

```c
case SYSCALL_EXEC_PROGRAM: {
    const uint64_t program_id = (uint64_t)frame->a0;
    const uintptr_t argument = (uintptr_t)frame->a1;
    if (sched_exec_current(frame, program_id, argument) != 0) {
        frame->a0 = (uint64_t)(long)-1;
    }
    break;
}
```

不能无条件把EXEC返回的0写入a0。dispatcher原有 `sepc += 4` 仍只执行一次，
成功EXEC使用fresh entry；失败返回旧ecall之后。FORK原有父/子返回值与snapshot规则不变。

## 5. 两个 ELF 的构建与嵌入

Make 把公共对象与两个入口分开，例如：

```make
USER_COMMON_OBJS = \
    $(USER_BUILD_DIR)/start.o \
    $(USER_BUILD_DIR)/syscall.o
USER_DEMO_OBJS = $(USER_COMMON_OBJS) \
    $(USER_BUILD_DIR)/main.o
USER_WORKER_OBJS = $(USER_COMMON_OBJS) \
    $(USER_BUILD_DIR)/worker.o
USER_OBJS = $(sort $(USER_DEMO_OBJS) $(USER_WORKER_OBJS))
```

demo.elf只链接DEMO_OBJS，worker.elf只链接WORKER_OBJS，不能把两个main一起链接。
两者使用现有 `user.ld`，保留O0默认、RX不超过4096断言和两个LOAD。
`USER_OBJS`并集用于依赖/clean列表，新增 `kernel/process/builtin_program.o` 到内核OBJS。
`user`目标构建demo.bin及worker.elf，原demo.bin继续保留；没有使用者时不必生成worker.bin。
clean规则维护worker.elf（本轮不执行clean）。

`kernel/user/user_entry.S` 保留原raw image和 `user_elf_start/end`，新增8字节对齐的
`user_worker_elf_start/end`，incbin `user/build/worker.elf`。
**`kernel/user/user_entry.o`显式依赖demo.bin、demo.elf、worker.elf**，不能只改汇编而不补Make依赖。
两个镜像源码和共享syscall头的增量修改，都要重建对应ELF、嵌入对象及kernel.elf。
PMM起点原本按 `__kernel_end` 算，新增内置字节会自然计入内核范围。

## 6. boot 统一经过镜像准备接口

现在的 `boot_load_user_elf` 只解析一个镜像，而新的registry会解析两个。
删除这个旧stage及 `boot_private.h` 中的声明；`kernel_main`移除layout/elf_size局部变量和调用。
boot私有头不再需要为这个stage包含ELF布局头。
`boot_start_user`只接收 `kernel_root, satp_value`，主调/声明/定义一致。

boot工作顺序：

1. 准备共同memory layout。用两个ELF的链接器符号分别取得span。
   各自先转uintptr_t并检查end>start，再作减法/构造span，不对两个独立声明的数组指针直接相减。
2. 建局部 `const tinyos::BuiltinProgramImage images[]`，顺序DEMO、WORKER。
   span/数组准备放在两S创建段之前，便于现有压力副本只移除两S演示。
3. 原sched_init及task_a/task_b创建保持，然后调用一次 `configure_builtin_programs`。
   原单镜像解析日志可换成明确的“两内置程序配置完成”，不用为打印再增加可变getter。
4. 两个boot空间都通过 `prepare_builtin_user_space(USER_PROGRAM_DEMO, ...)` 准备。
   保存每次得到的entry/top，例如entries[2]/stack_tops[2]，再进入第二个循环调用四参数task_create_user。
5. 任何普通准备/创建失败仍通过 `release_pending_user_spaces` 清全部待交付空间。
   成功adopt的source已为空；第二份失败也不能漏第一份。loader自身回滚失败沿新helper的fatal策略。
6. 两USER参数仍0/1，公开ID仍2/3；按原逻辑登记ID3为reaper，再启动调度。

两阶段“全准备，再发布”保留；不在boot并行维护第二套loader流程。

## 7. 演示改动及代码页预算

### 新的 worker.c

实现一个很小的独立程序：

- `static volatile unsigned long counter = 123;`，可写字符串初值首字母`w`，
  `volatile unsigned char buffer[4096 + 32]`为BSS，保证数据仍占两页。
- 检查seed123、字符串初值和全部BSS零；保存入口argument。
  示例程序可自行限制argument<=100，内核接口仍允许任意uintptr_t参数。
- 改自己的counter、字符串、buffer，yield后核对这些值及一个volatile栈局部变量。
- 输出 `[worker] fresh image OK\n`，返回 `(int)argument + 20`。

这样参数3得到23，参数17得到37，能观察到与DEMO seed7不同的初始镜像。

### 原 DEMO

原数据/BSS/多轮yield/sleep/time/write坏地址/fork快照与隔离检查保留。
保留tag2/3的旧fresh分支行为，让旧SPAWN/EXEC默认镜像仍可按原约定工作；
可将两段重复write/yield合成一段，消息仍分别为原U2/exec文本，返回23/37。

U1收集U0之后：

```c
if (user_spawn_program(UINT64_MAX, 0) != -1 ||
    user_exec_program(UINT64_MAX, 0) != -1) {
    fail("invalid program ID accepted\n");
}
const long spawned_id = user_spawn_program(USER_PROGRAM_WORKER, 3);
```

随后按原流程等待这个真实返回ID/status23。fork孩子验证并修改旧DEMO快照后改为：

```c
user_exec_program(USER_PROGRAM_WORKER, 17);
fail("exec returned\n");
```

父仍等待该fork ID/status37。两个新worker各输出一次fresh消息；fork孩子仍先输出snapshot OK。
U1的数据、父关系和最后退出行为保持。

协作者已在临时副本合并旧重复分支、加入两个包装和非法编号检查后，按实际O0选项编译链接：

| 镜像 | RX字节 | RW内存字节 | 未定义符号 |
| --- | ---: | ---: | --- |
| DEMO候选 | 3962 | 4152 | 无 |
| WORKER候选 | 857 | 4152 | 无 |

证据 `.cache/batch18-preparation-20261111/results.json`、build.log、source/及两个layout.txt。
头自包含/重复包含和新接口调用通过RV64 C++20语法检查，正式产品源哈希保持。
候选仅用于说明体积与接口可行，不是正式实现，更不是本批QEMU验收。
不要通过放大代码页或改优化级别掩盖预算问题；后续若超限，先核对重复分支和诊断文本。

## 8. 整批实现顺序与检查点

1. **先写共享编号头、新builtin_program.hpp，以及registry配置。**
   把旧spawn配置里的地址检查迁过去，加入两个局部ELF解析和一次提交；暂不改任务生命周期。
2. 迁移prepare，spawn变为两参数编排；EXEC从薄壳到模块传递编号。
3. 增加新syscall和用户包装，旧8/10显式补默认编号。
4. 增加worker、Make和incbin，统一boot配置及prepare。
5. 修改正常DEMO，整批提交给协作者集中检查，再完成既定P0和理解收尾。

可先用目标对象/`make user`检查编译，全部接线后：

```sh
make -j4
make test
riscv64-linux-gnu-readelf -lW user/build/demo.elf
riscv64-linux-gnu-readelf -lW user/build/worker.elf
timeout 20s make qemu
git diff --check
```

timeout124只是限时停止；要结合用户成功消息、退出状态和退出后S任务timer判断结果。
新增输入接口后需要更新compile_commands时再运行 `make compdb`。

## 9. 验收范围与责任

### P0：协作者承担，整批后集中执行

1. 构建/依赖/原宿主回归：两个独立ELF、两LOAD、O0/RX<=4096、无未定义符号；
   改任一用户入口或共享syscall头能重建正确ELF和嵌入对象，不链接重复main。
2. 配置/查找契约：坏第二镜像、重复配置不改变已保存配置，零PMM；
   未配置、`UINT64_MAX`及高位伪合法ID在分配/下标前拒绝，候选/entry/top不变；合法编号0、参数0可以通过。
   这些探针在隔离副本完成，不增加产品reset配置接口。
3. 真实新旧ABI：旧8/10仍默认DEMO；新12/13分离镜像与argument，
   正常WORKER seed123/BSS零与参数3/17分别退出23/37；同VA可对应不同镜像内容。
   非法编号零PMM/不耗ID，失败EXEC后旧用户实际继续运行。
4. 正常生命周期：ID2/4/5/3和退出0/23/37/0、父关系/EXEC同身份与内核栈保持；
   五份地址空间仍各7页，共35次合法释放，运行期21次分配、净释放14页，三次收集零PMM，最后S任务持续运行。
5. 选中WORKER的一次准备OOM：候选精确清理，旧EXEC现场/页/parent/活动根保持，用户实际继续并可重试成功。
   沿用现有失败探针，不新增穷举矩阵。
6. 原500轮压力保留：协作者适配 `test/verify_task_table_stress.py` 的Make分组和
   `configure_builtin_spawn`→`configure_builtin_programs`启动锚点，仅在隔离副本移除两S演示。
   旧测试payload继续调用默认DEMO的8/10；500轮2000worker、满槽拒绝500、逐轮资源平衡继续通过。

核心实现作者完成；例行探针/脚本适配由协作者维护，不把上面清单变成作者额外的测试框架作业。
500轮验证默认路径经过共同registry/prepare，但不能据此宣称穷举了两个程序的全部组合。

### P1：建议选做，不阻塞推进

让同一压力轮混合两个程序，或在worker里fork并检查seed123的运行快照。
学习点是“程序来源”与“运行状态”分离；需扩展现有payload，不另建任务工厂。

### P2：可选

逐个分配点故障注入、更多内置镜像或ELF格式组合矩阵。本批不要求实现。

## 10. 实现后梳理与三题

功能与P0完成后，再按实际源码梳理配置→查找→装载→发布/替换→退出/收集；届时一次讨论下面三题。

1. `user_spawn_program(1, 17)`中的1、17和返回task_id各属于哪层？EXEC成功后a0应该是什么？
2. 两个任务为何可以借用同一份内置ELF字节，却仍有各自的数据/BSS？FORK时为何不能重新装那个ELF？
3. 未知编号与装载中途OOM分别在哪一步失败？谁清候选，为什么旧EXEC程序此时还能继续？

原三题的原答、审查和修正版均在下文归档；集中解释后不要求重复作答，功能验证另按P0结果判断。

### 第1题原答与澄清（2026-11-12）

作者原答逐字保留：

> user_spawn_program的1是调度选择ELF描述，17是给新程序入口用的，task_ids是属于调度器用于选择进程，exec成功后a0是USER_PROGRAM_WORKER

用途已分清一部分：1确实选择WORKER的ELF描述，17确实是新入口参数。集中纠正：

- 程序编号由builtin_program模块查镜像表解释；scheduler选择可运行Task，不按程序编号选择ELF。
- task_id是任务的公开身份，不等于调度槽索引；TaskTable将ID查成槽，调度轮转使用槽和状态。
- `user_exec_program(USER_PROGRAM_WORKER, 17)`成功后fresh frame的a0是17；
  USER_PROGRAM_WORKER（值1）只是选择镜像的输入，不作为该入口参数。
- `user_spawn_program(USER_PROGRAM_WORKER, 17)`成功时，调用者a0得到新task_id，
  孩子初始frame的a0得到17；SPAWN的两个frame不能混为一个。EXEC保持原task_id。

修正版：程序编号1选择WORKER，argument17进入新程序a0；公开task_id标识任务，调度槽单独维护。
SPAWN给调用者返回新ID，给孩子入口传17；EXEC在同任务安装新镜像，成功进入新入口时a0仍为17。
本题已集中解释，不要求重复作答；整批P0仍按当前验证状态推进。

### 第2题原答与澄清（2026-11-12）

作者原答逐字保留：

> 因为有两个用户空间互不干扰？data和bss是独占物理页的，而text和rodata才是同样的物理页内，fork时候是直接复制的父进程的task，程序计数器（PC `sepc`）和寄存器上下文对不上  也无法继承父进程的映射与共享状态

data/BSS私有、重新装初始ELF不能代替运行快照的方向正确；按当前源码集中澄清：

- 同一个内置ELF的只读来源可被多次装载，但loader给每条用户内容页记录各自pmm_alloc_page；
  当前text/rodata装载页也私有，不共享用户代码PA。text和rodata同属一个进程的RX页，不等于不同任务共用该PA。
- 当前eager fork逐个复制所有用户内容页，包括代码、数据/BSS及用户栈，保留VA/权限但使用新PA，
  私有页表新建；共享的是借用的内核映射，不是用户代码页/COW。
- fork分配新的Task/公开ID，孩子有自己的内核栈/frame/Process；只把父trap_frame数值复制到孩子frame，
  不是复制整个Task对象或让两个任务共用frame指针。子a0=0，父得到新ID，sepc沿用已推进的旧调用点。
- 问题不在正常fork的PC/寄存器对不上；它们恰须与复制的用户栈和运行数据配套。
  若重新装ELF，改过的data/BSS回到初值、原调用栈丢失，便无法继续原fork调用后的执行。

修正版：内置ELF是可重复读取的只读源，实际用户代码/数据/栈目前都独占；fork复制当前页与寄存器快照到新Task，
继承同VA和权限的映射语义，不能用重新装初始ELF代替。本题解释后不重答，下一题仍是失败与候选归属。

### 第3题原答与澄清（2026-11-12）

作者原答保留（代码按原内容排版）：

```cpp
builtin_program.cpp里 if (g_root == nullptr || program_id >= kBuiltinProgramCount) {
        return -1;
    }
，
    const int create_result = user_address_space_create(g_root, &output);
    if (create_result != SV39_OK) {
        return create_result;
    }

    const int load_result = user_memory_load_elf(
        output.root,
        program.bytes.data(),
        program.bytes.size(),
        &program.layout,
        &g_memory_layout,
        &output.user_memory);

spawn.cpp里  const int result = tinyos::prepare_builtin_user_space(
        program_id,
        candidate,
        entry,
        stack_top);

    if (result != SV39_OK) {
        int res = user_address_space_release(&candidate);
        if (res != SV39_OK) {
            uart_puts("release space candidate failed");
            for (;;) {
                asm volatile("wfi");
            }
        }
        return -1;
    }并没有直接销毁之前的旧exec程序
```

核心判断正确：配置/编号拒绝在创建与分配之前；create或loader中途失败返回错误，调用者清理候选，准备阶段不销毁旧EXEC程序。
需要定位清楚两条调用路径：原答的spawn.cpp负责新建任务；EXEC对应候选清理实际在user_task_exec.cpp的prepare错误分支。
EXEC只在候选完整准备并验证成功后才进入切kernel root/release旧空间/adopt/fresh frame/activate新根的提交阶段。
普通准备失败仍使用旧空间、用户栈及运行数据；dispatcher把失败返回值-1写入旧frame.a0，从旧ecall后继续。
候选清理失败则诊断停机，不能把未清资源当成功回滚。提交开始后的失败也不能当普通准备错误返回。

修正版：未知编号零分配拒绝；准备OOM可能留下部分候选，由SPAWN或EXEC各自编排入口释放。
EXEC在准备阶段没有释放旧空间，因此普通错误可返回旧程序继续；旧空间释放属于后面的提交阶段。
三题已全部集中澄清和归档，不追加重答要求。

## 11. 当前结论与限制

2026-11-11：已完成源码核对、独立只读方案审查、临时接口与体积检查，并一次交付本任务。
正式产品实现未改，未运行新内核/QEMU，未提交或推送。下一步从第8节第1项开始。

2026-11-12：作者已实现配置函数的全预检/两ELF局部解析/统一保存/root最后提交及重复配置拒绝。
协作者检查当前源码，RV64 C++20 `-fsyntax-only` 无诊断；函数逻辑符合本节配置契约。
局部解析结果位于函数内，持久配置位于本实现匿名namespace，布局复制、ELF字节span借用。
此结论仅覆盖配置函数静态审查，不代表运行时配置拒绝路径或B18整批验收已完成。
下一步迁移prepare，编号在访问记录和分配前按完整uint64检查；产品代码未由协作者代改。

2026-11-12（Makefile之前阶段检查）：作者已接prepare、两参数spawn、sched到user_task::exec传编号及新用户包装。
configure/prepare的检查与候选归属、EXEC准备/提交及fresh a0保持符合约定。
当前三处待修集中列明：

1. syscall旧8/10被误改为双参数，新12/13尚无case；现双参数分支改名PROGRAM12/13，
   补回旧8/10默认DEMO、原a0为argument的分支，成功EXEC仍不覆盖fresh frame。
2. spawn.cpp保留旧configure_builtin_spawn及旧五个配置变量；删除这些已迁职责，只保留准备/发布/清候选编排。
   boot调用仍为旧配置属于后续未接阶段，之后必须改用新registry；全部旧include移除后删spawn.hpp。
3. builtin_program.h缺自己的<stdint.h>，独立展开UINT64_C失败，应补头依赖；syscall.c使用默认编号时包含此共享头。

范围内4个C++实现、按Make实际默认GNU模式的syscall.c语法检查无诊断，4个接口头重复包含/自包含及用户汇编组装通过；
共享编号头独立宏检查如上失败。初始额外严格C11探针拒绝既有asm，已按实际内核模式复查，不算产品缺陷。
证据.cache/batch18-stage-review-20261112/results.json和各日志，审查前后产品源hash保持。
未代改产品，未执行当前半接线状态的全链接/QEMU；Make、boot和worker仍待后续实现，本批P0未宣告通过。

同日复查：以上三项作者已修，编号头独立检查通过；剩余新问题是旧SPAWN8和EXEC10各自末尾缺break，
目标编译已实际报两条implicit-fallthrough。各自右花括号前补break，避免继续落入新12/13分支。
spawn编排及新旧参数约定其余符合当前阶段；boot旧引用保留到后续接线，不作为当前额外错误。

同日Make复查：两处break作者已补。Make对象分组已建立，但demo规则仍使用USER_OBJS并集，
还需按第5节改为两个ELF各自对象组、补worker链接/user双目标、builtin_program.o、incbin依赖及worker.elf清理项。
`make -n user`实际报worker.o needed by demo.elf；当前worker.c未建，未执行构建/clean。
新worker/incbin标签和boot仍属后续阶段，不将未完成步骤说成已通过，本批P0待全部接线后执行。

同日boot复查：作者已完成两参数声明/调用、两ELF描述/一次配置、两份DEMO prepare及原两阶段发布/清理/reaper，
main.cpp与boot_stages.cpp目标RV64语法检查无诊断。误删user_elf.h已单文件恢复，与index/HEAD相同。
当前两项接线问题集中列明：WORKER incbin误追加到kernel/boot/entry.S的.bss.stack，应移动到kernel/user/user_entry.S只读镜像段；
Make追加正确DEMO规则后未删旧USER_OBJS规则，后者覆盖前者，需删除旧规则。
make -n user已实际报overriding recipe和worker.o缺源；worker.c仍未创建，是接下来实现步骤。
无产品代改/真实QEMU，本批尚未验收。

随后复查：两项均已由作者修复，WORKER嵌入代码位于kernel/user/user_entry.S的只读镜像段，
旧DEMO规则已删；make -n user无覆盖警告，DEMO只链公共对象/main，worker.o缺源正确归worker.elf。
下一步实现user/worker.c及DEMO的新选择演示，完整构建/QEMU仍待这些源码完成。

同日worker构建检查：源文件误命名user/wrok.c，协作者仅改为user/worker.c，内容保持。
worker实际编译/链接通过，RX1073/RW4152、两个LOAD；make user另因DEMO超代码页断言失败。
当前集中待修：合并DEMO旧tag2/3重复write/yield；user_spawn(3)改为user_spawn_program(USER_PROGRAM_WORKER,3)；
worker删除yield前成功输出，保留全部校验后的一次，避免当前重复成功消息。
临时main副本完成前两项后实际O0链接RX3962/RW4152通过，证据.cache/batch18-worker-name-review-20261112/。
未代改正式main/worker函数内容，未执行真实内核/QEMU，整批P0仍待最终源码。

同日正常QEMU：作者修正上述三项后，正式make成功。原timeout8秒只观察到spawn/yield中途，不能由此判断卡住；
协作者实际观察20秒，两个worker成功消息、ID4/status23、ID5/status37、父ID3隔离成功/status0及U0/status0均通过，
最后用户退出后至少两S ticks，无fault/halt，返回124为预期限时结束。观察命令已改20秒，流程包含sleep/多次yield/内核任务轮转。
证据.cache/batch18-observe-20261112-220621-3/results.json与qemu.log，源hash保持。
本次8项正常日志检查通过，不等于资源账本、registry边界、OOM、旧ABI与500轮压力等完整P0已全部通过。

## 12. 最终实现、完整P0与收尾（2026-11-12）

作者已完成两镜像配置、候选准备、新旧用户接口、SPAWN/EXEC传参、独立worker、构建/嵌入和boot统一准备。
正式DEMO/WORKER各有两个LOAD，RX分别3962/929字节，RW各4152字节，默认O0和一页代码断言保持。
原三题已逐字或按原代码排版保留，集中纠正程序编号/入口参数/任务身份、ELF来源与装载PA、运行快照与候选提交；不要求重答。

### 当前完整执行路径

1. boot从内核只读段取得两份ELF span，registry在局部全部解析通过后复制描述/布局，最后发布root。
   span借用永久字节，解析布局按值保存；两个boot USER通过同一prepare加载DEMO。
2. 新SPAWN12解释a0=program_id、a1=argument，prepare分配私有空间、按所选记录装载，随后四参数task_create_user发布新身份。
   新任务入口a0是argument，调用者a0是新公开ID；旧SPAWN8默认选择DEMO。
3. 新EXEC13在同Task中准备候选，失败清候选后回旧程序；成功才切内核根、释放旧空间、adopt、安装fresh frame、激活新根。
   fresh a0=argument，公开ID/parent/Task/Process/内核栈不变；旧EXEC10默认选择DEMO。
4. FORK继续复制当前全部用户内容页和寄存器值，不依赖registry重新装初始ELF；当前用户text/rodata也使用私有PA。
5. 退出先切根释放用户页，结果保留到父成功收集；reap回收记录不重复释放物理页。镜像表和内置ELF继续存活，供下一次创建使用。

### 既定P0结果

| 范围 | 实际结果 | 证据 |
| --- | --- | --- |
| 完整/增量构建、嵌入字节 | 无诊断/未定义符号；两个ELF分别链接，内核标签范围与原ELF完整逐字节相等；分别修改worker、DEMO、共享syscall头后三次正确重建 | `.cache/batch18-final-20261112/build-and-zero/results.json` |
| registry配置与编号边界 | 真实registry/解析器的13项宿主检查通过；坏第二ELF不提交/可重试、重复配置保持、ID2/MAX/0x100000001零MM拒绝且输出哨兵保持、描述/layout值复制及0/1选择正确 | `.cache/batch18-final-20261112/registry/results.json` |
| 正式正常链与资源 | 双WORKER，ID4/status23、ID5/status37、父ID3/status0；五份空间各7页，35次合法释放、运行21次分配/净恢复14页，三次reap零PMM；EXEC身份/完整fresh frame/a0=17保持；最后至少两S ticks | `.cache/batch18-qemu-p0-20261112-s1lrucp6/normal/results.json` |
| 指定WORKER的准备OOM/继续/重试 | EXEC(1,17)第一次第4次分配NULL；候选3PA精确释放根最后；旧7页字节/映射/frame/parent/satp/PMM保持；真实旧用户999/5678/EE继续；再真实重试正式WORKER seed123/BSS并退出37 | `.cache/batch18-qemu-p0-20261112-s1lrucp6/exec-failure/results.json` |
| 新接口编号0/参数0 | 真实12/13各一次原始a0=a1=0；fresh DEMO seed7/BSS零、退出收集0、父隔离及最后两S ticks，11项检查通过，最终保留原WORKER ELF | `.cache/batch18-final-20261112/build-and-zero/results.json` |
| 原宿主与500轮压力/旧ABI | make test通过（含2500宿主记录）；真实2000worker：spawn/fork各1000、exec/fault各125、满槽拒绝500；逐轮资源/旧ID/记录保持，运行alloc18375/free18382含根最后7页；旧8/10默认DEMO通过 | `.cache/b18-host-stress-20261112-2218/results.json`、`host-test.log` |

统一汇总：`.cache/batch18-final-20261112/summary.json`。验证所用82项正式源码/配置与当前工作区哈希一致。
正常流程的“35次释放”按各份空间的7页归属计数，允许页在不同时段被再次分配；不是声称35个PA地址从不重复使用。

P0期间协作者只维护持久压力脚本的boot配置锚点及测试README说明；硬件/故障/增量探针只改隔离副本。
产品算法未由本轮P0代改；没有clean、提交或推送。

### 验证限制与下一阶段

registry宿主检查使用真实函数体/解析器及MM替身，只证明配置、查找和输出边界；真实映射、选镜像、分配失败和旧程序继续由独立QEMU证据补足。
OOM只覆盖既定一个分配点，零参数使用隔离短DEMO驱动；500轮压力验证默认镜像，不声称穷举所有双镜像组合。
当前仍是单hart、固定五槽、立即复制、整数内置编号；未增加COW、多线程、路径或argv。
未引用的旧spawn.hpp残留历史声明，属于后续非功能清理，不是当前运行接口。

本批功能/P0/三题全部收尾，不追加P1/P2门槛。下一阶段按既定路线进入文件系统，具体目标与接口在下一批单独交付。
