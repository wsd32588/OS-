# Bare、Sv39、特权切换与页表生命周期

整理日期：2026-10-24。范围：rCore学习实现BATCH15—19，B19已通过作者功能验收。
本篇用于复习当前机制，不引入新任务。章节全貌见[第四章总结](CHAPTER04_ADDRESS_SPACES.md)。

## 1. 先分开四种状态

讨论“切换”时，先明确改变的是哪一项。

| 状态 | 保存在哪里 | 回答的问题 | 本项目中的改变方式 |
| --- | --- | --- | --- |
| CPU特权级 | 处理器当前执行状态 | 当前是U、S还是M？ | trap、sret、mret等 |
| 地址翻译规则 | satp | 地址直接使用，还是按哪张根表翻译？ | csrw satp |
| 执行位置与访存地址 | PC、sp和其他通用寄存器 | 接下来执行哪里、访问哪里？ | 跳转、寄存器恢复、普通计算 |
| 物理页所有权 | Rust对象及分配器元数据 | 谁保证页仍有效，何时能回收？ | 构造、移动、Drop等 |

把satp切到用户表，不会自动让CPU进入U-mode；CPU进入S-mode，也不会自动切到内核表。
改变地址翻译规则，不会自动修改PC或sp的数值；恢复Bare也不会自动释放页表。

本项目中没有使用虚拟化扩展；以下讨论限于当前单hart、S/U执行环境。

## 2. Bare究竟做什么

Bare是satp.MODE选择的一种模式。它让当前S/U地址访问不经过分页页表翻译，地址数值直接作为物理地址使用。
在我们使用的RV64环境中，选择Bare时将整个satp写成0，其余字段也必须为0。

```assembly
csrw satp, zero
```

这条指令只改变地址翻译配置。寄存器、栈内容、ELF数据、分配器元数据和已经创建的页表都还留在内存里。

假设sp为0x80230000，CPU执行：

```assembly
ld a0, 0(sp)
```

Bare下直接访问物理地址0x80230000；Sv39下先将它作为VA，按当前页表翻译为PA。

| 当前模式及映射 | 地址处理 | 是否检查PTE权限 |
| --- | --- | --- |
| Bare | 地址0x80230000直接表示同数值PA | 否 |
| Sv39恒等映射 | VA 0x80230000经页表得到PA 0x80230000 | 是 |
| Sv39其他映射 | VA 0x80230000经页表得到映射指定的PA | 是 |

恒等映射只是页表中的VA与PA数值相同，仍要遵守分页权限。Bare不需要构造这条映射。
Bare下也不会因不存在某条PTE而产生页错误；但不存在的物理内存、PMP限制等仍可能导致访问失败。

## 3. satp里放什么

当前RV64实现使用：

```text
63          60 59              44 43                       0
+-------------+------------------+--------------------------+
|    MODE     |       ASID       |        根页表 PPN        |
+-------------+------------------+--------------------------+

Bare：MODE=0，其余字段也写0，即整个satp=0。
Sv39：MODE=8；本项目ASID=0；PPN为根表物理地址除以4096。
```

[PageTable::token](../../experiments/rcore-learning/os/src/mm/page_table.rs)只是计算配置数值：

```rust
(8usize << 60) | root_ppn
```

得到token不等于启用分页。必须由有权限的代码把这个数写进satp，硬件才按它选择地址空间。
根表使用物理页号，因此硬件能从它开始查表，不需要先通过同一张表“翻译根表自己的地址”。

## 4. 地址的数值不会随切表一起搬动

假设用户表存在如下映射，PA仅用于说明：

```text
VA 0x40001234 → PA 0x80445234
```

Sv39下访问前者，会经翻译到达后者。改成Bare以后，地址数值0x40001234直接代表同数值物理地址，
不会继续访问0x80445234。0x40001234也不在本项目配置的RAM区间0x80000000…0x88000000内。

同样，假设代码正在高跳板地址0xfffffffffffff080执行，页表将其翻译到内核镜像中的跳板PA。
如果直接清零satp，下一条PC仍是高地址，不会自动变成那页的低物理地址。
所以恢复Bare之前，必须先回到能够按直接物理寻址继续执行的位置。

当前内核对低地址代码和启动栈建立了恒等映射。这使低地址处Sv39→Bare时，PC/sp仍访问同一份物理存储。
这依赖已经核实的布局，不能推广成“任何地址都能随时关闭分页”。

## 5. Bare与U/S模式互相独立

| CPU特权级 | 翻译模式 | 本项目中的例子 |
| --- | --- | --- |
| S | Bare | 启动阶段、临时演示结束后 |
| U | Bare | 原hello/hello_peer两个旧应用 |
| S | Sv39 | B15内核分页探针、B18两表往返、B19的trap桥 |
| U | Sv39 | B19中真正执行的image_demo |

Bare下的U-mode仍然是用户特权级，不能任意执行S-mode专用指令。
但它缺少通过PTE的U/R/W/X位进行的页级隔离，不能把此前的Bare运行等同于完成独立用户地址空间保护。

在本项目的委托配置下，U-mode执行ecall后，硬件记录sepc、scause等状态、更新sstatus，
切到S-mode并跳向stvec。此时satp、sp和其他通用寄存器仍保留原值。
高trap入口执行的最初几条指令，仍要依靠用户表提供映射。

## 6. 为什么用户表中有U=0的跳板和现场页

B19用户表不仅映射用户ELF和用户栈，还包含两页供陷入后的S-mode使用：

| VA | 对象 | 权限（略去V） | 数据页的归属 |
| --- | --- | --- | --- |
| 0xfffffffffffff000 | 公共跳板代码 | R/X/A，U=0 | 内核镜像中的静态代码 |
| 0xffffffffffffe000 | 分页用户现场 | R/W/A/D，U=0、X=0 | PagedSession中的独立FrameTracker |

跳板页支持刚进入S-mode后的取指；现场页支持在切到内核表前保存寄存器。
两页均U=0，用户代码无法直接执行跳板或修改kernel_satp、kernel_sp、trap_handler。

当前框架也把同一现场页映射进内核表，以覆盖建立高trap入口与切表之间的短暂窗口。
共享映射不复制物理页，也不复制所有权。两张页表只各自保存PTE，唯一FrameTracker继续保活现场页。

## 7. B19的完整往返顺序

```text
① 启动Rust：S-mode + Bare
   保存8项CSR，构造两张表、ELF数据页和分页现场

② S-mode + 内核Sv39表
   保持低PC/启动栈可访问
   128字节续点帧保存ra/gp/tp/s0…s11，地址写入kernel_sp

③ 高跳板：仍为S-mode
   切用户表 + SFENCE
   sp指向高现场，恢复sstatus/sepc及整数寄存器
   最后恢复用户sp，sret进入U-mode

④ U-mode + 用户Sv39表
   用户_start → 清BSS → main → exit(0) → ecall

⑤ 高trap入口：S-mode + 原用户Sv39表
   交换sp/sscratch，保存用户寄存器和CSR
   读取kernel_satp/kernel_sp/trap_handler
   切内核表 + SFENCE，然后换回内核sp

⑥ 高跳板 → 低返回桥：S-mode + 内核Sv39表
   高跳板设置低fatal入口，再跳低返回桥恢复内核ra/gp/tp/s0…s11
   ret回原Rust调用

⑦ 低地址Rust：恢复Bare + SFENCE，恢复原CSR
   检查保存的退出现场；销毁本次两个空间和现场页
   继续原来的Bare双应用流程
```

这里有两份不同的“现场”。296字节PagedTrapContext保存用户整数现场及内核返回路线；
128字节启动续点帧保存普通内核调用需要保留的寄存器。旧调度器的272字节TrapContext仍是另一条Bare路径。

用户现场的x[2]保存当前用户sp；kernel_sp指向启动续点帧；该续点帧中的ra返回内核调用处。
恢复用户sp放在最后，是因为此前的ld仍以sp作为用户现场的寻址基址。

本批第一次用户trap就结束演示。exit不再恢复该应用，因此无需推进sepc。
未来处理会返回用户的普通系统调用时，才需要跳过ecall并恢复其返回值；本篇不提前实现这条新路径。

## 8. SFENCE与Bare切换的关系

Sv39与Bare之间的MODE改变立即生效，不必等SFENCE才改变地址解释规则。
但写satp本身不保证页表更新与后续硬件查询的顺序，也不自动清除旧地址翻译缓存。

```assembly
csrw satp, zero
sfence.vma zero, zero
```

第一条恢复直接物理寻址；第二条在当前hart完成相应地址翻译同步并使旧缓存翻译失效。
同步还涉及可能已经开始的页表访问，因此不能把它只理解成“为了下次查询更快”。
本项目所有空间都使用ASID 0，每次切根后采用全量SFENCE，先保证简单、明确的正确性。

三个操作需要分开：

| 操作 | 负责什么 | 不负责什么 |
| --- | --- | --- |
| csrw satp | 选择地址翻译模式与根表 | 不释放物理页，不自动改PC/sp |
| sfence.vma | 页表访问和地址翻译缓存同步 | 不清空普通数据内容，不执行Rust Drop |
| fence.i | 让本hart后续取指看见此前写入的机器码 | 不更换根页表，不代替SFENCE |

## 9. 为什么先恢复再销毁

若satp还指着将被销毁的根表，页表所有者Drop可能把表页交回分配器。
即使已有TLB命中暂时维持执行，硬件仍可能在翻译缺失时重新查表；回收后的页又可能用于其他对象。
不能寄希望于“等刷新TLB之前都安全”。

当前顺序为：

```text
回到内核表及低PC/内核sp
→ 恢复原Bare并完成SFENCE
→ 结束ActiveRun对PagedSession的独占借用
→ 检查保存的退出现场
→ drop(session)：先销毁两个根表，再释放现场页
```

恢复Bare后，两个页表对象仍真实存在；它们的内存直到后续Drop才归还。
恢复Bare只让当前硬件不再按它们进行新的分页访问；同步和所有权次序共同保证回收安全。

本批临时使用23页：内核表11页、用户表5页、用户ELF/栈6页、现场1页。
共享跳板已在内核镜像中，启动续点已在原栈中，二者不新增页池分配。
验收确认全部销毁后free=3008、in_use=0、堆恢复原基线。

## 10. 为什么以后不一定恢复Bare

[ActiveRun::drop](../../experiments/rcore-learning/os/src/mm/paged_user.rs)写回的是进入前保存的satp。
本演示要求进入前satp=0，所以恢复结果是Bare，随后原双应用按旧流程执行。

长期运行的分页内核可以保留一张始终有效的内核表。用户退出后切回这张内核表并同步，再回收用户空间，
无需每次退出都关闭分页。关键条件是当前执行所需的代码、栈、数据和异常入口仍有有效映射，旧空间已停止使用。

截至本篇整理时，B19完成的是单ELF临时分页执行闭环。双应用调度器仍使用Bare；
没有新增分页任务调度，也没有开始下一批实现。

## 11. 源码、证据与来源

- [BATCH19任务、作者原答及修正版](../../experiments/rcore-learning/BATCH19.md)。
- [paged_user.rs](../../experiments/rcore-learning/os/src/mm/paged_user.rs)：对象归属、CSR守卫、启动演示与回收。
- [paged_user.S](../../experiments/rcore-learning/os/src/mm/paged_user.S)：续点、恢复与高trap入口。
- [trampoline.rs](../../experiments/rcore-learning/os/src/mm/trampoline.rs)：公共页地址与B18的S-mode探针。
- 作者实现验收：学习目录`.cache/batch19-author-review-r7e0xtq3/`；本次仅整理已有结论，未重跑QEMU。
- [RISC-V Supervisor规范](https://docs.riscv.org/reference/isa/v20240411/priv/supervisor.html)：satp、Bare、trap与SFENCE。
- [Zifencei规范](https://docs.riscv.org/reference/isa/v20240411/unpriv/zifencei.html)：机器码写入后的本hart取指同步。
