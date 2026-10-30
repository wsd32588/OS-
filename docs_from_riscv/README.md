# RISC-V与操作系统学习资料

更新：2026-10-30。本目录沿用仓库已有名称`docs_from_riscv/`。

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
| [BATCH07：安全写回用户内存](tinyos/BATCH07_COPY_TO_USER.md) | **当前任务**：`user_copy_to` 与固定8字节原始时间GET_TIME；完整接口/不变量/分级验收已交付，待作者实现 |
| [BATCH06：多页数据/BSS](tinyos/BATCH06_MULTI_PAGE_DATA.md) | **最近完成**：有界页集合与跨页装载、70个宿主场景和真实QEMU通过，三题原答与修正已归档 |
| [BATCH05：ELF装载模块与布局配置](tinyos/BATCH05_ELF_LOADER.md) | 已归档：公开接口、布局检查、启动接线、验证及三题审查 |
| [TINYOS_EVOLUTION.md](TINYOS_EVOLUTION.md) | 总路线图、完成状态和已知限制；过程细节移至各批次文档 |
| [CPP_STANDARD_LIBRARY.md](CPP_STANDARD_LIBRARY.md) | C++标准库基础支持接入：目标组件、工具链约束、验收与后续实际结果 |

rCore子项目的成功不会自动修复TinyOS主线。两套实现与验证记录分别保留。

后续每个完整的TinyOS批次使用`tinyos/BATCHnn_TOPIC.md`，在同一文件中持续记录和修订，不把不同批次的详细过程堆回总路线图。

## 规范及其他资料

- `riscv-spec.pdf`：已有RISC-V非特权规范PDF；阅读时以PDF内部版本为准。
- `riscv-privileged.pdf`：已有RISC-V特权规范PDF；不因本次整理宣称它是最新版。
- [md-scroll.md](md-scroll.md)：已有个人工具资料，保持原内容。
- 教程和实现固定来源：[sources.lock.json](../experiments/rcore-learning/sources.lock.json)。

现有PDF、个人资料、历史任务文件均保留。本次章节总结是新增整理层，不迁移或删除这些原件。
