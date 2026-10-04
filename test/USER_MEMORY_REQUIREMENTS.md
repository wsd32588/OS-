# 用户内存回收与失败回滚：回归测试需求

本批目标：把已经验证过的用户内存释放、准备失败回滚和任务创建失败清理，整理成仓库内可重复运行的测试。本文定义验收条件；测试完成情况以实际执行结果和 `MEMORY.MD` 为准。

## 1. 当前基础与范围

- `user_memory_release()`、`prepare_demo_user_memory()` 和任务创建失败时的调用者清理已经实现。
- `make test` 已使用 C++20 测试代码覆盖 PMM、Sv39 和 H1-H8 全部释放场景；实际被测模块继续按 C 编译。
- `verify_prepare_rollback.py` 保留 Q1 的 8 个准备失败场景；`verify_task_creation_rollback.py` 保留 Q2 的任务槽满及实际调用者清理。`make test-qemu` 顺序运行两个脚本。
- 本批沿用共享根页表、静态任务及静态内核栈。宿主测试已拆为独立 CMake 项目，主内核继续使用 Make；每任务地址空间、动态任务、ELF 加载和内核语言迁移留到后续。

资源约定：`UserMemory` 拥有代码页和栈页，借用根页表；只回收已为空的中间页表。任务创建成功后归属转交给任务，失败时仍由调用者持有。根页表、静态内核栈以及其他映射必须保留。

## 2. 宿主机释放测试

链接仓库实际的 `kernel/user_memory.c`、`kernel/vm.c` 和 `kernel/pmm.c`。使用页对齐的宿主内存建立测试页池，替代 UART 和架构相关 flush 接口；替代接口只记录调用或注入故障，不复制被测释放逻辑。

| 编号 | 场景 | 验收结果 |
|---|---|---|
| H1 | 新建根页表，用户代码和栈共享一个独立 L0/L1 分支，正常释放 | 返回 `SV39_OK`；两处查询均为 `SV39_ERR_NOT_MAPPED`；归还代码、栈、L0、L1 共 4 页；根页表保留；描述五字段全部清空 |
| H2 | 完全空描述；以及 H1 成功后再次释放 | 返回 `SV39_OK`；空闲页数不增加；不再执行解除映射或物理页释放 |
| H3 | 分别在代码、栈映射中设置 PA 不匹配或缺少 U 位 | 返回错误；两处映射的 PA/flags、空闲页数和描述均不变；不得先释放另一处合法映射 |
| H4 | 注入栈物理页释放失败，再撤销故障重试 | 首次返回 `SV39_ERR_PAGE_FREE_FAILED`；已释放代码指针立即为空；栈指针、root 和 VA 保留；重试完成剩余清理，最终页数恢复且描述全空；代码页不重复释放 |
| H5 | 两处叶映射已解除，但物理页和页表仍待清理 | 接受 `SV39_ERR_NOT_MAPPED`，继续完成清理；同一路径被第一次回收后，第二次回收不得误报失败 |
| H6 | NULL 参数；非空描述缺少 root | 返回 `SV39_ERR_INVALID_ARGUMENT`；不解引用 NULL，不修改资源或描述 |
| H7 | 同一中间页表中保留一处其他映射，再释放用户代码和栈 | 用户页归还；其他映射 PA/flags 不变；非空页表及根页表保留；页数按实际可回收资源计算 |
| H8 | 分别注入第一次、第二次 flush 失败，再撤销故障重试 | 首次返回错误；物理页及中间页表尚未释放，拥有页的指针保留；重试完成清理，最终页数恢复 |

测试还应记录释放顺序：两处用户叶映射解除且所需 flush 成功后，才能归还用户物理页或中间页表。宿主记录只能检查调用约定，真实 TLB 行为由 QEMU 验证。

## 3. QEMU 准备失败与调用者清理

沿用现有脚本在临时源码副本中插入探针，执行真实 `fence.i`/`sfence.vma`；不向工作区功能源码永久加入探针。

### Q1：保留现有 8 个准备失败场景

- 代码页、栈页、L1、L0 四个分配失败点。
- 首次、第二次 flush 的一次性失败；后续清理操作恢复正常。
- 代码 VA、栈 VA 两种已有映射冲突。

每个场景必须检查原始错误码、空闲页数恢复到调用前、输出描述保持哨兵值。原本不存在的用户路径应清除；冲突场景必须保留原映射 PA/flags，仅清理本次创建的资源。

全部场景通过并关闭注入后，原演示继续执行 yield、R、sleep、X、exit，exit 后至少观察两次 S-mode timer tick，且没有意外 fault 或清理失败。

### Q2：持久化任务槽满场景

在临时副本中用内核任务占满普通任务槽，再实际调用 `task_create_user()`：

1. 创建返回失败，调用者描述在清理前逐字段保持原值。
2. 执行 `kernel_main()` 已有失败分支中的调用者清理。
3. 清理成功，描述全空，用户代码和栈映射消失，准备阶段分配的 4 页归还。
4. 共享 root 及内核恒等映射保留。
5. 输出明确的 PASS 后进入原有失败停机分支；本场景不要求继续调度。

Q2 可扩展现有脚本，也可使用独立脚本。不得以直接调用释放函数替代对实际创建失败及调用者分支的验证。

## 4. 测试交付与完成标准

- 释放测试的源码、故障注入和断言保存在 `test/`，接入 `make test`。
- QEMU 脚本自行创建和清理临时副本，不依赖此前留在 `/tmp` 的文件。
- 每个失败报告应包含场景及不满足的条件；断言至少检查返回值、页数、描述和映射，不能只匹配日志。
- 失败、缺少工具、构建失败或未在限时内观察到必需结果，均返回非零。主动结束已完成验收的 QEMU 观察不能单独当作失败；超时本身也不能当作通过。
- `test/README.md` 记录最终运行命令及宿主/QEMU 的验证边界；`MEMORY.MD` 记录实际执行结果。

完成时执行：

```sh
make test
make
python3 test/verify_prepare_rollback.py
python3 test/verify_task_creation_rollback.py
# 两个 QEMU 脚本也可用 make test-qemu 一起运行。
git diff --check
```

本批不要求恢复损坏的任意页表，也不要求同时注入多个清理故障。准备失败的 8 个场景以清理阶段能正常执行为前提；释放函数的部分失败重试由 H4/H8 单独检查。

## 5. 建议实施顺序

H1-H8 已接入 `make test`，Q1/Q2 已接入 `make test-qemu`。2026-10-05 完整复验通过，本批需求已完成。后续练习继续一次给齐要求、注意事项和关键代码，作者完成后集中检查；发现功能缺陷时，以失败用例配合最小修复。

H8 的 host_set_flush_failure 指定接下来第几次 flush 失败一次，0 关闭；失败调用仍计数并记录事件。
首次释放返回 INVALID_ARGUMENT，叶映射均消失，描述及空闲页数不变，两级父 PTE 保留。
代码 flush 失败会短路栈 flush，增量为 2/1/0；栈 flush 失败时为 2/2/0。
关闭注入后重试，unmap 均返回 NOT_MAPPED，重新 flush 两页后 free 代码、栈、L0、L1，
增量 2/2/4，描述全空及页数恢复。两个阶段分别比较完整事件，根页表从未 free。

Q2 独立脚本在临时副本中保留原 task_create_user 调用及 kernel_main 失败清理语句，
探针只填槽并检查前后状态；通过 free 包装检测任何 root free 尝试，检查 satp、
内核 text/rodata/data 各页和 root/UART 的恒等映射。准备消耗四页、创建失败保留五字段、
清理归还四页且清空描述/用户路径均已在 QEMU 实测；随后进入原失败停机分支。

已完成并验收的 H7：在 fixture.stack_address 后一页映射独立 PMM 页，
第三处映射共用原 L0/L1，建立映射不应额外分配页表。确认准备的 PA/flags，
保存 root->L1 和 L1->L0 PTE；release 返回 OK、清空描述、两处用户映射消失，
空闲页数仅增加 2，第三处映射及两个父 PTE 不变。释放区间调用增量 2/2/2，
精确六条事件只有两个用户 unmap、flush、free，没有第三页或任何页表 free。
上述检查结束后，再由测试解除第三处映射、flush、free 第三页、reclaim 空表，
最后 expect_released 确认恢复到 pages_after_root。不能在第三映射保留时使用该检查。
test_release_preserves_other_mapping 已接到 H7 run_case；make test CTEST='ctest --verbose' 复验全部通过，本批未修改内核逻辑。

已完成的 H6 包含两个独立用例：传入 nullptr；正常 fixture 的描述仅将 root 设为 nullptr。
两者返回 INVALID_ARGUMENT，完整状态及 unmap/flush/free 调用计数保持不变。
缺 root 用例须通过保留的 fixture.root 查询真实代码/栈映射，补入观察快照；
capture_user_memory 在描述 root 为空时跳过查询，默认映射值不能证明实际映射不变。
非法调用检查后，恢复描述 root（若曾清空），正常释放并调用 expect_released。
两个函数均接到 test_user_memory 的 run_case 链；不增加事件循环或故障注入。
作者实现后经 make test CTEST='ctest --verbose' 复验，两个 H6 及全部宿主测试通过。

已完成的 H3 包含代码 PA 不符、栈 PA 不符、代码缺 U、栈缺 U 四个独立用例。
每个用例使用新的 UserMemoryFixture，只修改一处映射，保留原描述；
PA 不符可暂时映射到另一处已分配的用户页，缺 U 只去掉 U 位。
准备异常映射并确认查询 PA/flags 后再取快照；调用释放应返回
SV39_ERR_INVALID_ARGUMENT，完整快照和 unmap/flush/free 调用计数均保持不变。
随后恢复正确映射，确认正常释放仍成功并恢复四页，避免错误路径破坏后续清理。
四个用例都需接到 test_user_memory()。此批仅验证宿主逻辑，不修改内核功能。

已完成的 H4 仅在 host_support 的 pmm_free_page 包装中注入指定页失败：
新增 host_set_free_failure(void *page)，非空指针启用，nullptr 关闭。
匹配时不调用真实 free，返回 PMM 错误 -1，并照常记录尝试和事件；
user_memory_release 应将其转换为 SV39_ERR_PAGE_FREE_FAILED。
不手动提前 unmap，不改变真实 PMM/内核逻辑。

首次释放的完整验收：两处叶映射均不存在，只有代码页归还（空闲页数 +1）；
描述仅 code_page 清空，stack_page/root/两个 VA 保留；L0/L1 尚未释放。
unmap/flush/free 尝试次数分别增加 2/2/2。
首次调用后立即关闭注入，再做断言和重试。
重试返回 OK，归还栈/L0/L1 共三页，描述全空；尝试次数分别增加 2/2/3。
调用计数用前后差值；free 计数包含失败的尝试。

分别按首次与重试的事件区间检查：两次 unmap 完成后执行两个 flush，
两个 flush 成功后才开始 free。首次只有代码成功 free 与栈失败 free；
重试只有栈成功 free 和两个中间表成功 free。
两次调用合计代码 free 尝试恰为一次，栈 free 尝试恰为两次（一次 -1、一次 0），
根页表从未作为 free 参数。保存旧页地址仅用于事件比较，不访问已释放页内容。
test_stack_free_failure_retry 已接到入口；require_events 分别比较两个事件区间。
fixture.stack_address 是固定常量，描述状态检查使用可变的 fixture.memory 字段。
