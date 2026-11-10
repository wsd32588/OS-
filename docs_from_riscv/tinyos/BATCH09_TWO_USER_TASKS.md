# BATCH09：双用户任务与同址数据隔离

状态：功能、全部既定P0及三题原答/集中审查/修正版归档完成，整批收尾。日期：2026-11-04。

导航：[批次索引](README.md) · [上一批](BATCH08_PRIVATE_ADDRESS_SPACE.md) · [总路线图](../TINYOS_EVOLUTION.md)

## 目标和可观察结果

把同一份用户ELF独立装入两个私有地址空间，让两个U任务真实交替运行。两个程序的入口、栈顶、全局变量VA相同，但物理页与运行状态独立；一个用户退出后，另一个继续正常运行。

沿用B08的所有权和选根机制。本批新增的连接是：boot给每个用户传入一个初始参数，任务初始frame的a0把它交给用户C函数。用户用0/1标记区分日志和自己的计数器预期值。

默认演示保留两个S任务A/B，增加到两个U任务U0/U1，普通静态槽数由3变4，idle仍是单独对象。预期能看到各自完整的消息：

```text
[U0] start
[U1] start
[U0] counter OK
[U1] counter OK
...
[U0] done
[U1] done
```

两种tag的start/done各一次、counter OK各三次；不要求两者全局输出严格轮流，也不规定哪个先退出。每个tag内部必须保持start→三轮成功→done。原Time OK/Hello from user/Data OK演示各执行两次，两次用户exit后S-mode tick继续。

## 本批边界

- 仍只有一份用户源码、一份ELF、同一套VA布局。两次独立create/load，所有用户内容页仍分别分配；不做代码页共享或COW。
- 不增加fork/exec/wait、动态任务槽、task_destroy、堆、ASID优化或多hart。本批是两个独立装载的静态用户任务，不是父子进程复制。
- 不重写调度器或地址空间类；用户程序继续使用当前C接口和已有syscall，不引入用户态C++标准库、构造运行时、printf或新syscall。
- 现有buffer初始与末尾检查保留，不在每次调度后追加全buffer或全内存检查。新增演示用一个已有全局计数器验证私有状态。
- 沿用正常路径、原82个宿主组及原有失败重试规则。必要复验由协作者承担，作者不需要新增测试矩阵。

## 已核对的起点和现成接口

- UserAddressSpace的create/release、loader槽1限制和调度S/U/idle选根已在B08通过真实验证。
- task_create_user已经接管完整外层对象，成功清源、失败保留；frame中已有a0字段，task_prepare会清零它。
- user/start.S目前的第一条操作是`call main`，不会改写a0。无需为一个参数写新runtime.S或增加汇编保存/恢复。
- 两次调用user_memory_load_elf使用同一份user_elf_start、elf_size、UserElfLayout和UserMemoryLayout即可；输出必须是两个不同的UserMemory成员。
- 当前代码、栈各一页，数据/BSS两页；单用户内容共4页，连同根和用户L1/L0共7页。继续保留现有大buffer和两页数据布局。

## 接口：用初始a0传入用户参数

在include/task/sched.h和kernel/task/sched.c同步修改签名，新增最后一个参数：

```c
int task_create_user(
    uintptr_t entry,
    uintptr_t user_stack_top,
    UserAddressSpace* space,
    uintptr_t user_argument
);
```

保留原有参数/归属检查及槽位失败处理，在准备初始frame时增加：

```c
task->frame->a0 = (unsigned long)user_argument;
```

全部创建操作成功后仍转移完整外层，来源整体清空：

```c
task->user_space = *space;
*space = (UserAddressSpace){0};
```

user_argument是通用用户入口参数，不在调度器中限制为0/1；本批boot选择0/1，用户演示自行拒绝其他tag。它不是调度器task_id，也不是需要解引用的指针。两个S先创建时，用户返回的task_id通常是2/3，而tag明确是0/1。

用户入口改为：

```c
int main(unsigned long user_tag) {
    /* 用user_tag选择本任务的日志和计数器预期值。 */
}
```

这是由自写_start调用的freestanding入口参数约定，没有argc/argv。当前目标GCC在-ffreestanding下已做该签名的独立语法检查，未产生诊断；不代表本批整体实现已完成。user/start.S保留现状即可，main返回后仍由已有user_exit退出。

进入C函数后使用user_tag这个C参数，不要假设后续syscall仍会在a0里保留启动tag；a0也是参数与返回值寄存器。

## boot：准备两份空间，再逐个转移

boot_start_user中使用两个分别初始化的对象，例如：

```cpp
UserAddressSpace spaces[2]{};
```

完整流程保持明确：

1. 校验并初始化调度器，准备两个S任务。错误立即处理，不把失败的task_id当成功。
2. 对每个spaces[i]分别create，再以spaces[i].root装载同一ELF，输出写入&spaces[i].user_memory。先把两份空间准备好，再开始用户任务转移。
3. 分别创建U0/U1，将i作为最后一个参数。相同entry和stack_top可以复用，它们通过不同根翻译。
4. 每次成功后对应spaces[i]整体为空，资源由任务拥有。检查返回值，两个用户都创建成功后才进入原boot_start_scheduler。

MAX_TASKS改为4，不把idle计入普通槽数。新增a0赋值即可，原切表辅助函数和三处接线应能直接支持第二个用户，无须按tag写切表分支。

### 失败时谁清理

统一清理仍由boot调用现有user_address_space_release，适合抽一个本文件内的pending-spaces清理函数；对数组中尚未转移的空间逐个清理并检查结果。完整空对象重复释放成功，内层失败或根free失败时保留剩余描述并报告停机。

| 失败时点 | boot仍然拥有什么 | 处理 |
| --- | --- | --- |
| 任一个create/load失败，尚未创建用户任务 | 数组中所有已准备的对象 | 清理全部局部对象，再停机 |
| 用户任务创建失败，对应来源未转移 | 该来源及其他尚未转移对象 | 清理这些局部对象，再停机 |
| U0已转移、U1创建失败 | U0来源为空；U1仍在局部数组 | 只清未转移来源，U0空间仍由任务拥有；不启动调度 |

不复制一个已装载space当作第二个拥有者，不通过保存旧指针释放已交给任务的空间。当前没有取消已创建任务的API，本批不承诺“任意启动失败均自动回滚全部14页”。不为这个分支增加task_destroy；已转移对象保持由任务持有，系统停在失败路径。

## 用户演示：同一全局VA，各自的预期值

保留原静态counter初值7和现有buffer。两次独立装载后，两者起初都应看到7以及各自零初始化的BSS。用户tag只用于选择日志和私有计数器的预期值，不修改内核描述。

建议用下列演示规则，使验收数字明确：

1. main先确认tag为0或1，输出对应start整行。
2. 两者各自确认原初值，然后把counter设置为`100 + user_tag * 100`，即100/200。预期值保存在局部变量中。
3. 做三轮：各自增加counter和局部预期值，调用已有user_yield，恢复后确认counter仍等于自己的预期值，输出对应counter OK整行。计数分别为101/102/103与201/202/203。
4. 沿用原sleep(2)、GET_TIME前后比较和末尾buffer检查；睡眠后额外确认counter仍为自己的最终预期值。原来硬编码counter==8的检查同步改为局部预期值，避免误报。
5. 原Time OK/Hello/Data OK成功后输出对应done整行，正常返回。任何错误沿用已有fail→user_exit路径，不增加错误格式化框架。

日志用静态完整字符串，例如`[U0] counter OK\n`与`[U1] counter OK\n`，每行一次user_write并检查长度。不要分成一次prefix写入加一次正文写入：两次syscall之间可以调度，前缀可能与另一个任务的正文交错。所有消息仍小于SYSCALL_WRITE_MAX，不需要strlen或printf。

注意：两者GET_TIME读取同一硬件时钟，隔离的是各自保存结果的用户内存，不是两台独立的时钟。代码/全局变量VA相同是本批的预期行为，不通过给U1改链接地址回避隔离验证。

## 文件范围

| 文件 | 作者本批修改 |
| --- | --- |
| include/task/sched.h | task_create_user增加入口参数 |
| kernel/task/sched.c | MAX_TASKS=4、写初始frame->a0，保留原归属和调度逻辑 |
| kernel/boot/private/boot_stages.cpp | 两份独立prepare/load、tag传入、逐次转移及未转移对象清理 |
| user/main.c | 接收tag、三轮独立计数与完整日志，保留原时间/数据演示 |
| user/start.S | 核对已经保留a0，通常无需修改 |
| user/user.ld、Makefile、mm各模块 | 继续复用单ELF、现有页布局与接口，通常无需修改 |
| test及临时QEMU探针 | 协作者必要适配和例行复验 |

sched.c仍为C，boot_stages.cpp为C++：来源清空的写法分别沿用(UserAddressSpace){0}和{}，头/定义/调用的第四参数一起修改。检索所有真实调用可用`rg -n 'task_create_user\(' include kernel`。旧文档保留历史三参数接口，不批量替换历史记录；旧QEMU脚本有旧路径/页数/调用假设，按本批所需最小适配。

## 验收和分工

作者完成上述核心及普通make/qemu观察后集中检查。P0由协作者保证；P1/P2均是选做，不因未做选项延迟下一批。

| 等级 | 场景与预期 | 学习点/分工 |
| --- | --- | --- |
| P0 | make -B无新警告、最终无未定义；原82个宿主组通过；用户ELF仍一页代码、四内容页，所有调用签名一致 | 协作者例行验证，无新增作者宿主作业 |
| P0 | 默认QEMU收到tag0/1；start/done各1次、counter OK各3次，原时间/数据消息各2次；每个tag内顺序正确、2次exit后至少2次S tick | 作者普通观察；协作者解析完整日志，不规定全局交错顺序 |
| P0 | 临时真实探针确认两root和全部私有内容PA不同、相同VA翻译不同PA；普通syscall持对应用户root；真实U0→U1切表正确，timer/yield/sleep后状态保持 | 协作者临时记录；为覆盖真实U timer可以仅在副本延长用户计算，作者不必加入忙等或计时测试框架 |
| P0 | 双用户准备完成后私有资源14页；首个退出先回内核根、只归还自己7页，另一用户映射/内容和拥有描述保持且继续运行；第二个再归还7页，模板/共享表保持，任务拥有对象空 | 协作者记录free前顺序和页计数，不在free后解引用旧根，不加全量buffer工作 |
| P0 | 一条启动拒绝：首空间已装载7页、第二空间只有根1页时，使第二次loader入口非法；该次内容alloc为0，清理8页恢复到准备前基线、模板保持、未启动任务 | 协作者临时副本故障注入；作者无需写新的失败矩阵 |
| P1 | 第一个已转移而第二个任务创建失败时，失败来源保持并可释放，已转移任务仍拥有其资源，boot停机不误free | 部分转移与局部清理，可只讨论思路；不增加取消任务API |
| P2 | 两份不同ELF、第三个用户、COW、并发页表保护、全套旧脚本适配 | 后续方向，本批不作为前置 |

常用命令：

```sh
make -j4
timeout 16s make qemu
```

限时QEMU返回124通常是观察结束，不表示用户程序失败；结合标记、exit和后续tick判断。协作者收尾时运行make -B -j4、make test并核对目标符号/布局；不clean/stage/commit/push。

## 功能梳理与理解收尾

实际路径为“一份镜像→两次独立装载→两个入口参数→两份运行状态→按根翻译相同VA→各自退出回收”：

1. boot分别为spaces[0]、spaces[1]创建私有根并装载同一ELF。两个用户的入口、栈及counter VA相同，各自的物理内容页和用户下级表不同；共同借用内核根模板的槽0/2映射。
2. task_create_user接管完整UserAddressSpace，清空对应boot来源，初始frame->a0分别写0/1。恢复寄存器进入U态_start后，call main保留a0，C入口接收user_tag。tag用来选择日志和计数起点，实际隔离来自页表映射。
3. 调度器选择任务时激活相应root，counter在两份空间中分别从100/200递增到103/203。syscall进入S态本身不自动切satp，借用的内核映射使处理函数和内核栈在当前用户root下可用；当前root getter把该任务的内层root交给用户复制接口。
4. U0退出时先激活持续有效的kernel_root，再释放U0完整空间：四内容页、两用户下级表和一私有根共7页。内核根及共享表、U1的7页保持。U1之后继续运行，最终同样归还自己的7页；静态任务槽和内核栈仍保留。

最初给出的三题如下；实际聊天收尾聚焦同VA隔离、入口a0和正常退出，扩展的部分转移情形由协作者在修正版中补充，不追加重答：

1. 同一份ELF、相同的counter和栈VA，为什么两个用户能保存不同值？相同镜像与共享可写页有什么区别？
2. user_tag如何从boot经过初始frame与_start进入C？为什么不能把a0永久当作tag寄存器，tag又为什么不等于task_id？
3. 一个用户已经退出，另一个还在运行时，内核根、两份空间和页计数分别是什么状态？部分创建成功后boot清理谁？

### 作者原答（2026-11-04）

以下保留作者本轮合并原文，不修正拼写或数组下标：

> 因为使用的是两个空间，spcaes[1/2]，在不同的任务空间里，相同的counter虚拟地址对应的位置不同
> user_tag作为a0的参数值传入main, a0还要被返回值使用
> 切回内核根和之前一样未来防止自己破坏掉page导致page fault, 释放的只是U0自己的user_memory

### 集中审查与修正版

第一题核心正确：两个root把同一个counter VA映射到不同物理页，因此能保存不同的值。代码中的数组下标应为spaces[0]、spaces[1]，不是1/2；成功创建任务后这两个boot来源已清空，真正的拥有对象在任务内部。tag0/1是入口数据，不负责建立隔离；同一ELF被复制装载两份，也不等于共享一份可写物理页。

第二题核心正确，补齐路径：boot的i→task_create_user最后参数→初始frame->a0→上下文恢复a0→用户_start调用main→C参数user_tag。a0是ABI参数/返回值寄存器，之后的syscall会用它传指针、字符或sleep次数，并写回返回值，所以启动tag需要作为C参数使用，不能期待寄存器永远保留它。tag0/1也不是普通任务槽编号：本演示先建两S，两个用户task_id为2/3。

第三题的安全方向正确，但“只释放user_memory”少了外层。sched_on_exit调用user_address_space_release释放完整UserAddressSpace：内层解除用户映射、刷新、释放四内容页并回收两用户下级表；外层最后释放U0的私有根，共7页。切回内核根是为了后续执行不再依赖正在撤销/回收的页表，不能把结果说成必然立刻page fault，已有TLB可能暂时掩盖错误。kernel_root和借用共享表持续存活，U1拥有另一套私有根/下级表/内容页，因此释放U0不改变U1；首退归还7页，次退再归还7页，均相对用户准备前的PMM基线计算。释放结束后调度器会按下一任务重新选择root，不会一直留在内核根。

补充原任务的部分转移情形：若U0已交给任务而U1创建失败，boot只清仍由局部数组拥有的未转移空间；已清空的U0来源不会释放任务内部资源。本批无取消任务API，失败后停机，不承诺该情形自动回滚全部14页。这段是协作者解释补充，不记作作者已经回答。

本批三题已集中审查并归档，不要求反复重答。上述修正版为协作者整理，未冒充作者再次复述。此次仅核对源码/维护文档，沿用已保存的P0证据，未修改产品或重跑测试，不自动开启下一批。

## boot草稿检查与局部修正（2026-11-04）

作者先实现4参数task_create_user、frame->a0、MAX_TASKS=4与用户main参数，再请求检查boot_start_user。首次草稿只create spaces[1]、两次固定向同根装载，另有spaces[2]越界、只清当前来源、清理循环内提前boot_fail及失败后继续创建问题。随后作者已将loader改为spaces[i].root并改部分失败清理，但仍只创建一张根、用户创建失败清理后继续执行，is_ok始终false。

经作者本轮求助，协作者仅在boot_stages.cpp作局部修正：

- 抽release_pending_user_spaces，统一逐个release并检查结果；来源为空时沿用幂等成功。错误索引通过uart_put_hex显示，避免uart_putchar(0/1)发送不可见控制字符。
- 将sched_init与两S任务创建放在用户资源准备前，逐次查错、立即停机。
- 每轮create(&spaces[i])后，用spaces[i].root装载并写入其user_memory；任一失败清理全部尚未转移的来源。
- 两份准备完成后再进入任务创建循环，tag传i。创建失败清理未转移来源后立即boot_fail，不继续下一轮、不用聚合is_ok；已成功转移的来源为空，任务拥有资源不受局部清理影响。

此次验证仅对应boot修复：

1. 修前构建成功，但正常QEMU停在Demo user memory load failed且未进入调度，确认错误可复现。
2. 修后make -j4成功，正式默认QEMU Time OK/Hello from user/Data OK/exit各2次，末次exit后10次S tick，无fail/fault；124是16秒观察结束。此时用户程序虽接收user_tag，仍执行原counter=8逻辑，尚无本批的tag0/1和三轮计数证明。
3. 临时真实QEMU用wrapper在第二次正式loader调用中，仅将复制ELF布局的entry设为data_address，返回INVALID_ARGUMENT且该次内容alloc为0。正式统一cleanup回收第一空间7页+第二根1页，共8个拥有页各free一次，两来源清空、页数恢复，内核模板/全部共享根及下级表哈希/satp保持。task_create_user与scheduler_start均0调用，停在预期失败循环。

证据.cache/batch09-boot-fix-93y2acnn/：before/after构建与默认QEMU日志、结果JSON；qemu/下run_probe.py、probe.cpp及results.json可重跑。临时探针5秒后由runner SIGTERM停止，61正式源配置哈希保持。完整副本构建如实保留现有user_tag未使用警告，无新增诊断或未定义；不代改尚待作者实现的用户演示。

本轮未修改sched、用户程序或正式测试；整批B09的双tag/三轮计数、真实双U切换及14页回收仍待作者完成演示后集中验收。未clean/stage/commit/push。

## 用户演示草稿检查（2026-11-04）

作者补了fail(reason)、100/200起点和三轮计数后请求检查。只读审查发现：用户ELF引用内核uart_puts/uart_put_hex；counter_message仅构造未调用user_write；三轮循环误包含R/GET_TIME/sleep/X；原counter初值7与sleep后最终预期值检查被删除。main的tag上界检查已存在。应通过用户syscall一次输出完整start/counter OK/done行并检查长度，三轮仅包计数/yield/恢复后检查与日志，时间演示置于循环之后并恢复睡眠后的计数检查。

实际make user -j4复现counter_message set-but-not-used警告和UART符号未定义，用户ELF链接失败；未执行QEMU，未修改正式产品。该结果是草稿诊断，不能视为B09验收完成。

## 双tag正常演示与集中复验（2026-11-04）

作者已移除用户UART引用，通过user_write整行输出start/counter OK/end，三轮循环收窄到计数/yield/恢复后检查，时间与sleep置于循环外，并恢复counter初值7和sleep后最终预期值检查。end与任务原文done表示同一完成事件，不要求改名。当前遗漏为main开头的user_tag>1拒绝，已集中反馈；正式boot只传0/1，正常演示不会暴露该遗漏。

协作者执行make -B -j4无警告，kernel nm -u为空；user ELF入口0x40000000，RX段0x752字节，RW filesz0x11/memsz0x1038，仍为一页代码、一页栈和两页数据/BSS。make test的82个具体宿主组及汇总均通过。

正式默认20秒QEMU：U0/U1的start/end各一次，counter OK各三次且各自顺序正确，yield六次，sleep两次，Time OK/Hello from user/Data OK/exit各两次；末次exit后12个S-mode tick，无失败/故障。退出码124为timeout结束观察。证据.cache/batch09-verification-__jztirh/的build.log、build-host-results.json、host-details.log、default-qemu.log和default-results.json；截至该次检查，51个选定正式源配置哈希保持，未改产品。

独立重跑最新正式源码的第二loader拒绝探针：第一空间7页+第二根1页，第二loader以非法入口返回INVALID_ARGUMENT且0次内容分配；正式清理8个拥有页各一次，两来源空、PMM基线恢复、全部共享表哈希与satp保持；task_create_user/scheduler_start均0次。副本完整构建无警告、无未定义符号，61正式源配置哈希保持。证据.cache/batch09-negative-tehgzhrb/含可重跑runner/probe/results.json；5秒runner SIGTERM停止预期boot_fail循环，不是客体自行关机。

独立真实运行探针也已完成，证据.cache/batch09-runtime-p4kr4amp/含run_probe.py/probe.cpp/probe.h/build.log/qemu.log/results.json：

- 两份空间共14个私有物理页互不相同，四个相同VA分别查询到不同PA，初始counter均7，完整转移后来源空。
- 实际U0→U1切根5次，两个U各选择6次、各1次真实U timer、各3次yield及1次sleep；真实syscall保持对应用户root，逐轮counter分别101/102/103与201/202/203。
- U0先退出：release前CSR为内核根，先释放内容和用户下级表，最后在槽1为空时释放根，共7页各一次。U1完整拥有描述、全部内容页和私有表在此释放前后哈希保持，之后继续执行9个真实syscall。
- U1再退出回收7页，两个拥有描述空，PMM恢复14页，共享表哈希保持；其后2个idle timer正常。两tag正常日志和原时间/数据检查仍全部通过。

副本完整构建无警告、内核无未定义，61正式源配置哈希保持。仅在临时副本使A有限，并在每U原计数循环之前增加约1.5计时周期的GET_TIME计算以暴露真实U timer和idle；原用户检查全部保留。14秒runner SIGTERM结束观察，不是客体自行关机；只观察释放前根，不解引用已释放根。没有新增作者测试作业或正式产品修改。

本次复验期间作者已恢复入口user_tag>1拒绝。与上面完成探针时的版本相比，仅main增加这一前置拒绝（证据author-tag-guard.diff）；协作者对最新代码再作正常构建/QEMU回归，不重复扩展矩阵。此前“guard待恢复”是审查时状态，不能当作当前仍遗漏。

最终增量构建无警告，最新正式16秒QEMU两tag start/end各一次、counter OK各三次且顺序正确，原成功消息/exit各两次、yield六次/sleep两次，末次退出后8个S tick，无失败/故障。证据同目录tag-guard-build.log、tag-guard-qemu.log和tag-guard-results.json；124为timeout。功能及全部既定P0通过，原三题尚无作者原答，下一步理解收尾，不自动开启下一批。未clean/stage/commit/push；协作者本轮只写记录和临时探针，未代改正式产品。

## 准备记录

2026-11-04：核对B08完成记录、当前boot/sched/loader/单ELF/用户入口和已有调用；内部独立只读审查支持本范围，提前明确a0现成入口和部分转移失败所有权。只交付完整任务与索引，未修改正式产品、运行双用户QEMU或宣称本批已实现。仅用目标GCC -ffreestanding对带unsigned long参数的main做语法检查，返回0无诊断。证据.cache/batch09-task-preparation-mywvbyof/；既有实现与验证仍以B08为准。
