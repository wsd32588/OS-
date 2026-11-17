# TinyOS 改造批次索引

更新：2026-11-17。这里的编号只用于 TinyOS 改造线，独立于 [rCore 学习批次](../../experiments/rcore-learning/README.md)。

每个完整批次只使用一份文档，在其中持续维护任务、接口、修改记录、验证和问答。总路线见 [TINYOS_EVOLUTION.md](../TINYOS_EVOLUTION.md)。

## 批次总览

| 批次 | 实施时间 | 主题 |
| --- | --- | --- |
| [BATCH01](BATCH01_CPP_MODULE.md) | 2026-10-26 | 让段复制模块真正以 C++ 进入内核 |
| [BATCH02](BATCH02_PMM.md) | 2026-10-26 | PhysicalPageAllocator 封装 PMM 状态 |
| [BATCH03](BATCH03_ADDRESS_PAGE_TABLE.md) | 2026-10-27—28 | 地址、页号、PTE 与页表接入 |
| [BATCH04](BATCH04_USER_MEMORY.md) | 2026-10-28 | 用户内存准备、访问、释放及 C 接口兼容 |
| [BATCH05](BATCH05_ELF_LOADER.md) | 2026-10-28—29 | ELF 装载模块、布局配置及启动接线 |
| [BATCH06](BATCH06_MULTI_PAGE_DATA.md) | 2026-10-29—30 | 多页数据/BSS与有界页集合，功能和P0通过 |
| [BATCH07](BATCH07_COPY_TO_USER.md) | 2026-10-30 | 安全写回用户内存与GET_TIME，功能/P0及三题归档完成 |
| [BATCH08](BATCH08_PRIVATE_ADDRESS_SPACE.md) | 2026-10-30—11-04 | 独立用户根页表，功能/P0及三题集中审查归档完成 |
| [BATCH09](BATCH09_TWO_USER_TASKS.md) | 2026-11-04 | 双用户任务与同址数据隔离，功能/P0及三题集中审查归档完成 |
| [BATCH10](BATCH10_USER_EXIT_RESULT.md) | 2026-11-04—05 | 用户退出结果，功能/P0及三题集中审查归档完成 |
| [BATCH11](BATCH11_REAP_AND_REUSE.md) | 2026-11-05—07 | Process、结果收集与槽复用，功能/P0及三题集中审查归档完成 |
| [BATCH12](BATCH12_BUILTIN_SPAWN.md) | 2026-11-07 | 运行期创建内置镜像任务，功能/P0及三题归档完成 |
| [BATCH13](BATCH13_EAGER_FORK.md) | 2026-11-08 | 立即复制用户空间与现场的FORK，功能/P0及三题归档完成 |
| [BATCH14](BATCH14_BUILTIN_EXEC.md) | 2026-11-09 | 同任务替换内置镜像的EXEC，功能/P0及三题归档完成 |
| [BATCH15](BATCH15_PARENT_AND_WAIT.md) | 2026-11-09—11 | 父子关系、限父等待与固定根孤儿收养，功能/P0及三题归档完成 |
| [BATCH16](BATCH16_TASK_TABLE.md) | 2026-11-11 | 固定任务表提取，功能/P0/500轮压力及三题归档完成 |
| [BATCH17](BATCH17_USER_TASK_OPERATIONS.md) | 2026-11-11 | 用户任务创建/替换/退出事务分离，功能/P0及三题归档完成 |
| [BATCH18](BATCH18_BUILTIN_PROGRAMS.md) | 2026-11-11—12 | 两份内置ELF选择，功能/P0及三题归档完成 |
| [BATCH19](BATCH19_BUILTIN_FILE_SYSTEM.md) | 2026-11-12—13 | 只读内置文件与按偏移读取，功能/P0及三题归档完成 |
| [BATCH20](BATCH20_USER_FILE_READ.md) | 2026-11-13—14 | 用户按路径显式偏移读取，功能/P0及三题归档完成 |
| [BATCH21](BATCH21_OPEN_FILE_MODEL.md) | 2026-11-14—15 | 内核打开文件对象与FD表，功能/P0及三题归档完成 |
| [BATCH22](BATCH22_PROCESS_FILE_DESCRIPTORS.md) | 2026-11-15 | 进程FD与用户open/read/close，功能/P0及三题归档完成 |
| [BATCH23](BATCH23_UART_POLLING_INPUT.md) | 2026-11-15 | UART轮询接收与内核任务回显，功能/P0及三题归档完成 |
| [BATCH24](BATCH24_KERNEL_FILE_CONSOLE.md) | 2026-11-15—16 | 内核cat/hex文件查看工具，功能/P0及三题归档完成 |
| [BATCH25](BATCH25_CONSOLE_INPUT_QUEUE.md) | 2026-11-16 | 控制台原始输入队列，功能/P0及三题归档完成 |
| [BATCH26](BATCH26_CONSOLE_DEVICE_SERVICE.md) | 2026-11-16 | 长期控制台设备与服务，功能/P0及三题归档完成 |
| [BATCH27](BATCH27_CONSOLE_FILE_DESCRIPTORS.md) | 2026-11-16—17 | 控制台FD与用户读写，功能/P0及三题归档完成 |
| [BATCH28](BATCH28_STANDARD_STREAMS.md) | 2026-11-17 | 标准0/1/2与fresh新进程，功能/P0及三题归档完成 |
| [BATCH29](BATCH29_PATH_EXEC.md) | 2026-11-17起 | 按路径执行内置程序，完整任务已交付，待作者实现 |

最近完整归档 **BATCH28**：标准0/1/2与fresh新进程功能、全部既定P0及三题集中澄清已收尾。

## 当前批次与近期归档

[BATCH29：按路径执行内置程序](BATCH29_PATH_EXEC.md)——完整任务已交付，待作者实现。
新EXEC_PATH19安全复制路径→精确lookup→永久文件记录身份映射程序编号→复用原exec；成功保持Task/ID/FD，失败旧程序继续。
main新增e执行/bin/worker，worker通过保留的stdout输出；不叠加shell/argv/路径SPAWN。接口/不变量/固定P0/三题见当批。

[BATCH28：标准输入输出与新进程资源](BATCH28_STANDARD_STREAMS.md)——功能、全部既定P0与三题原答/集中澄清归档完成，本批收尾，见第14—16节。
user模式fresh创建三项独立Console打开对象0只读/1和2只写；SPAWN全新资源，不继承父FD；fork共享/exec保留/exit关闭。
权限检查先于poll/复制，标准流在allocate前准备、失败撤销；kdbg创建保持空表。137宿主/3CTest、user19/默认36/生命周期21项QEMU与旧27项进程回归通过，108源保持；核心由作者实现。

[BATCH27：通过FD访问控制台](BATCH27_CONSOLE_FILE_DESCRIPTORS.md)——功能、全部既定P0及三题原答/集中澄清归档完成，本批收尾，见第14—16节。
同池Console种类、精确/dev/console、READ暂空-2及新FDwrite18；默认kdbg与user echo互斥，用户SIE0读取主动poll，无后台producer。
宿主130组/3CTest、默认36/正式user19/独立retry23项真实QEMU及原进程回归通过，107源保持；本批不预留标准流、不改旧WRITE5、不做shell/IRQ。

[BATCH26：长期存活的控制台设备与服务](BATCH26_CONSOLE_DEVICE_SERVICE.md)——功能、全部既定P0及三题原答/集中澄清归档完成，本批收尾。
设备拥有原始队列/显式收发回调，私有服务长期拥有设备；boot一次配置、kdbg统一走服务，当前仍任务A独占。
宿主120组、默认QEMU36项及真实55字节输入/68次442字节输出链、独立旧进程回归通过，106源保持；见当批第10—12节。本批不加设备FD/用户ABI/标准流。

[BATCH25：控制台原始输入队列](BATCH25_CONSOLE_INPUT_QUEUE.md)——功能、全部既定P0及三题原答/集中澄清归档完成，本批收尾。
256字节固定环、查看不消费/实际交付后提交；task_a独自持有并接在UART与kdbg之间，不加用户ABI/标准流/IRQ。
宿主115组、真实控制台27项与55字节队列实际交付、独立旧进程回归均通过，102源保持；详见当批第10—12节。

[BATCH24：内核cat/hex文件查看工具](BATCH24_KERNEL_FILE_CONSOLE.md)——功能、全部既定P0及三题原答/集中澄清归档完成（第13—14节），本批收尾。
有界行输入/命令分派和只读文件显示两模块，输出依赖显式传入；task_a栈上持控制台，局部迁C++，无新任务/FD/用户ABI。
宿主110用例、实际命令QEMU20项、当前独立进程回归/35页账本通过；普通控制台保持安静/零PMM，完整证据和限制集中见当批。

[BATCH23：UART轮询接收与内核任务回显](BATCH23_UART_POLLING_INPUT.md)——功能、全部既定P0与三题原答/集中澄清归档完成（第8—9节），本批收尾。
新增uart_try_getchar的0..255/-1接口，复用task_a回显、替换A刷屏；本批不接用户console/PLIC/队列或标准FD。
原始5字节/空输入/定时抢占与原正常资源链真实QEMU41项、宿主104用例通过；旧AA检查适配及限制见当批。后续拟做独立内核文件查看工具。

[BATCH22：进程FD与用户open/read/close](BATCH22_PROCESS_FILE_DESCRIPTORS.md)——功能、全部既定P0及三题原答/集中澄清归档完成（第18—19节），本批收尾。
文件服务私有池、Process私有FD表、15/16/17用户接口与fork共享/exec保留/退出关闭；原14接口保持。
当前宿主104用例、正常QEMU34项、真实文件边界33次调用/19拒绝、共享FD生命周期与56页释放账本通过；完整证据和隔离fixture限制见当批。

[BATCH21：打开文件对象与FD表](BATCH21_OPEN_FILE_MODEL.md)——功能/全部既定P0与三题归档完成（第16—17节），本批收尾。
内核固定池/FD表，独立open与共享继承、准备读取/提交位置、引用关闭与boot演示；本批不接Process或新用户ABI。
宿主六组/原suite及真实QEMU31项通过，实际7+11字节/共享位置引用/最后关闭/零PMM与原35页账本保持；生命周期接线留下一批。

[BATCH20：用户按路径、显式偏移读取文件](BATCH20_USER_FILE_READ.md)——功能/全部既定P0与三题归档完成，本批收尾。
新增READ_FILE14，安全复制用户路径、lookup/read_at及按实际数量拷出；复用只读FS，正常演示放WORKER。
无FD/隐式游标，不改变Process/Task生命周期；宿主三组及原suite、32次真实U边界、正式四次7/11与35页资源链通过，证据与限制见第11节。

[BATCH19：只读内置文件与按偏移读取](BATCH19_BUILTIN_FILE_SYSTEM.md)——功能/全部既定P0及三题归档完成，本批收尾。
只读文件表与read_at已接入boot，按名字查找/bin/demo、/bin/worker和/etc/about，再从lookup结果配置原ELF registry。
宿主7组及原suite、500轮压力、最终正常QEMU的文件来源/零PMM与35页资源链通过；执行路径、证据与限制见当批第10节。

[BATCH18：选择不同的内置ELF](BATCH18_BUILTIN_PROGRAMS.md)——功能/全部既定P0及三题原答/集中澄清/修正版均归档，本批收尾。
固定DEMO0、WORKER1，新12/13分离程序编号与入口参数，旧8/10保持默认镜像；镜像表独立拥有一次配置，SPAWN/EXEC复用原生命周期。
完整/增量构建及嵌入字节、registry13项、真实编号0参数0、正常35页资源链、指定WORKER的EXEC OOM/实际继续重试、原宿主/500轮压力均通过；正式RX3962/929，详细证据与限制见当批第12节。

[BATCH17：分离用户任务操作](BATCH17_USER_TASK_OPERATIONS.md)——功能/全部P0与原三题原答/集中审查/修正版均归档，本批收尾。
作者完成迁移后协作者按授权拆为create/exec/exit三个实现，统一无状态接口显式接收依赖；scheduler保留当前校验和重调度。正常资源链、500轮压力、活孤儿与EXEC准备失败/真实重试均通过，详细证据见当批文档。

[BATCH16：提取固定任务表](BATCH16_TASK_TABLE.md)——功能/P0、追加500轮压力与三题原答/集中审查/修正版均已归档，本批收尾。
Task/同槽Process固定存储、ID/高水位与分配/查找/安全复用归TaskTable；current/idle/root/reaper与调度/用户事务暂留sched。宿主2500记录与QEMU2000子进程压力、原双S演示/35页回收、五槽孤儿及ID耗尽边界均通过；证据和限制集中在本批。

[BATCH15：父子关系、等待收集与孤儿收养](BATCH15_PARENT_AND_WAIT.md)——功能、全部既定P0及三题原答/集中审查/修正版归档完成，本批收尾。
Process父ID、U1收养者、五槽、限父REAP与WAITPID11、活/DEAD孤儿转交均已实现；真实非父拒绝/坏地址重试、五份空间35页释放及零PMM收集验证通过。功能提交d3ba173，收尾证据在当批文档；整体跨文件拆分仍在进程管理阶段完成后。

[BATCH12：用户请求创建内置镜像任务](BATCH12_BUILTIN_SPAWN.md)——功能、全部既定P0、执行路径梳理和原三题原答/集中审查已归档，本批收尾。覆盖SPAWN/fresh U2/退出23/收集、失败回滚与重试、现场/隔离/回收和8KiB栈观测；保持固定槽/单hart，不做fork/exec/VFS。

[BATCH13：复制当前用户空间与执行现场的FORK](BATCH13_EAGER_FORK.md)——功能、全部既定P0及三题原答/集中审查归档完成，本批收尾。父5/子0、运行快照/隔离/子31收集、槽满与L0分配失败回滚/重试、28次合法释放与8KiB栈观测均通过；保持单hart固定槽，不做COW/exec/父子权限或VFS。

[BATCH14：保持任务身份并替换内置用户镜像](BATCH14_BUILTIN_EXEC.md)——功能、全部既定P0与原三题原答/集中审查已归档，本批收尾。EXEC10保持同Task/ID5，新镜像tag3退出37并由父收集；准备失败旧程序可继续/重试，35次合法释放及真实8KiB栈观测通过。不新增VFS/argv/父子权限。

2026-11-01[静态对象构造/析构运行时](../CPP_STANDARD_LIBRARY.md)通过实际构建、正常系统回归和受控析构QEMU后，作者恢复BATCH08主线。

[BATCH08：独立用户根页表与显式地址空间生命周期](BATCH08_PRIVATE_ADDRESS_SPACE.md)——整批已收尾。82组宿主验证及真实QEMU确认同VA隔离、跨槽零分配拒绝、S/U/idle选根、用户timer、syscall私有root和7页退出回收；三题已集中澄清并列根分支、活动根销毁及root-only重试。该批正式演示保持一个用户任务、静态槽及单hart。

[BATCH09：双用户任务与同址数据隔离](BATCH09_TWO_USER_TASKS.md)——整批收尾。同一ELF两份独立装载，tag0/1与三轮计数通过；82组宿主、真实双U切根/syscall/用户timer、先退7页且幸存者保持、再退7页及启动失败8页清理均通过。三题原答/集中审查/修正版已归档，澄清0/1下标、a0路径和完整外层释放。

[BATCH10：用户退出码与异常终止原因](BATCH10_USER_EXIT_RESULT.md)——整批收尾。正常0、main返回23、fault13与完整负long、初始化/分配清结果、无效入口零修改及7+7回收/结果持久化均通过；82宿主及正式双U通过。三题原答/集中审查/修正版已归档，澄清用户ABI返回、reason区分及task_destroy保留结果。

[BATCH11：收集用户退出结果与静态槽复用](BATCH11_REAP_AND_REUSE.md)——整批收尾。Process/REAP、初始化及全部既定P0通过（82宿主、正式双U、事务/复用/FAULT/宽值），三题原答/集中审查/修正版已归档。非阻塞REAP先复制结果后消费，DEAD→UNUSED；公开单调ID与槽索引分开，高水位内找空槽复用。限定安全boot/trap阶段，不增加父子等待或创建syscall；全部P0由协作者承担。

## 相关主题

- [Task组成与模块边界](../TASK_MODULE_DESIGN.md)：架构上下文与Process已拆出，记录资源和调度职责分工。
- [当前代码规范](../../CODE_STYLE.md)：新代码与本次触及代码的规则及工具配置。

- [受限 ELF 的功能与学习总结](../USER_ELF_LEARNING.md)：早于这轮 C++ 改造的 ELF 基础，不重复编成另一份批次记录。
- [C++ 标准库基础支持](../CPP_STANDARD_LIBRARY.md)：工具链及现有基础头的独立主题。
- [rCore 章节总结](../rcore/README.md)：用于补充原理，不与 TinyOS 验收混记。

## 阅读与更新

先看对应批次的结果和接口，需要追溯时再看过程记录。旧错误和旧任务保留当时语境，以文件开头的状态和最终验收结论为准。

每批固定归档目标/接口、实现/修正、验证/限制，以及理解题的原答/审查/修正版；不为每个小修正再新增文件。证据目录通常位于被 Git 忽略的仓库根 `.cache/`，文档记录结论，详细日志留在本地。
