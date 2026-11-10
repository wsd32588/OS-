# TinyOS 改造批次索引

更新：2026-11-09。这里的编号只用于 TinyOS 改造线，独立于 [rCore 学习批次](../../experiments/rcore-learning/README.md)。

每个完整批次只使用一份文档，在其中持续维护任务、接口、修改记录、验证和问答。总路线见 [TINYOS_EVOLUTION.md](../TINYOS_EVOLUTION.md)。

## 已完成批次

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

最近完整归档 **BATCH14**，功能、P0和三题原答/集中审查/修正版均完成；当前为BATCH15任务。

## 当前批次与近期归档

[BATCH15：父子关系、等待收集与孤儿收养](BATCH15_PARENT_AND_WAIT.md)——完整任务已交付，待作者实现。
Process父ID、U1收养者、五槽、限父REAP与WAITPID11、活/DEAD孤儿转交；接口与一页用户预算准备通过，未实现新内核功能。全部P0协作者承担，阶段完成后再拆sched。

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
