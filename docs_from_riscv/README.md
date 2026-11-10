# RISC-V与操作系统学习资料

更新：2026-11-07。本目录沿用仓库已有名称`docs_from_riscv/`。

## rCore章节总结与当前机制

阅读入口：[rCore章节总结索引](rcore/README.md)。
按第一至第四章组织整体原理、执行路径、数据结构、资源归属与已验证边界。
当前重点：[Bare、Sv39、特权切换与页表生命周期](rcore/BARE_SV39_TRAP.md)。

原始BATCH01—22任务、答题原文与逐次验收记录继续保存在
[rCore学习工作区](../experiments/rcore-learning/README.md)；章节总结负责连贯复习，批次文档负责追溯细节。
章节总结覆盖本轮BATCH01—22；第四章本轮收尾完成，当前已回到TinyOS主线。
rCore用于补充具体概念，不自动进入第五章；最新状态以学习工作区PROGRESS为准。

## TinyOS主线资料

| 文档 | 用途与边界 |
| --- | --- |
| [USER_ELF_LEARNING.md](USER_ELF_LEARNING.md) | **ELF统一复习入口**：当前全路径、概念问答、作者原答与修正；旧解析资料折叠保留 |
| [USER_ELF_LOADING.md](USER_ELF_LOADING.md) | 已完成的受限ELF装载任务与验收历史；日常复习看上一份总结即可 |
| [TinyOS批次索引](tinyos/README.md) | 按完整批次查找任务、实现、验证和原答修正；每批一份独立文档 |
| [BATCH12：用户创建内置镜像任务](tinyos/BATCH12_BUILTIN_SPAWN.md) | **当前任务**：完整接口与验收已交付，SPAWN8、fresh用户空间、返回新ID并收集status23，作者已开始配置草稿 |
| [BATCH11：收集结果与静态槽复用](tinyos/BATCH11_REAP_AND_REUSE.md) | **最近完成**：Process/REAP、槽复用和初始化全部必要验证通过，三题原答/审查/修正版已归档 |
| [BATCH10：用户退出码与异常终止原因](tinyos/BATCH10_USER_EXIT_RESULT.md) | 已归档：功能/P0及三题归档完成；正常/23/fault/完整负long、结果重置与保留、7+7回收及82宿主通过 |
| [BATCH09：双用户任务与同址数据隔离](tinyos/BATCH09_TWO_USER_TASKS.md) | 已归档：双tag三轮计数、82组宿主、真实双U切根与7+7页回收/幸存者保持及启动拒绝通过，三题原答/集中审查/修正版已归档 |
| [BATCH08：独立用户根页表](tinyos/BATCH08_PRIVATE_ADDRESS_SPACE.md) | 已归档：82组宿主及真实选根/用户timer/idle/跨槽拒绝/7页回收通过，三题原答/集中审查/修正版已归档 |
| [BATCH07：安全写回用户内存](tinyos/BATCH07_COPY_TO_USER.md) | 已归档：写回与GET_TIME、75个宿主场景、正式及临时QEMU通过；三题原答/审查/修正版已归档 |
| [BATCH06：多页数据/BSS](tinyos/BATCH06_MULTI_PAGE_DATA.md) | 已归档：有界页集合与跨页装载、70个宿主场景和真实QEMU通过，三题原答与修正已归档 |
| [BATCH05：ELF装载模块与布局配置](tinyos/BATCH05_ELF_LOADER.md) | 已归档：公开接口、布局检查、启动接线、验证及三题审查 |
| [TINYOS_EVOLUTION.md](TINYOS_EVOLUTION.md) | 总路线图、完成状态和已知限制；过程细节移至各批次文档 |
| [CPP_STANDARD_LIBRARY.md](CPP_STANDARD_LIBRARY.md) | **运行时最小验证通过**：启动构造、防重复、DSO筛选与逆序析构已真实QEMU验证；含机制说明、日志入口及标准库历史边界 |

rCore子项目的成功不会自动修复TinyOS主线。两套实现与验证记录分别保留。

后续每个完整的TinyOS批次使用`tinyos/BATCHnn_TOPIC.md`，在同一文件中持续记录和修订，不把不同批次的详细过程堆回总路线图。

## 规范及其他资料

- [TinyOS代码规范](../CODE_STYLE.md)：当前通用/C/C++规则、头文件边界及内核/宿主配置。
- [Task组成与模块边界](TASK_MODULE_DESIGN.md)：已实现的RISC-V上下文、Process资源边界与调度器分工。
- `riscv-spec.pdf`：已有RISC-V非特权规范PDF；阅读时以PDF内部版本为准。
- `riscv-privileged.pdf`：已有RISC-V特权规范PDF；不因本次整理宣称它是最新版。
- [md-scroll.md](md-scroll.md)：已有个人工具资料，保持原内容。
- 教程和实现固定来源：[sources.lock.json](../experiments/rcore-learning/sources.lock.json)。

现有PDF、个人资料、历史任务文件均保留。本次章节总结是新增整理层，不迁移或删除这些原件。
