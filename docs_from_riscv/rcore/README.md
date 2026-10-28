# rCore章节总结

初次整理：2026-10-24；第四章补全与本轮收尾：2026-10-25。
本轮BATCH01—22核心功能与已安排理解题均已验收/归档，第四章按约定范围结束，现回TinyOS。
rCore保留为按需补充，不自动进入第五章。第一至三章总结保持原范围，第四章已补齐到B22。
[BATCH20](../../experiments/rcore-learning/BATCH20.md)已于2026-10-25通过作者功能验收，三个理解题已复核并保存。
[BATCH21](../../experiments/rcore-learning/BATCH21.md)两个分页任务的yield协作调度已通过作者功能验收，三个理解题已复核并记录。
[BATCH22](../../experiments/rcore-learning/BATCH22.md)分页timer抢占与最后三个理解题均已完成，原答和修正版已保存。

## 从哪里开始读

| 阅读顺序 | 章节总结 | 对应批次 | 本项目的完成边界 |
| --- | --- | --- | --- |
| 1 | [第一章：启动与运行时](CHAPTER01_BOOT_AND_RUNTIME.md) | B01—02 | 从固件交接到Rust、输出与panic/关机 |
| 2 | [第二章：用户态与Trap](CHAPTER02_USER_AND_TRAP.md) | B03—05 | 用户镜像、初始现场、U/S切换和write/exit；顺序替换批处理循环略过 |
| 3 | [第三章：任务、切换与时钟](CHAPTER03_TASKS_AND_TIMER.md) | B06—09 | 两应用常驻、协作调度和timer抢占；get_time与扩展练习未实现 |
| 4 | [第四章：地址空间](CHAPTER04_ADDRESS_SPACES.md) | B10—22 | 堆、页帧、Sv39、MemorySet、ELF、分页syscall与双任务协作/抢占；本轮收尾 |
| 专题 | [Bare、Sv39、特权切换与页表生命周期](BARE_SV39_TRAP.md) | B15—19 | 截至B19的模式、地址、现场与回收顺序；后续timer见第四章总结 |

每章按“本章解决什么→模块怎样分工→完整执行路径→关键状态与所有权→易错点→验证边界”组织。
这是对原理与最终实现的合并整理，不是把历次任务机械拼接，也不是新一轮作业。

## 四章之间怎样连接

```text
第一章：CPU如何走进Rust？代码怎样输出和结束？
    ↓ 提供启动、栈、BSS、SBI与基本运行时
第二章：怎样让用户代码运行，再受控地回到内核？
    ↓ 提供用户入口、TrapContext、ecall/sret与系统调用
第三章：怎样让多个可恢复的执行流轮流运行？
    ↓ 提供TaskContext、任务状态、调度与定时器
第四章：每个应用怎样拥有自己的内存视图？
    ↓ 已打通独立页表下的ELF、syscall与分页多任务
当前安排：本轮收尾，返回TinyOS受限ELF装载实现与学习测试
```

第三章解决“何时运行谁”，第四章解决“运行时地址怎样解释、哪些内存能访问”。
第四章总结已包含B20分页syscall返回、B21独立空间协作调度与B22时钟抢占。
原B19的单应用闭环与这两个后续批次的实现范围应分别看待。

## 当前仓库同时保留两条运行路径

启动阶段`mm::init()`依次执行内存演示及B19—22真实分页用户演示。
每个演示各自恢复Bare/原CSR并回收临时页，B22还先撤销自己的timer；
全部结束后，原loader再装载两个flat bin应用，进入旧Bare timer调度。

| 路径 | 用户程序 | 地址环境 | 现场与退出方式 |
| --- | --- | --- | --- |
| B19临时分页演示 | image_demo ELF，入口0x40000000 | 独立用户Sv39表，内核另有自己的表 | PagedTrapContext296字节，exit后返回启动调用并回收 |
| B20分页syscall演示 | syscall_demo ELF | 同一用户空间多次往返 | write后修改sepc/a0继续用户，exit后回收23页 |
| B21/B22分页双任务 | task_demo / preempt_demo ELF分别装载两次 | 独立用户根与私有数据，共用内核根和启动栈 | 296字节用户现场；yield/timer选择用户空间，全部退出后归还35页 |
| 原双应用调度 | hello/hello_peer，入口0x80400000/0x80420000 | Bare | TrapContext272字节和TaskContext112字节，支持yield/timer/exit |

因此一份源码中可能同时看到旧`__alltraps/__restore`、分页`__paged_alltraps/__paged_restore`，
以及B18纯S-mode的跳板probe。它们各自的调用范围和sscratch契约要分别追踪。

## 怎么结合原始材料复习

| 材料 | 回答什么问题 |
| --- | --- |
| 本目录章节总结 | 为什么这样设计，各批知识怎样连起来？ |
| [Bare专题](BARE_SV39_TRAP.md) | 哪个操作改特权级、哪个操作改页表，何时能安全释放？ |
| [BATCH01—22](../../experiments/rcore-learning/README.md) | 当时做了什么，原回答哪里有误，怎样修正？ |
| [PROGRESS.md](../../experiments/rcore-learning/PROGRESS.md) | 哪些行为已经验证、证据在哪、当前停在哪里？ |
| [ROADMAP.md](../../experiments/rcore-learning/ROADMAP.md) | 教程整体顺序及尚未实现的章节边界 |
| [os/src](../../experiments/rcore-learning/os/src)与[user/src](../../experiments/rcore-learning/user/src) | 当前实际运行的代码是什么？ |

硬件原理、示例地址和一次构建观测应区分。例如0x80200000是当前平台的内核装载安排，
某个函数或栈的具体链接地址可能随构建变化；教材设计也不等于作者代码已经实现全部功能。

## 整理与验证依据

本目录总结整理时只读核对已有批次、答题记录和相关源码，没有为整理重跑QEMU或修改功能实现。
B20—22通过结论沿用已完成的作者验收，进度和证据以各批任务文档与PROGRESS为准。
本轮最新作者功能验收为学习目录`.cache/batch22-author-review-l9k_umtl/`，
真实抢占/4ecall/35页与CSR恢复通过，保留一条未用导入警告；本次收尾整理不重复运行。

教程固定来源与各章提交见[sources.lock.json](../../experiments/rcore-learning/sources.lock.json)。
原批次文档、TinyOS资料与PDF均保留；接下来由作者主导
[TinyOS受限ELF装载](../USER_ELF_LOADING.md)，rCore不新增进程、fork/exec或文件系统等功能。
