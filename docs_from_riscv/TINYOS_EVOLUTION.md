# TinyOS 改造路线图

更新：2026-11-11。

本文件只维护路线、批次入口和仍然有效的限制。每个完整批次使用一份独立文档，目标、接口、修改、验证和问答都在对应文件中持续更新。

## 当前状态

2026-11-01作者亲写的C++最小静态对象运行时已通过正式构建/75个宿主组/正常QEMU，以及临时真实QEMU的两全局对象构造、二次init拒绝、局部析构、DSO筛选、全局逆序/重复finalize与entry受控返回验证。机制、证据和支持限制集中在[标准库与运行时主题](CPP_STANDARD_LIBRARY.md#当前插入试验静态对象生命周期2026-10-31)。随后已恢复并实现BATCH08；不将最小运行时成功等同于完整C++库、堆、异常或并发支持。

[BATCH06：多页数据/BSS 与有界用户页集合](tinyos/BATCH06_MULTI_PAGE_DATA.md)功能、P0验证及理解题归档均已完成。UserMemory改为有界页集合，数据/BSS可跨页，代码/栈仍各一页，共享root与静态调度保持。上一批[BATCH05](tinyos/BATCH05_ELF_LOADER.md)的布局配置和公开C入口继续使用。

BATCH06验证包括：完整构建无警告、70个具体宿主场景通过、默认跨页BSS运行及6页退出回收、重叠/容量零分配拒绝、分隔PA的跨页文件/BSS复制、第四flush失败完整回滚及第二数据页free失败重试。详情与证据只在BATCH06维护。

最近[BATCH07：安全写回用户内存](tinyos/BATCH07_COPY_TO_USER.md)功能、P0与三题归档均已完成。作者实现逐页V/R/W/U写回及GET_TIME接线，协作者补齐简化后的两次栈采样演示和必要验证：完整构建无警告、75个具体宿主场景、正式QEMU及临时3+5跨页/代码拒绝/六页回收均通过。三题集中澄清const页表与内容、失败返回-1和合法前缀、原始time/睡眠次数/字节数，以及已有映射为何无需分配或TLB刷新。证据和原答/修正版留在当批文档。

最近[BATCH08：独立用户根页表与显式地址空间生命周期](tinyos/BATCH08_PRIVATE_ADDRESS_SPACE.md)功能、全部P0及三题集中审查归档已完成。UserAddressSpace拥有私有root、借用内核根，内部保留UserMemory；82组宿主及真实QEMU验证同VA不同PA隔离、跨槽零分配拒绝、S/U/idle选根、用户timer、syscall私有root和退出7页恢复。三题集中澄清并列根分支、共享映射、活动根销毁及root-only重试。保持单用户正式演示、静态任务、单hart/ASID0，无fork/exec或堆；完整证据与限制只在当批维护。

最近[BATCH09：双用户任务与同址数据隔离](tinyos/BATCH09_TWO_USER_TASKS.md)功能、全部既定P0及三题集中审查归档完成。保留两S，同一ELF装入两份私有空间，初始a0传tag0/1，双U三轮保持同VA私有计数；82组宿主、真实双U切根/syscall/用户timer、首退7页且幸存者保持、再退7页和启动拒绝8页清理均通过。三题澄清0/1下标、a0传递与完整外层7页释放；完整证据与限制见当批。

最近[BATCH10：用户退出码与异常终止原因](tinyos/BATCH10_USER_EXIT_RESULT.md)功能、全部既定P0及三题集中审查归档完成。默认双0、main返回23、真实FAULT13与完整负long、初始化/分配清旧结果、无效入口零修改及7+7释放/结果保留均通过；82宿主及正式构建/双U日志通过。三题澄清用户ABI返回、reason区分与task_destroy保留结果；当批尚无wait/fork或槽复用，完整证据只见当批；当前复用进展见下一行BATCH11。

最近[BATCH11：收集用户退出结果与静态槽复用](tinyos/BATCH11_REAP_AND_REUSE.md)Process/REAP与槽复用功能验证通过：82宿主、正式双U收集、21次事务边界调用、同槽新ID4真实执行及FAULT/完整负long收集、7+7回收均通过。保持两S两U与单hart安全boot/trap阶段。sched_init全Process预检/全槽重置及原子拒绝、idle现场专项验证也通过；三题原答/集中审查/修正版已归档，本批收尾。

最近[BATCH12：用户请求创建内置镜像任务](tinyos/BATCH12_BUILTIN_SPAWN.md)功能、全部既定P0、执行路径梳理和原三题原答/集中审查均归档，本批收尾。完整构建/82宿主/实际SPAWN与收集、回滚/重试、现场/隔离/回收/8KiB栈观测通过；保持单hart固定槽，不扩展fork/exec或VFS。

最近[BATCH13：复制当前用户空间与执行现场的FORK](tinyos/BATCH13_EAGER_FORK.md)功能、全部既定P0及三题原答/集中审查已归档，本批收尾。正式父5/子0、运行快照/权限/页隔离/子31收集、槽满与L0分配失败回滚/重试、28次合法释放及8KiB栈观测通过；完整证据与限制只在本批维护。

最近[BATCH14：保持任务身份并替换内置用户镜像](tinyos/BATCH14_BUILTIN_EXEC.md)功能、全部既定P0与原三题原答/集中审查已归档，本批收尾。同Task/ID5替换空间与fresh frame，tag3退出37并由父按原ID收集；一次准备失败保留旧程序且实际重试成功，35次合法释放/8KiB栈观测通过，完整证据只在本批维护。

最近[BATCH15：父子关系、等待收集与孤儿收养](tinyos/BATCH15_PARENT_AND_WAIT.md)功能、全部既定P0、路径梳理及原三题原答/集中审查/修正版均归档，本批收尾。
Process记录父公开ID，boot登记U1/ID3为收养者，SPAWN/FORK设置父、EXEC保持；限父REAP与WAITPID11支持任意孩子，父退出转交活/DEAD孩子。五槽、真实非父拒绝/坏地址重试、孤儿转交与35次合法页面释放均验证通过；详细证据只在当批维护。

最近[BATCH16：提取固定任务表](tinyos/BATCH16_TASK_TABLE.md)功能/全部P0、追加500轮压力、路径梳理及三题原答/集中审查/修正版均已归档，本批收尾。
Task/同槽Process存储、ID/高水位与分配回收归TaskTable，调度与用户操作保持；宿主2500记录及真实QEMU2000子进程压力、原主线/35页回收、五槽孤儿及ID耗尽边界均通过。

最近[BATCH17：分离用户任务操作](tinyos/BATCH17_USER_TASK_OPERATIONS.md)功能/全部P0及三题原答/集中审查/修正版均归档，本批收尾。
用户创建/fork、exec、退出分别落在三个实现文件，显式依赖与原C接口保持；原正常35页资源链、500轮压力、活孤儿及EXEC准备失败/真实重试通过，不改变内置镜像配置。

TinyOS当前已具备独立用户根、分页多任务、运行期SPAWN、立即复制FORK、内置镜像EXEC、限父等待收集与固定根孤儿收养；阻塞等待与路径式execve尚未实现。不能按TinyOS批次编号或单个syscall直接换算rCore章节。

最近[BATCH18：不同内置ELF选择](tinyos/BATCH18_BUILTIN_PROGRAMS.md)功能/全部既定P0及三题原答/集中澄清/修正版均完成，本批收尾。
DEMO0/WORKER1独立ELF，一次性镜像表与准备模块，新12/13分离镜像编号/入口参数，旧8/10保持默认DEMO；
完整/增量构建、嵌入字节、registry13项、新编号0参数0、正常35页资源链、指定WORKER准备OOM/实际继续重试与原宿主/500轮压力均通过。详情和验证限制集中在当批。

当前[BATCH19：只读内置文件与按偏移读取](tinyos/BATCH19_BUILTIN_FILE_SYSTEM.md)完整任务已交付，核心待作者实现。
以完整文件名查找永久只读字节，提供无隐式位置的read_at；三文件包括原两个ELF及普通说明文本，boot从查找结果取得原镜像输入。
文件描述与字节生命周期、路径契约及既定P0已说明，目标/宿主接口准备通过；用户FD、路径执行入口、目录与磁盘后端继续分批推进。

rCore 第一至第四章的本轮学习已经收尾，当前以 TinyOS 为主；遇到具体概念缺口时再查阅 [rCore 章节总结](rcore/README.md)，不自动进入第五章。

## 已完成批次

编号属于 TinyOS 改造线，与 rCore 的 BATCH 编号分别维护。

| 批次 | 主题 | 结果 |
| --- | --- | --- |
| [BATCH01](tinyos/BATCH01_CPP_MODULE.md) | C++ 段复制模块接入 | 真实目标混合编译、C ABI、复制和补零通过 |
| [BATCH02](tinyos/BATCH02_PMM.md) | 物理页分配器封装 | 单一状态、LIFO 回收、清零、不可复制约束通过 |
| [BATCH03](tinyos/BATCH03_ADDRESS_PAGE_TABLE.md) | 地址、页号、PTE 与页表 | 类型检查和页表操作接入，原编解码及布局契约保持 |
| [BATCH04](tinyos/BATCH04_USER_MEMORY.md) | 用户内存准备、访问与释放 | 类型化复制、释放重试、循环化及 C 接口兼容通过 |
| [BATCH05](tinyos/BATCH05_ELF_LOADER.md) | ELF 装载模块与布局配置 | 策略与机制分开，启动接线及配置验证通过 |
| [BATCH06](tinyos/BATCH06_MULTI_PAGE_DATA.md) | 多页数据/BSS与有界页集合 | 功能、P0与三题原答/审查/修正版归档完成 |
| [BATCH07](tinyos/BATCH07_COPY_TO_USER.md) | 安全写回用户内存与GET_TIME | 功能、P0与三题原答/审查/修正版归档完成 |
| [BATCH08](tinyos/BATCH08_PRIVATE_ADDRESS_SPACE.md) | 独立用户root与显式生命周期 | 功能/P0及三题原答/集中审查/修正版归档完成 |
| [BATCH09](tinyos/BATCH09_TWO_USER_TASKS.md) | 双用户任务与同址数据隔离 | 功能/P0及三题原答/集中审查/修正版归档完成 |
| [BATCH10](tinyos/BATCH10_USER_EXIT_RESULT.md) | 用户退出码与异常终止原因 | 功能/P0及三题原答/集中审查/修正版归档完成 |
| [BATCH11](tinyos/BATCH11_REAP_AND_REUSE.md) | Process、退出结果收集与静态槽复用 | 功能/P0及三题原答/集中审查/修正版归档完成 |
| [BATCH12](tinyos/BATCH12_BUILTIN_SPAWN.md) | 用户请求创建内置ELF任务 | 功能/P0及三题原答/集中审查/修正版归档完成 |
| [BATCH13](tinyos/BATCH13_EAGER_FORK.md) | 立即复制用户空间与执行现场的FORK | 功能/P0及三题原答/集中审查/修正版归档完成 |
| [BATCH14](tinyos/BATCH14_BUILTIN_EXEC.md) | 同Task替换内置用户镜像的EXEC | 功能/P0及三题原答/集中审查归档完成 |
| [BATCH15](tinyos/BATCH15_PARENT_AND_WAIT.md) | 父子权限、WAITPID与固定根孤儿收养 | 功能/P0及三题原答/集中审查归档完成 |
| [BATCH16](tinyos/BATCH16_TASK_TABLE.md) | 固定任务表与稳定槽存储 | 功能/P0/500轮压力及三题原答/集中审查归档完成 |
| [BATCH17](tinyos/BATCH17_USER_TASK_OPERATIONS.md) | 用户任务资源事务分离 | 功能/P0及三题原答/集中审查归档完成 |
| [BATCH18](tinyos/BATCH18_BUILTIN_PROGRAMS.md) | 不同内置ELF选择与EXEC | 功能/P0及三题原答/集中审查归档完成 |

日常入口见 [批次索引](tinyos/README.md)。此前受限 ELF 功能本身的学习记录见 [ELF 学习总结](USER_ELF_LEARNING.md)；其历史任务留在 [USER_ELF_LOADING.md](USER_ELF_LOADING.md)。

标准库基础支持有独立主题文档 [CPP_STANDARD_LIBRARY.md](CPP_STANDARD_LIBRARY.md)，不把它误记为完整 STL 或完整 C++ 运行时已经接入。

## 当前模块边界

| 模块 | 负责什么 | 当前边界 |
| --- | --- | --- |
| boot | 选择镜像和演示布局、启动任务 | 布局策略留在启动侧 |
| user_loader | 校验、分配、复制、映射、刷新和失败清理 | 两个LOAD，代码/栈各一页，数据最多14页，完整范围限制用户根槽1 |
| UserAddressSpace | 私有根归属、模板借用和外层销毁 | 借用槽0/2、私有槽1，活动根拒绝释放，根free失败可重试 |
| UserMemory | 记录页归属并显式释放 | 借用root，最多16条内容页记录，支持部分释放重试 |
| user_access | 按用户映射和权限复制数据 | 读取逐页V/R/U，写回逐页V/R/W/U，后页失败可留下合法前缀；不改映射或归属 |
| Process与创建服务 | 空间/退出记录拥有关系，内置ELF创建和运行快照复制 | Process不持有Task栈或第二套PID；SPAWN/FORK最后转移空间并发布Task，部分复制失败保持归属待清理 |
| task/scheduler | 固定槽/公开ID、调度状态/现场、终止与收集编排 | 五槽静态8KiB内核栈，可运行期创建与复用，限父收集与根收养；进程内部页操作交给Process/MM |
| Sv39PageTable 与地址/PTE 类型 | 构表、查询、解除映射和空表回收 | 4 KiB 叶；表页仍为 512 个原始条目 |
| PMM | 分配、清零和归还物理页 | 单 hart、固定可分配区间 |
| RISC-V 架构层 | satp、TLB、trap 等硬件操作 | 与可在宿主验证的软件构表分开 |

MMU 是 CPU 硬件。软件内存子系统按分配器、页表、用户内存和架构操作划分职责，不把所有状态集中到一个庞大的 MMU 类中。

## 仍然有效的限制

- 正式演示由boot创建两个用户任务，U1依次SPAWN/wait与FORK/EXEC/wait；各有私有root、借用共同内核分支。五槽静态池，正式主线使用四槽。
- ELF装载保留受限两段，代码/栈各一页，数据/BSS最多14页；布局配置不执行ELF重定位，没有堆或动态区域。
- 页表物理地址的直接访问依赖现有恒等映射；仅有单 hart 的本地 TLB 刷新。
- UserMemory 的显式释放支持部分失败后重试。prepare/loader 的回滚自身再失败时，仍有历史局部资源归属限制，不能宣称任意故障都能完全回滚。
- PMM 仍以内核末尾和 DTB 地址保守划界，未解析 DTB 的实际 RAM 节点；没有多核锁。
- 调度器仍使用静态任务槽和8KiB内核栈；运行期创建、退出、限父收集与复用已具备，没有动态分配Task/内核栈或阻塞wait。
- C++已有自写基础头和通过验证的最小全局构造/受控终止运行时；仍不等于完整STL、堆、异常、TLS或并发支持。
- 旧 QEMU 注入脚本可能还引用旧路径、函数签名或页数。按当批需要适配，不将完整历史矩阵自动列为推进前提。
- 独立语言实验仍在 [experiments/cpp-rust](../experiments/cpp-rust/README.md)；其旧构建路径复用前需重新核对。

## 后续方向

BATCH11至BATCH18均已完成实现、既定P0与三题收尾；当前BATCH19文件系统第一批完整任务已交付，尚未实现。

2026-11-11作者在B15收尾后提出现在开始拆sched，调整11月9日“多内置ELF后再拆”的顺序。
本阶段安排如下；核心仍由作者实现，协作者集中审查并承担既定P0：

1. BATCH15父子关系、限父收集、wait-any与孤儿收养已完成。
2. BATCH16提取TaskTable，BATCH17分离用户任务操作，均已完成。
3. BATCH18不同内置ELF的选择与EXEC已完成，程序替换可选择两份独立镜像，旧ABI保持。
4. 当前进程管理阶段已收尾；BATCH19先做只读内置文件/查找/read_at并接回启动数据源，之后分批补路径入口、打开文件状态和磁盘后端。阻塞wait队列、COW、多线程与动态内核栈不是前置。

此顺序以TinyOS当前进程管理能力为范围，不等于逐项复刻rCore第五章所有应用或练习。
以下更远方向仍是建议，不同时开工：

| 方向 | 需要先决定的内容 |
| --- | --- |
| 用户地址空间与区域归属 | 支持的区域数量、页归属、显式转移和释放失败重试 |
| 扩展 ELF 布局 | 多页段、权限、区间与边界策略；不要把配置变化当作重定位 |
| 更多独立用户程序 | B09之后再决定不同ELF、更多用户及进程管理范围 |
| 任务与调度的 C++ 组织 | 静态/动态任务边界、状态迁移、栈和现场布局 |
| 物理内存布局完善 | DTB 内存节点解析、保留区与分配范围 |

这些方向不同时开工。完整功能及有学习价值的测试由作者主导，协作者负责解释、集中审查与例行复验。

远期资料发布意向（2026-10-30）：等项目达到约相当于rCore第八章的阶段，再考虑借用GitHub搭建文档网站，整理学习/实现过程、设计取舍与注意点。当前先持续积累批次记录和验证材料，网站方案与发布范围到时再定；此意向不启动搭站或改变现有学习路线。

## 跨语言与资源约定

- C/汇编调用入口保留明确的 C ABI；C++ 类型、模板和成员声明放在 C 链接块外。
- C 与 C++ 分别编译，ISA、ABI、代码模型和链接选项统一；禁止异常或 RTTI 不等于自动具备完整运行时。
- 静态实例使用明确初始化，不引入依赖尚未实现的启动/退出运行时。
- 外部输入显式检查；普通原始地址构造和指针转换不执行地址翻译。
- 资源归属及转移必须清楚，释放失败保留剩余资源信息；不以析构吞错误或提前清空描述。
- 实现需要维护硬件或 C/汇编布局时，继续用静态断言核对。

## 每批文档怎么维护

新批次使用 `docs_from_riscv/tinyos/BATCHnn_TOPIC.md`。在同一份文件中维护：

1. 目标、范围、接口和核心不变量。
2. 实现选择、审查问题与最终修正。
3. 已验证结果、证据目录和已知限制。
4. 功能梳理、理解题原答、审查与修正版。

原任务与历史错误应标明时点，避免旧“待修”覆盖最新验收结果。完成一批后只更新索引和本文件的状态摘要；过程细节继续写入所属批次，不重新堆回总路线图。

## 历史基线

2026-10-22：TinyOS 构建及 61 个具体宿主场景通过（另有汇总 PASS），但 QEMU 在启用 Sv39 后仍停在 ELF 准备失败；当时独立 C++/Rust 实验只完成宿主验证。该阶段问题后来已修复，证据见仓库根 `.cache/code-audit/2026-10-22/`，不能用这段历史描述当前运行状态。

官方工具链参考沿用原记录：[GCC freestanding](https://gcc.gnu.org/onlinedocs/gcc/Standards.html)、[GCC C++ 选项](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html)。
