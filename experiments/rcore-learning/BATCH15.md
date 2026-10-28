# BATCH15：临时启用 Sv39 与安全恢复 Bare

2026-10-20：作者已完成两处核心并移除 guard，独立审查未发现功能必修问题，既定P0通过。
实际 timeout 8s make run 完成启用、访存、拒绝、恢复及原双应用运行，QEMU正常退出0。
随后只删除 main.rs/paging.rs 两条未使用导入，无警告构建通过，核心实现未改。
记录 `.cache/batch15-author-review-vx6sj7a4/`；以下保留整批任务与早期预检，第9节三题已集中复核并记录原答/修正版。

## 1. 本批目标与范围

BATCH14 构造了内核页表，但 CPU 仍在 Bare 下运行。现在要真正把根页表交给 MMU，
在 S-mode 下继续执行代码、访问栈、堆和只读数据，再安全退出这个临时分页作用域。

完整路径是：

```text
构造并持有 MemorySet
  → 核对完整内核恒等映射
  → 安装临时 S-mode 故障入口
  → 写 satp，执行 sfence.vma
  → 读取 satp，真实取指与 volatile 访存
  → 拒绝重复进入，保持当前硬件与资源状态
  → 恢复 satp=0 并刷新，恢复原 stvec
  → 销毁探针和 MemorySet
  → 原有 Bare 用户应用、timer、退出继续运行
```

这次确实开启分页，但只在内核启动的一个受控作用域内开启。
原应用固定装载窗口没有 U 映射，静态用户栈也仍在内核 RW/U0 区域，
不能把这张内核表一直开着就直接进入旧用户任务。
持续启用分页、用户地址空间、ELF 和切换时的 trap 布局随后再实现。

## 2. satp 保存的是什么

RV64 的本批字段如下：

```text
bits[63:60]   MODE：8 表示 Sv39，0 表示 Bare
bits[59:44]   ASID：本批固定为0
bits[43:0]    根页表的物理页号 PPN
```

因此 token 的数值为：

```text
(8usize << 60) | 根页表PPN
```

这里填的是根页号，不是根表物理字节地址，也不是某个数据页的 PPN。
例如根表 PA=0x80440000，则 PPN=0x80440，对应 token=0x8000000000080440。
这个地址仅用于说明编码，实际根表位置由分配器决定。

PageTable::token 只计算数值，不写 CSR，也不会改变运行模式。
MemorySet::token 是已提供的转发方法。

## 3. 为什么还需要 sfence.vma

写 satp 改变 MODE 会立即改变地址翻译方式，SFENCE 不是开启分页的开关。
但写 satp 本身不保证页表写入的可见顺序，也不负责清除旧的地址翻译缓存。
本批 ASID 始终为0、只有一个 hart，每次切换后都使用：

```assembly
csrw satp, <token寄存器>
sfence.vma zero, zero
```

第二条指令同步本 hart 的地址翻译，范围为所有 VA/ASID。
它不是清空 CPU 的普通数据缓存，也不是用于同步新指令内容的 fence.i，
更不代表已经向其他 hart 发出 TLB shootdown。

两条放在同一个 asm! 中，使用 options(nostack)，不写 nomem、readonly 或 pure。
它们必须在编译器看来可能影响内存，不能让先前建立表项的操作被移到启用后面。

回到 Bare 使用整个 satp=0，之后仍执行全量 sfence.vma。
当前 PC/SP 在恒等映射下数值不变，因此下一条指令和当前栈可跨过这次切换继续使用。
这个前提不能照搬到任意高地址内核或任意页表切换。

## 4. 表的生命周期与私有守卫

已提供私有 ActiveKernel<'a>，其中保存 &MemorySet 和原 stvec。
正常代码中，只要守卫还需要这个借用，就不能先销毁被借用的 MemorySet。
离开内部作用域时，守卫 Drop 依次：

1. 写 satp=0 并 sfence.vma；
2. 恢复原 stvec；
3. 守卫结束后，外层才销毁探针和 MemorySet。

MemorySet 的 Drop 仍只释放页表页和元数据；它不会自动操作 satp。
负责恢复硬件的是这个专门的作用域守卫，不能把两者混为一谈。

整个入口和守卫保持模块私有，unsafe 约束由封闭启动流程保证：
单 hart、尚未启动用户任务与 timer、SIE=0 且 sie=0、所有必要代码与数据恒等映射、
作用域期间页表不修改、守卫不泄漏。普通借用和 Drop 不能替代这些硬件前提。
本批不暴露一个可随意保存或遗忘的公共“安全激活”接口。

叶项 A/D 已预置，活动期硬件无需更新它们；活动期也不通过 Rust 修改页表字节。
软件读取/验证完整页表发生在启用前，表页拥有者一直保留到恢复 Bare 并刷新结束。

## 5. 为什么临时替换 stvec

现有 __alltraps 只支持 U-mode 陷入，第一步无条件交换 sp/sscratch。
当前仍在启动内核，不能用这条 U-trap 路径处理 S-mode 自己的页错误。
SIE=0 也不能阻止同步异常。

已提供 paging_trap.S 中的 fatal-only 入口，临时启用期间 stvec 指向它：

```text
不碰原sp和sscratch
  → 用a0/a1/a2保存scause/sepc/stval
  → 恢复Bare并sfence.vma
  → 建立独立8KiB应急栈
  → 调用Rust诊断，失败关机，不再返回
```

入口自身放在已恒等映射的 RX 代码中，应急栈在本项目 BSS 内。
8KiB 是本项目的容量选择，不是 RISC-V 的强制要求。
它用于改善故障诊断，不是可恢复的通用 S-trap，也不能挽救一张连故障入口都无法取指的任意坏表。
正常退出时恢复原 stvec，旧 U-trap、调度、sscratch 约定保持原样。

## 6. 文件与两处核心

已经接好：

- `os/src/mm/page_table.rs`：新增 token 方法，保留作者原来的编码/映射/查询函数体。
- `os/src/mm/memory_set.rs`：提供 token 转发，以及启用前逐页核对固定内核布局的 verify_kernel_mappings。
- `os/src/mm/paging.rs`：CSR读取、环境检查、私有作用域守卫、故障诊断、真实访存演示与状态核对。
- `os/src/mm/paging_trap.S`：临时 S-mode fatal-only 入口及应急栈。
- `os/src/mm/mod.rs`：BATCH14 演示后调用 paging::run_demo()?。

作者只需完成两处 TODO，再删除 paging.rs 顶部 compile_error。不改用户程序或原 trap 汇编，不加依赖。

### A：PageTable::token

位置：page_table.rs。返回 MODE=8、ASID=0、根PPN组合出的 usize。
根号来自 self.root_ppn.as_usize()，不要再左移12位，也不要将它误放到 PTE 的 bits[53:10] 布局。
PTE 编码与 satp 编码是两套不同格式。

### B：write_satp_and_flush

位置：paging.rs。函数已标 unsafe，但 edition2024 中，内部危险操作仍要放在显式 unsafe 块里。

使用 asm! 完成两条指令：

```text
csrw satp, {token}
sfence.vma zero, zero
```

token 使用命名输入 token = in(reg) token，options 仅用 nostack。
同一函数既接受 Sv39 token，也接受用于退出的0，不能把模式写死在这个函数内部。
它不分配内存、不销毁页表、不修改 stvec；这些顺序由已提供的调用流程负责。

## 7. 真实探针与可观察输出

先构造内核 MemorySet，再在开启前准备一个栈上 u64 和一个 Box<u64>。
进入活动作用域后，使用 write_volatile/read_volatile 写读两组哨兵，读取 rodata 中的静态字节，
并调用一个不内联的普通函数。代码执行和访存此时确实由当前 satp 对应页表参与翻译。

CSR核对要求 satp 完整值等于 token，MODE=8、ASID=0；SIE/sie 和 sscratch 保持原值。
这次不故意写只读页或执行无X页，因此不能把正常访问验证说成全部硬件权限异常都已测试。

新增输出应为：

```text
[paging] Sv39 active: MODE=8 ASID=0 stack/heap/rodata=true
[paging] reenter=Err state_unchanged=true
[paging] Bare restored: stvec/sscratch=true frames/heap=restored
```

守卫结束后核对 satp=0、原 stvec/sscratch 恢复，两个哨兵值仍在。
最后才销毁 Box 和 MemorySet，页数及堆统计回到演示前基线。
本批新增应急栈是永久静态BSS，不属于临时堆分配，随内核生命周期保留。

之后应继续看到原双应用各三轮、双向timer抢占、各exit0和allExited，默认系统QEMU主动退出0。
这证明临时切换没有破坏后面的 Bare 用户态执行，不能据此声称用户应用已经在 Sv39 下运行。

## 8. 验收范围、命令与状态

P0 必须，由协作者承担：真实启用/读取 satp、正常取指与栈/堆/只读数据访问，
恢复Bare及CSR/资源生命周期不变量；唯一拒绝路径为活动期再次 enter_kernel 返回Err，
且 satp/stvec/页数/堆占用不变。保留BATCH14检查和原双应用运行观察，不追加故障矩阵。

P1 选做：只读追踪私有守卫与fatal入口的不同结束路径，新增代码0行。
P2 故意触发权限错误、通用S-trap恢复、多hart/ASID、持久地址空间切换留到后续，不阻塞本批。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
timeout 8s make run
echo $?
```

早期保留guard的骨架预期构建失败；当前作者已完成两处并删除guard，功能及无警告构建已通过。
124表示timeout结束，不能算主动关机；不要使用旧二进制代替重建验收。

批前快照：`.cache/batch14-before-activation-fi607cym/`。
预检副本：`.cache/batch15-preflight-d29qonf8/`，只在副本补齐两处核心。
上述是早期预检；作者版本已另行验收，记录见页首目录，不能混淆两次验证。

## 9. 理解题与答案审查（2026-10-20）

以下保留作者原答，逐题记录审查与修正版。集中补充的讲解不记作作者已独立答对。

### 第1题：token、根PPN与硬件启用

问题：satp 为什么存根PPN而不是根PA？本批token中的MODE、ASID分别是什么，计算token是否已经启用分页？

**作者原答：**

> 因为还需要能放下其他位，开启token的使用MMU应该已经接管了内存并开启分页，因为map不能脱离MMU存在

**审查：** 注意到字段空间有限的方向正确，但需要区分页表构造、token数值和CSR状态。
map可以在Bare下写PTE，计算token不会自动让CPU使用这张表。

**修正版：**

根表按4KiB对齐，物理字节地址低12位为0，保存PPN即可定位根表。
本批satp布局为MODE4位、ASID16位、根PPN44位；MODE=8表示Sv39，ASID固定为0。
token只计算这三个字段组合的usize，不是一次硬件操作。

```text
map()            → 准备内存中的PTE
token()          → 算出要写入satp的数值
csrw satp, token → 选择Sv39及根表
sfence.vma       → 同步地址翻译
```

页表本身是内存中的数据结构，Bare下也能构造、修改和软件遍历。
写satp使相应的Sv39模式立即生效；随后sfence.vma负责页表更新与翻译缓存同步，完成本批切换流程。
不能理解成等sfence执行后才开始分页，这也是写satp后的指令本身就必须可取指的原因。

### 第2题：TLB缓存与释放前同步

问题：为什么写satp之后还要sfence.vma？退出作用域时，为什么必须先恢复Bare和刷新，才能释放表页？

**作者原答：**

> 刷新TLB，方便取放，而不用每次都必须页表查询，刷新和恢复需要首先去掉TLB中的内容，防止在释放之前还有程序查询使用

**审查：** 已注意到缓存可能残留旧翻译，但“缓存的加速作用”与“刷新让旧结果失效”需要分开。
还需明确先离开旧根表，再同步，最后归还页帧；仅清缓存却继续使用旧satp仍可能重新遍历旧表。

**修正版：**

TLB等地址翻译缓存复用查表结果，用于减少正常访问时的重复查询。
sfence.vma则同步页表更新并使相应旧翻译失效；后续访问会按需要重新建立缓存。
写satp本身不保证页表写入顺序和旧缓存处理，本批ASID0切换后采用全量sfence.vma。

本批销毁顺序为：satp=0停止使用旧表→sfence.vma完成同步→恢复原stvec→drop表页。
若仅刷新但satp仍指向旧表，CPU之后仍可能重新查它；若先释放，则表页可能在CPU还使用时被复用。
这里是单hart的本地同步，不代表多hart场景已经完成远端shootdown。

### 第3题：恒等映射、S-trap与恢复Bare

问题：当前指令地址和sp为何能跨过两次切换继续使用？为什么不能让旧U-trap处理启动期S fault，
又为什么本批要恢复Bare后才运行旧用户应用？

**作者原答：**

> pc只是记一个数，而sp更简单只是记当前栈顶，S-trap入口是为了解决在Umode无法捕捉内核页表和地址的情况而出来的一个专门负责内核部分的，Bare这个不知道

**审查：** PC/sp确实保存数值，但地址解释方式变化后，数值相同不保证访问同一位置。
临时入口的原因是当前异常来自S-mode、旧入口的栈交换约定不适用，不是U-mode能否查看内核地址。
恢复Bare的原因由本次讲解补充，不记作作者已经独立回答。

**修正版：**

启用Sv39后，指令地址和sp用于虚拟地址访问。当前代码有RX恒等映射，栈有RW恒等映射，
所以同一个数值仍到达原来的物理指令和栈字节；恢复Bare后也能在原数值处继续使用。
这依赖具体映射，不能推广为任何页表切换都无需处理PC/sp。

旧__alltraps假设来自U-mode，第一步无条件交换sp/sscratch。
启动内核自己在S-mode发生异常时不满足这个约定，故本批临时使用仍在S-mode运行的fatal-only入口，
不触碰旧栈或sscratch，先退回Bare，再用应急栈诊断并失败关机。

当前内核表未映射旧应用代码窗口，旧用户栈也没有U权限，不能直接保持这张表进入旧用户任务。
所以临时测试结束后恢复Bare，再沿用已经通过的旧应用流程；持续分页还需要准备用户映射和切换路径。

## 10. 来源与本批取舍

固定官方 ch4 `f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79` 的
[PageTable::token](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/mm/page_table.rs)、
[MemorySet::activate](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/mm/memory_set.rs)，
硬件规则参考 [Supervisor 规范的 satp 与 SFENCE.VMA 部分](https://docs.riscv.org/reference/isa/v20240411/priv/supervisor.html)。

官方本章已配合完整用户地址空间；本项目拆成先构造、临时启用验证、再接用户空间的步骤。
临时恢复流程和fatal-only入口是本项目接线，不声称直接来自官方完整实现。
