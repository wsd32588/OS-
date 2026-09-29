# TinyOS 项目记忆

最后更新：2026-09-30

## 协作方式

- 这是运行在 Windows WSL Ubuntu 上的 RV64 学习内核，QEMU 机器为 `virt`，由 OpenSBI 在 S-mode 启动，入口为 `0x80200000`。
- 作者希望边学边亲手实现。每批只推进一个概念：先解释目标和可观察结果，再让作者写，协作者负责审查和验证；不要一次性代写或重构大量代码。
- 新会话或其他协作者必须先读 `AGENT.MD` 和本文件，再看实际源码与 `git status --short --branch`。
- 远期的 Lisp/C++ 语言设想以及 `D-li-ne`、`TextEditor` 集成均冻结；当前只完成内核基础设施。

## 当前实现

- 启动与链接：`kernel/entry.S` 建立 16 KiB boot stack；`linker.ld` 把内核放在 `0x80200000`。
- Trap：`kernel/trap_entry.S` 保存/恢复 272 字节 `TrapFrame`；C/汇编偏移统一来自 `include/trap_offsets.h`，C 端有静态断言。
- 中断与时钟：`kernel/trap.c` 处理 S timer interrupt；`kernel/timer.c` 经 SBI TIME 设置下一次中断；未知 trap 打印 `scause`、`sepc`、`stval` 后停机。
- 调度：`kernel/sched.c` 是两个静态 S-mode 任务的轮转抢占调度器，任务栈各 4 KiB，状态为 READY/RUNNING/DEAD。入口函数返回会经 trampoline 调用 `task_exit()`；全部任务结束时关闭 STIE 后停机。目前没有 idle task。
- PMM：`kernel/pmm.c` 从页对齐的 `__kernel_end` 分配 4 KiB 页，以 Device Tree 地址为保守上界；支持清零、释放、intrusive free list、优先复用和重复释放检测。尚无并发保护。
- 虚拟内存：`kernel/vm.c` 实现 Sv39 canonical 地址检查、VPN/PTE 编解码、三级 4 KiB 映射、查询和叶映射解除；中间页表由 PMM 分配。`kernel/vm_arch.c` 负责 `satp`、`sfence.vma` 和 CSR 读取。
- 当前使用恒等映射：内核、PMM 区域和 UART `0x10000000` 的 VA 等于 PA；没有 unmap、页表回收、ASID 或每任务地址空间。
- 测试：`test/` 在宿主机的页对齐内存上测试 PMM 和纯 Sv39 算法；RISC-V CSR、真实 page fault、trap 和调度只在 QEMU 验证。

## 已确认的关键事实

- `make test` 通过：PMM 基本测试、8192 页上的 20,000 次确定性压力测试，以及包含 4096 页映射/查询和异常 PTE 的 Sv39 测试。
- 分页已经真实启用。拆段前曾验证 `satp.MODE=8`，并通过读取未映射 canonical 地址 `0x40000000` 得到 `scause=13`、`stval=0x40000000`；临时故障注入已经移除。
- 当前 `linker.ld` 使用三个 `PT_LOAD`：`.text` 为 RX，`.rodata`（含 `.eh_frame`）为 R，`.data/.bss` 为 RW；各映射边界按 4 KiB 对齐。
- 当前 PTE 权限与 ELF 段对应：text=`R|X|A`，rodata=`R|A`，data/bss、PMM 和 UART=`R|W|A|D`，叶 PTE 另含 `V`。
- 2026-09-29 最新验证：
  - `make -B test` 全部通过。
  - `make -B` 无编译或链接警告。
  - `readelf -lW kernel.elf` 显示三个 LOAD 段分别为 `R E`、`R`、`RW`，不再有 RWE 段。
  - QEMU 内核范围为 `[0x80200000, 0x8020a000)`，根页表为 `0x8020a000`，`satp=0x800000000008020a`。
  - 启用分页后 UART、timer trap、A/B 抢占、B 返回和 A 继续运行均正常；`timeout` 导致的退出码 124 不表示内核失败。
  - 向 `__rodata_start=0x80202000` 临时写入时得到 `scause=15`、`stval=0x80202000`，证明只读 PTE 阻止了 store。
  - 跳转到 `__data_start=0x80203000` 临时取指时得到 `scause=12`，且 `sepc=stval=0x80203000`，证明无 X 的 PTE 在取指前触发 page fault。两个临时探针均已删除。
  - `sv39_unmap_page()` 已通过宿主测试：覆盖参数错误、缺失路径不分配页表、非法/V-only PTE、中间大页、相邻映射隔离、重复 unmap、PMM 页数不变和解除后重新映射。
  - 2026-09-30 强制宿主测试、交叉构建和 8 秒 QEMU 回归通过；额外的 ASan/UBSan 宿主测试在关闭当前环境不支持的 leak 检测后通过。
- `task_yield()` 已移除：当前任务在 S-mode，S-mode ECALL cause 9 没有由 OpenSBI 委托给 S-mode；旧代码的 `a7=1` 反而命中 legacy SBI console putchar。以后应在 U-mode syscall 或明确设计的内核同步调度入口中重新引入。

## Git 与工作区

- `main` 跟踪 `origin/main`；分页里程碑为 `7335387 Enable Sv39 paging`，段权限里程碑为 `7326a7f Protect kernel sections with Sv39 permissions`。当前本地分支领先远端 2 个提交。
- 权限实验结束后工作区已确认干净。后续仍要先检查 Git 状态，不要擅自恢复、覆盖、重新暂存或提交作者的改动。
- `.o`、`.d`、`kernel.elf` 已被 `.gitignore` 忽略。不要把 `make clean` 当作普通验证步骤。

## 下一步（只做这一批）

学习并实现空中间页表的回收，仍先在宿主测试中完成：

1. unmap 一个叶 PTE 后，扫描对应 L0 表；只有 512 项全为 0 时才允许从 L1 断开并释放该表。
2. 再用相同规则判断 L1 表是否为空；根页表本身不在单页 unmap 中释放。
3. 保证存在相邻映射时不回收共享页表，并验证 PMM 页数只在整张中间表变空时增加。
4. 纯页表层仍不刷新 TLB；真正修改活动页表前再为架构层设计按地址 `sfence.vma` 接口。

## 已知限制

- PMM 以内核末尾和 Device Tree 地址推断可用区，不解析 DTB 内存节点，也没有锁。
- 当前页表依赖恒等映射才能直接解引用物理页表地址。
- `sv39_map_page()`/`sv39_unmap_page()` 只处理 4 KiB 叶；unmap 尚不回收空中间页表，也没有权限修改接口。
- 调度器仅用于演示两个静态 S-mode 内核任务，没有 idle task、用户态隔离、syscall 或动态任务生命周期。

## 常用命令

```sh
cd /home/asus/tinyos
make -B test
make -B
riscv64-linux-gnu-readelf -lW kernel.elf
timeout 8s make qemu
make compdb
git diff --check
git status --short --branch
```

QEMU 手动退出：先按 `Ctrl+A`，再按 `X`。完整的 QEMU + `gdb-multiarch` 双终端调试流程见 `DEBUGGING.MD`；启动/内存管理路线见 `BRINGUP.md`。
