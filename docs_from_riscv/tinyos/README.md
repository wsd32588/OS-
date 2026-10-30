# TinyOS 改造批次索引

更新：2026-10-30。这里的编号只用于 TinyOS 改造线，独立于 [rCore 学习批次](../../experiments/rcore-learning/README.md)。

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

最近完成 **BATCH06**，功能、P0验证和三题原答/审查/修正版均已归档。

## 当前批次

[BATCH07：安全写回用户内存](BATCH07_COPY_TO_USER.md)——完整任务已交付，待作者实现。新增 `user_copy_to` 和固定8字节原始时间输出的 GET_TIME，练习逐页翻译、写权限与失败前缀；复用BATCH06跨页buffer。文件/接口、不变量、可观察结果及分级验收集中在该文档，协作者承担P0，不追加历史测试矩阵。

## 相关主题

- [受限 ELF 的功能与学习总结](../USER_ELF_LEARNING.md)：早于这轮 C++ 改造的 ELF 基础，不重复编成另一份批次记录。
- [C++ 标准库基础支持](../CPP_STANDARD_LIBRARY.md)：工具链及现有基础头的独立主题。
- [rCore 章节总结](../rcore/README.md)：用于补充原理，不与 TinyOS 验收混记。

## 阅读与更新

先看对应批次的结果和接口，需要追溯时再看过程记录。旧错误和旧任务保留当时语境，以文件开头的状态和最终验收结论为准。

每批固定归档目标/接口、实现/修正、验证/限制，以及理解题的原答/审查/修正版；不为每个小修正再新增文件。证据目录通常位于被 Git 忽略的仓库根 `.cache/`，文档记录结论，详细日志留在本地。
