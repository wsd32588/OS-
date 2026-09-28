# TinyOS 项目记忆

最后更新：2026-09-29

## 项目定位

- 学习用的 64 位 RISC-V 小型内核，运行环境是 Windows WSL Ubuntu。
- 目标机器为 QEMU `virt`，由 OpenSBI 启动，内核入口地址为 `0x80200000`。
- 当前阶段重点是 S 模式 trap、SBI 定时器、TrapFrame 和简单抢占式调度。
- 作者希望亲手完成适合学习的代码；协作者应小步解释、提示和验证，不应一次性代写大块功能。

## 远期设想停车区（当前冻结）

- 以下内容只作为未来可能性记录，在作者明确重新开启前，不应据此扩展当前任务、浏览相关仓库、设计集成层或改变内核近期路线。
- Lisp/C++ 设想：以 Lisp/DSL 表达程序，经语义 IR、可达性分析和程序级特化后生成普通 C++；只有程序实际需要的动态特性进入程序专属运行时。
- 可能在未来考虑接入作者的其他项目：`https://github.com/wsd32588/D-li-ne.git` 和 `https://github.com/wsd32588/TextEditor.git`。目前尚未评估内容或集成方式。
- 当前唯一主线是完成 TinyOS 最基础的内核机制。U-mode、syscall、ELF 装载等应按操作系统学习进度自然引入，而不是为了远期应用设想提前赶工。

## 当前代码结构

- `kernel/entry.S`：设置 16 KiB boot stack，进入 `kernel_main`。
- `include/trap_offsets.h`：C 与预处理汇编共用的 TrapFrame 偏移和总大小定义。
- `kernel/trap_entry.S`：按共享偏移在当前栈上保存 31 个非零通用寄存器中的 30 个（`sp` 由 frame 地址隐式表示）及 4 个 trap CSR，共 272 字节；恢复后执行 `sret`。
- `kernel/trap.c`：处理 S timer interrupt；未知 trap 会打印诊断并停机。
- `kernel/timer.c`、`kernel/sbi.c`：读取 `time`，通过 SBI TIME 扩展设置下一次定时器。
- `kernel/pmm.c`：以链接后的内核末尾为起点、Device Tree 地址为保守上界，提供按 4 KiB 对齐的物理页分配；顺序区域之外还有 intrusive free list，分配时优先复用释放页并在返回前清零。
- `kernel/vm.c`：提供 Sv39 PTE 编解码、canonical 地址判断、三级 4 KiB 页表遍历以及单页映射/查询；中间页表由 PMM 分配。
- `test/`：宿主机测试入口及 PMM 测试；使用页对齐宿主虚拟内存验证不依赖 RISC-V 特权指令的算法。
- `kernel/sched.c`：最多两个任务、每任务 4 KiB 栈、READY/RUNNING/DEAD 状态、轮转抢占。
- `drivers/uart.c`：直接访问 QEMU virt 的 16550 UART MMIO。
- `linker.ld`：把内核链接到 `0x80200000`，导出页对齐的 `__kernel_start` 和 `__kernel_end`。

## 最近验证

2026-09-26 在 `/home/asus/tinyos` 执行：

- `make -B`：从当前源码强制重编译成功，所有 C/汇编文件均无编译警告；链接器报告 `kernel.elf` 含 RWX LOAD 段。
- 使用与 Makefile 相同参数把所有 C/汇编源文件隔离编译到临时目录：成功，额外启用 `-Wpedantic` 后也没有警告。
- 强制重编译后执行 `timeout 6s make qemu`：成功进入 OpenSBI 和内核；看到任务 A 输出、定时器 tick、任务 B 输出 10 次后返回、随后 A 继续运行。退出码 124 来自 `timeout` 主动终止 QEMU，不是内核失败。
- `kernel.elf` 是 RV64 ELF，入口为 `0x80200000`；当前只有一个覆盖 `.text`、`.rodata`、`.eh_frame` 和 `.bss` 的 RWE LOAD 段。
- 在 `task_a()` 中加入 `task_yield()` 后，用 QEMU GDB stub 和 `gdb-multiarch` 单步验证：进入函数时 `a0=0x41`，执行 `li` 后 `a7=1`；ECALL 返回到了下一条指令，`mcause=9`、`scause=0`，串口额外输出一个 `A`，且没有 TinyOS 的 `[ecall]` 日志。结论是该 ECALL 进入 OpenSBI，并被 legacy SBI extension 1 当作 console putchar 处理。
- 已把本次实际使用的双终端调试流程整理到 `DEBUGGING.MD`；作者下一步应亲手重复关键命令，避免只依赖 AI 调试。
- Makefile 已加入 `-MMD -MP`、自动包含 `.d` 文件，并让 `clean`/`sync` 处理或排除 `.d`。强制构建生成了全部依赖文件；触碰 `include/trap.h` 后，`make -n` 只计划重编译 `kernel/main.o`、`kernel/trap.o`、`kernel/sched.o` 并重新链接。实际增量构建和随后 6 秒 QEMU 回归均通过。
- 已添加 `.gitignore`，忽略 `*.o`、`*.d` 和 `kernel.elf`；用 `git check-ignore -v` 验证了内核和驱动构建产物均命中规则。文件仍保留在磁盘，只是不再污染 Git 状态。
- `task_create()` 现在会在修改任务表前拒绝空入口或已满的任务表，`kernel_main()` 也会检查两次创建结果并在失败时停机。正常路径重新构建和 QEMU 回归通过；临时传入空函数指针后，只输出创建失败信息，没有初始化定时器、启动调度器或运行 A/B，随后已恢复真实入口并无警告重建。
- 已移除当前无效的 S-mode `task_yield()`/ECALL 调度接口及 handler 分支；`rg` 验证内核和头文件无相关残留。以后进入正确的 U-mode syscall 或另行设计 kernel task 同步切换时再重新引入。
- `trap.h` 已为 TrapFrame 总大小以及关键字段偏移加入编译期断言；`trap.c` 的 `SCAUSE_CODE_MASK` 拼写已修正。UART 发送现在会轮询 16550 LSR 的 THRE 位。全量构建和 6 秒 QEMU 回归通过。
- QEMU 中出现一次 `[task] task returned` 是预期行为：有限循环的 `task_b()` 返回到 `task_trampoline()` 后打印该日志，随后 `task_exit()` 标记 DEAD 并等待 timer；无限循环的 `task_a()` 不会走到这里。
- 上述日志已改成更准确的 `[task] entry returned`。相关拼写、错误路径、UART 循环和尾随空格已整理；`git diff --check` 无输出，全量构建及 6 秒 QEMU 回归通过。
- `scheduler_start()` 现在以 `int` 返回启动失败，调用方会处理失败并停机。所有任务结束时，timer 调度路径会打印一次 `no runnable tasks`、调用 `timer_stop()` 清除 `sie.STIE` 并进入终止循环，不再恢复 DEAD 任务。
- 临时把 A/B 都改成有限循环后，QEMU 依次看到两个 entry 返回和一次 `no runnable tasks`，之后不再出现 tick。GDB 在终止循环中确认 `sie=0x0`（调用前为 `0x20`）且 `scause=0x8000000000000005`。随后已恢复无限 A，重新构建与 6 秒正常回归通过。
- TrapFrame 的 34 个字段偏移和 272 字节总大小已集中到 `include/trap_offsets.h`；`trap_entry.S` 不再直接写保存/恢复偏移，`trap.h` 对每个 C 字段都做编译期断言。故意把 `a0` 偏移从 64 改为 72 时，构建按预期失败在 `trap_frame.a0` 静态断言，随后已恢复。
- 修改共享偏移头后，依赖文件会让 `main.c`、`trap.c`、`sched.c` 和 `trap_entry.S` 全部重编译。最终反汇编确认栈帧仍为 `-272/+272`，`a0=64`、`sepc=240`、`sstatus=248`；全量构建、`git diff --check` 和 6 秒 QEMU 调度回归均通过。
- Makefile 已把 QEMU 内存显式固定为 128 MiB。QEMU monitor 验证 RAM 为 `[0x80000000, 0x88000000)`；OpenSBI 传入 `hart_id=0`、Device Tree 地址 `0x87e00000`。链接器导出的内核范围为 `[0x80200000, 0x80208000)`。
- 最小 PMM 已能从 `__kernel_end` 起顺序分配并清零物理页。QEMU 中前三次分配返回 `0x80208000`、`0x80209000`、`0x8020a000`，相邻地址相差 `0x1000`，分配后报告 `0x7bf5` 个可用页；构建和 `git diff --check` 通过。释放和重复释放检测尚未实现。
- PMM 已加入 intrusive free list。释放 `0x80209000` 后可用页增加 1，下一次分配优先返回同一地址，计数恢复为释放前的 `0x7bf5`。2026-09-27 强制重编译和 5 秒 QEMU 回归通过，定时器抢占与任务返回仍正常。当前释放会验证空指针、页对齐、下界以及是否超过顺序分配高水位，并通过线性扫描拒绝重复释放；尚无并发保护。
- PMM 临时测试已从 `kernel_main()` 移到 `test/`，内核启动只保留初始化及可用页日志。`make test` 用宿主机分配的 32 MiB 页对齐虚拟内存完成基本边界检查、清零/复用/重复释放检查，以及固定随机种子的 20,000 次分配释放压力测试；全部通过。随后 `make -B`、`git diff --check` 和 5 秒 QEMU 回归通过，QEMU 初始化报告 `0x7bf8` 个可用页并正常进入调度。
- clangd 21 与 Bear 3.1.6 已安装。编译数据库必须在同一次 Bear 捕获中执行交叉内核构建和宿主测试构建；否则后一次宿主数据库会遗漏 `main.c`、trap、scheduler 和 UART。`make compdb` 现会捕获两套构建，当前数据库有 15 条记录。不要为 clangd 启用 `--query-driver`：项目使用软浮点 `lp64`，而 Ubuntu 的 RISC-V glibc 交叉头只带 `lp64d` stubs，查询驱动会错误引入 glibc 并报告缺少 `gnu/stubs-lp64.h`。clangd 可从交叉编译器名字自动识别 RISC-V 目标，并使用自己的 freestanding 标准头；宿主测试仍按 `/usr/bin/cc` 参数解析。
- 2026-09-29：Sv39 纯逻辑已实现并通过宿主测试。覆盖 PTE 编解码、canonical 地址、VPN 提取、非法 PTE、重复映射、页内偏移，以及 4096 个页面的确定性映射/查询；`make -B test` 无警告且全部通过。
- 内核已用真实 PMM 构造但尚未激活恒等映射页表：映射内核 `[__kernel_start, __kernel_end)`、PMM 区域 `[__kernel_end, device_tree)` 和 UART 页面 `0x10000000`。根页表位于 `0x80208000`，软件查询确认关键地址 VA==PA。
- 构造页表后执行 `make -B` 成功，唯一链接警告仍是 RWX LOAD 段；8 秒 QEMU 回归继续看到 timer tick、B 返回和 A 继续运行。当前仍为 `satp.MODE=Bare`，该回归只证明建表没有破坏现有内核，不代表硬件分页已启用。

已验证定时器抢占及“B 返回后由下一次 timer tick 换出”的路径；无效的 S-mode `task_yield()` 接口已移除。

## 重要代码审查结论

### 1. 已移除的 ECALL 调度路径为何错误

任务初始 `sstatus` 设置了 `SPP=1`，所以 `sret` 后任务仍在 S 模式。`task_yield()` 在 S 模式执行 `ecall`；本次 OpenSBI 输出的 `MEDELEG` 为 `0x0000000000f4b509`，其中 cause 9 对应位没有委托给 S 模式。因此该 ECALL 会进入 M 模式/OpenSBI，而不是本项目的 `stvec`，`trap_handler()` 中的 `EXCEPTION_ECALL_S == 9` 分支不会按预期执行。

现有演示没有暴露此问题，因为任务从未调用 `task_yield()`；任务返回后调用的 `task_exit()` 直接写任务状态并等待 timer interrupt，也没有执行 ECALL。

此外，`task_yield()` 把 `a7` 设为 1。当前 OpenSBI 声明支持 legacy SBI，而 legacy extension 1 是 console putchar，因此这个 ECALL 可能被 OpenSBI 当作旧式字符输出调用，而不是调度请求；`a0` 又没有为该用途初始化。最小实验应观察“没有进入本项目 `[ecall]` 日志、任务也没有按 yield 切换”这一现象。

下一步应先做架构选择，不要直接堆补丁：

- 保持任务在 S 模式：把 yield/exit 设计成内核内部调度入口，并明确如何安全保存同步调用点的上下文；或
- 让任务进入 U 模式：用 U-mode ECALL（cause 8）实现真正的系统调用，同时补齐输出等服务的权限边界。

这应由作者作为下一阶段练习，在修改前先画清楚控制流。

### 2. 仓库状态需要保护

- 仓库已有首个提交，`main` 跟踪 `origin/main`。
- 当前 Sv39 阶段仍同时存在暂存区和工作区改动；不要擅自恢复、覆盖、重新暂存或清理。
- `.o`、`.d` 和 `kernel.elf` 已由 `.gitignore` 排除；不要因为它们不再显示在状态中就把 `make clean` 当作普通验证步骤。

### 3. 头文件自动依赖和 TrapFrame 共享布局已补齐

Makefile 现在会为 C 和预处理汇编生成并包含 `.d` 依赖文件。修改 `include/trap_offsets.h` 后，普通 `make` 已验证会重编译所有直接或间接包含它的 C 对象及 `trap_entry.S`。

TrapFrame 的汇编偏移和总大小以 `include/trap_offsets.h` 为单一来源；C 结构体通过 `offsetof` 和 `sizeof` 静态断言与其保持一致。修改布局时必须同时理解字段语义，但不应再在汇编中手写数字偏移。

### 4. 已发现但尚未触发的边界情况

- `task_create()` 的空入口和创建失败处理已经补齐并验证；`scheduler_start()` 的失败返回也已有调用方处理。
- 全部任务 DEAD 时目前进入关闭 timer interrupt 的永久终止循环。这对当前静态演示足够，但还不是真正的 idle task；以后需要设备唤醒、动态创建任务或电源管理时应重新设计。
- 链接脚本生成单个 RWE LOAD 段。分页和内存保护尚未实现，所以当前不阻塞学习，但后续应把代码、只读数据和可写数据权限分开。

### 5. 后续基础练习

按优先级建议：

1. 把 RISC-V CSR 操作隔离到不参与宿主测试的 `kernel/vm_arch.c`，实现最小 `satp` 激活与读取接口。
2. 在 trap 入口已经初始化、timer 尚未启动时启用现有恒等映射；QEMU 验证 `satp.MODE=8`、UART、trap、timer 和调度仍工作。
3. 分页稳定后再用真实缺页访问验证 instruction/load/store page fault，并随后拆分 ELF 段权限。
4. 更晚再处理真正的 idle task、每任务地址空间、U-mode 和 ELF 装载。

## 常用命令

```sh
cd /home/asus/tinyos
make
make test
make compdb
make qemu
timeout 6s make qemu
git status --short --branch
```

不要把 `make clean` 当作普通验证步骤；它会删除当前构建产物。

## 建议的下一次会话目标

下一阶段只做 Sv39 的硬件激活：将 `satp`/`sfence.vma` 放在单独的 RISC-V 文件中，不加入宿主测试；利用现有恒等映射让写入 `satp` 后的下一条指令、当前栈、trap 入口和 UART 地址保持不变。确认 QEMU 调度回归及 GDB 中 `satp.MODE=8` 后，才进入真实 page fault 和段权限验证。当前不要展开 U-mode、ELF、C++ runtime 或其他仓库集成。

完成本次 `task_yield()` 实验后，必须整理一份作者可独立重复的 QEMU + `gdb-multiarch` 调试流程，并让作者实际操作一次。至少包括启动 `make debug`、连接 `target remote :2600`、符号/地址断点、`continue`、`stepi`/`nexti`、反汇编、查看通用寄存器与 RISC-V CSR、检查调用栈，以及安全退出 QEMU/GDB。目标是让作者能够脱离 AI 复现本次判断。
