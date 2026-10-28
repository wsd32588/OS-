# 第二章总结：用户运行时与 Trap 往返

整理日期：2026-10-24。对应 [BATCH03](../../experiments/rcore-learning/BATCH03.md)、
[BATCH04](../../experiments/rcore-learning/BATCH04.md) 与
[BATCH05](../../experiments/rcore-learning/BATCH05.md)。原任务、作者回答与纠正记录继续保留。

这一章把“内核自己输出”推进为“应用以 U-mode 运行，通过内核请求输出，最后退出”。
核心成果是用户运行时、固定地址装载、两条应用栈、完整整数 Trap 现场与 U↔S 往返。
按作者选择，**未实现第二章顺序覆盖同一装载窗口的多应用批处理循环**，随后直接进入第三章
多应用常驻与调度。第二章核心路径完成，不等于官方本章全部扩展均已实现。

本文先按第二章完成时的单应用路径解释。2026-10-24 的源码已经增加第三章调度和第四章
临时分页演示：旧 `trap/` 路径仍服务 Bare 双应用，B19 的分页 ELF 使用另一套私有入口与现场。
不能把下文 272 字节现场、`sscratch` 约定直接套用到新的分页路径。

## 1. 应用的执行环境由谁提供

| 需求 | 谁负责 |
| --- | --- |
| 应用代码与用户 `_start` | `user/` 包独立编译、链接 |
| 把镜像放到入口对应地址 | 内核 loader |
| 初始用户 `sp` | 内核构造现场，汇编恢复到 CPU |
| 用户 BSS 零初值与调用 `main` | 用户库 `_start` |
| 将输出请求变为 `ecall` | 用户库 syscall 包装 |
| 请求是否合法、怎样提供服务 | 内核 trap/syscall |
| 结束当前应用或结束整个系统 | 内核策略决定；应用只有 `exit` 请求 |

用户库是一组适合本项目的小型运行时辅助接口：入口、BSS、输出、syscall、退出与 panic。
它与应用共同链接，不是另一个常驻进程，也不要求实现完整 libc/libc++。

源码入口：

- [user/lib.rs](../../experiments/rcore-learning/user/src/lib.rs)：`_start`、BSS、公开包装和用户 panic。
- [user/syscall.rs](../../experiments/rcore-learning/user/src/syscall.rs)：寄存器接口与 `ecall`。
- [user/console.rs](../../experiments/rcore-learning/user/src/console.rs)：格式化文本转 `write` 请求。
- [loader.rs](../../experiments/rcore-learning/os/src/loader.rs)：镜像、栈、现场初始化；当前版本已扩为双应用。
- [trap/context.rs](../../experiments/rcore-learning/os/src/trap/context.rs)、[trap.S](../../experiments/rcore-learning/os/src/trap/trap.S)：整数现场布局与保存/恢复。
- [trap/mod.rs](../../experiments/rcore-learning/os/src/trap/mod.rs)、[syscall.rs](../../experiments/rcore-learning/os/src/syscall.rs)：解释原因、分发服务；当前已含 yield、timer 和多任务退出。

## 2. 先生成应用，再装载，最后才执行

第二章应用链接到 `0x80400000`，应用窗口为 128 KiB。
独立 `make user` 只生成 ELF/BIN；既不会启动 QEMU，也不会证明应用已经在 U-mode 执行。
编译目标 `riscv64gc-unknown-none-elf` 规定架构与 ABI，不规定运行时 CPU 特权级。

`include_bytes!` 在编译时把用户 BIN 放进内核镜像，形成只读的源字节序列。
这份副本所在位置通常不是用户入口地址，所以 loader 仍需复制到固定应用窗口：

```text
用户 ELF → objcopy → 用户 BIN
    → include_bytes! 嵌入内核
    → 内核启动后检查输入
    → 清零固定应用窗口
    → 把 BIN 复制到 0x80400000
    → fence.i
```

装载前拒绝空镜像、超过窗口的长度、地址溢出和源/目标重叠，防止部分写入后才发现输入非法。
清空整个窗口去掉旧字节；应用自身 `_start` 仍履行用户 BSS 初始化责任。

`fence.i` 使本 hart 后续取指与先前对机器码内存的写入同步；它不负责虚实地址转换，
也不等于后续管理页表翻译的 `sfence.vma`。

这个 BIN 装载器不解析 ELF 的程序头，因此链接地址必须和实际装载地址配套。
第四章的 `UserImage::from_elf` 才负责按多个 `PT_LOAD`、不同权限与零填充构建地址空间。

## 3. 三条栈和一份初始现场

| 区域 | 第二章用途 | 初始状态 |
| --- | --- | --- |
| 启动栈 64 KiB | `_start` 到 `rust_main` 的启动调用链 | 汇编已设置 `sp`，正在使用 |
| 用户栈 8 KiB | 应用在 U-mode 中建立函数栈帧 | 静态数组已准备，用户尚未运行 |
| 应用内核栈 8 KiB | 应用 trap 后的汇编现场与 Rust handler 调用链 | 顶部先放初始 `TrapContext` |

此时没有 PMM 或页表；“准备用户栈”是保留普通 RAM 中的静态数组，并取其上边界。
初始栈顶是末尾后一字节的地址。运行后 `sp` 随函数调用改变，不能一直把它理解为区域最高边界。

以 `K` 表示应用内核栈上边界，初始现场放在 `K - 272`。普通 BSS 必须先清零，再写现场；
反过来会抹掉刚准备好的寄存器值。启动栈被排除在清零区间外，原因见[第一章](CHAPTER01_BOOT_AND_RUNTIME.md)。

### 现场格式是 Rust 与汇编共同遵守的协议

```rust
#[repr(C)]
pub struct TrapContext {
    pub x: [usize; 32],
    pub sstatus: usize,
    pub sepc: usize,
}
```

| 内容 | RV64 字节偏移 | 说明 |
| --- | --- | --- |
| `x[n]` | `n * 8` | 整数寄存器 xn 的内存副本 |
| `x[2]` | 16 | 用户 `sp` |
| `x[10]` | 80 | 用户 `a0`，参数或返回值 |
| `x[17]` | 136 | 用户 `a7`，syscall 编号 |
| `sstatus` | 256 | 特权和中断相关状态副本 |
| `sepc` | 264 | 将来恢复执行的位置 |
| 总长 | 272 | 32 个整数槽加两个 CSR 槽，且为 16 的倍数 |

`sp`、`a0`、`a7` 是 `x2`、`x10`、`x17` 的 ABI 别名，不是另一组寄存器。
现场采用 `x` 数组是为了统一按编号保存，`x[2]` 从来不是 `a0`。
`x0` 恒为零，不需要恢复，本实现将它的槽写零；包含 `tp/x4` 在内的其余整数寄存器均保存。

初始化时把整数槽置零，再填写 `x[2]=user_stack_top`、`sepc=entry`，清除保存状态的 SPP。
这只是写 RAM，不会改变此刻 CPU 的 `sp`、`sepc` 或模式。
第三章加入定时器后，当前旧现场构造器还清 SIE、设置 SPIE；不要把后续变化误记为 BATCH04
最初仅清 SPP 的实现已经包含全部中断初始化。

## 4. 特权级与地址翻译分别控制什么

```text
U / S / M：哪些指令与资源访问被允许，由谁处理异常和服务？
Bare / Sv39：CPU 发出的地址如何解释，是否经过页表翻译和页权限检查？
```

第二章在 Bare 下也能通过 `sret` 进入 U-mode。直接 `call` 用户 `_start` 只改变执行位置与 `ra`，
仍在原来的 S-mode。链接器也不会设置用户 `sp`；是恢复汇编加载保存现场后，`sret` 才真正交接。

| CSR 或字段 | 作用 |
| --- | --- |
| `stvec` | S-mode trap 入口与模式，本路径为 Direct |
| `sepc` | trap 位置；`sret` 使用的返回 PC |
| `scause` | 异常/中断原因；U ecall 为 8 |
| `stval` | 原因相关的附加信息，不是所有 trap 都表示访存地址 |
| `sstatus.SPP` | trap 前的模式；`sret` 据此选择返回 U 或 S |
| `sstatus.SIE` | 当前 S-mode 中断使能状态 |
| `sstatus.SPIE` | trap 保存的 SIE，供返回时恢复 |
| `sscratch` | 由软件赋予含义的 CSR，本旧路径用它配合换栈 |

SPP 不是随时查询当前特权级的通用字段；它保存供 trap 返回使用的信息。
清 SIE 只涉及 S-mode 中断，并不屏蔽所有特权级的中断或同步异常。
第二章还清 `sie`，不能仅凭内核此刻 SIE=0 就断言用户运行期间不会收到 S-mode 中断。

## 5. ecall 之后：硬件、汇编、Rust 各做什么

下面假设固件已将 U-mode ecall 委托到 S-mode，内核已安装入口。

### 硬件建立异常入口状态

当应用执行 `ecall`：

1. `sepc` 记录这条 `ecall` 的地址，`scause=8`，`stval` 按异常语义更新。
2. SPP 记录此前 U-mode；SPIE 保存 SIE，随后 SIE 清零。
3. CPU 进入 S-mode，PC 转向 `stvec` 指定入口。

硬件不保存整数寄存器，也不修改 `sp` 或 `satp`。进入 S-mode 不是自动获得一条新的内核栈。
在本章 Bare 下不涉及切表；第四章开启用户分页后，“`satp` 不变”成为入口必须额外解决的问题。

### 汇编先保住现场，再交给 Rust

用户运行时，旧路径约定 `sp=U`、`sscratch=K`，其中 U 为当前用户栈指针。

```assembly
csrrw sp, sscratch, sp   # sp=K，sscratch=U
addi sp, sp, -272       # 为现场预留完整空间

# 保存原始整数寄存器之后，才能借用 t0/t1/t2 读取 CSR。
csrr t0, sstatus
sd t0, 256(sp)
csrr t1, sepc
sd t1, 264(sp)
csrr t2, sscratch
sd t2, 16(sp)           # 用户 x2 来自 sscratch，而非已更换的 sp

mv a0, sp
call trap_handler
```

普通 Rust 函数的序言和运算可能覆盖寄存器，并依赖合法栈。
所以不能在保存前就让 Rust 使用用户现场；也不能把 `call` 随意换成不建立返回地址的 `jr`。

### Rust 解释原因与参数

旧路径中的可返回 syscall 按如下关系处理：

```rust
context.sepc += 4;
let id = context.x[17];
let arguments = [context.x[10], context.x[11], context.x[12]];
let result = crate::syscall::syscall(id, arguments);
context.x[10] = result as usize;
```

`ecall` 固定 4 字节，即使支持压缩指令也如此。硬件保存它自身地址，因此正常返回时要跳过它。
遗漏后会再次执行同一条 `ecall`；由于 `a0` 可能已经变成上一次的结果，不保证重复调用的参数还相同。
未知同步异常不能一律加 4 后忽略。

这里有两种不同的返回值：Rust handler 的真实 `a0` 返回现场指针；用户 syscall 的结果写入
RAM 中的 `x[10]`，等汇编恢复后才出现在用户 `a0`。不能把输出字节数当作现场地址返回。

## 6. 返回与首次进入共用恢复代码

`__restore` 接收现场地址，恢复 CSR 和整数寄存器，然后切回用户栈：

```assembly
mv sp, a0
ld t0, 256(sp)
ld t1, 264(sp)
ld t2, 16(sp)
csrw sstatus, t0
csrw sepc, t1
csrw sscratch, t2       # 暂存将要恢复的用户 sp

# 以当前 sp 为基址恢复普通整数寄存器，此时仍不换用户 sp。

addi sp, sp, 272
csrrw sp, sscratch, sp  # sp=用户 sp，sscratch=K
sret
```

访问现场时必须保留寻址基址。若先把 `sp` 改成用户值，后续 `ld ..., offset(sp)` 就会从用户栈
误读寄存器值。旧路径通过最后交换完成恢复；B19 的分页现场使用另一种末尾恢复方式，原理相同。

| 阶段 | `sp` | `sscratch` |
| --- | --- | --- |
| 用户执行 | 用户当前栈位置 U | 应用内核栈上边界 K |
| 入口交换后 | K | U |
| 预留并保存现场 | K−272 | U |
| Rust handler | 在现场下方建立自己的内核栈帧 | U |
| 恢复完并释放现场 | K | 要恢复的用户 sp |
| 最后交换并返回 | 用户 sp | K |

因此旧路径的 `sscratch` 不是“永远保存内核栈顶”；其含义随阶段改变。
`sret` 不接受 `sepc` 操作数，也不使用 `ra` 返回；它读真实 CSR 中的 `sepc` 与 SPP，
并更新 SIE←SPIE、SPIE←1、SPP←0 等状态。

首次进入用户态也可复用 `__restore`：恢复汇编只要求内存格式正确，不要求现场一定由一次真实
trap 生成。内核构造入口、栈和状态后，将其当作首次执行状态交给恢复代码即可。

## 7. write、exit 与 SBI 是两层接口

本章用户接口约定为：

| 服务 | `a7` | 参数 | 结果 |
| --- | --- | --- | --- |
| write | 64 | `a0=fd`、`a1=buffer`、`a2=length` | `a0` 返回写入长度或负错误值 |
| exit | 93 | `a0=exit_code` | 成功处理不返回原应用 |

`ecall` 不会按 `a7` 的数字自动选择内核或固件。执行时的特权级产生不同原因，再由委托和入口配置
决定处理方；服务方接管后才按自身 ABI 解读寄存器。SBI 的 EID/FID 与用户 syscall 编号不能混用。

用户 wrapper 使用 `inlateout("a0")`，明确同一寄存器既传参数又接结果；write 可能读取缓冲区，
不能把内联汇编错误标为 `nomem` 或 `pure`。

第二章 `sys_write` 先检查 fd=1；长度零直接成功；用 `checked_add` 防止末尾溢出；
要求整个缓冲区落在应用窗口或该应用用户栈内，然后才逐字节读取并输出。
这里的范围检查约束的是这个内核服务的读操作；Bare 应用仍缺少页表强制的完整内存隔离。
用户原始 VA 将来不能在任意内核页表下直接强转并读取，这属于第四章的新边界。

本实现输出时可以出现：

```text
U-mode write ecall → S-mode 内核 → SBI ecall → M-mode 固件
                      ← 固件返回 ←
用户继续执行      ← sret ←
```

这只是当前输出后端的选择。内核也可以直接驱动 UART；timer 设置、关机等也可能使用 SBI，
不能记成“只有 write 才会到 M-mode”或“所有 syscall 最终都必须到固件”。

应用正常结束的完整路径是：

```text
用户 _start → main() → 返回退出码
用户 _start → exit(code) → syscall 包装 → ecall
S-mode trap → sys_exit → 内核决定结束应用
```

所以 `rust_main` 末尾不必显式调用用户 exit。`main` 返回的是退出码，用户运行时负责把它变成请求。
BATCH05 单应用策略在内核 exit 中关机；当前旧双任务策略在 exit 中调度其他应用，最后才关机。
B19 单 ELF 演示则退出到保存的启动续点。这三种策略都不等于“用户 exit 自己有权关闭整机”。

退出应用不再恢复执行时，保存 `sepc` 是否加 4 不影响后续用户指令；旧 BATCH05 handler 统一先加 4，
B19 首次 exit 演示不需要推进它。可返回 write 与永不恢复的 exit 应按执行路径分别判断。

## 8. 本章已经证明什么，尚未提供什么

BATCH03 证明了独立用户 ELF/BIN、入口及 syscall 接线；没有以构建成功冒充 U-mode 运行。
BATCH04 证明了固定装载、现场和栈准备，超长镜像拒绝不修改目标；当时尚未执行用户代码。
BATCH05 才通过真实 U ecall、write 返回用户、main 返回后 exit 的闭环，以及一条坏指针 write
返回错误后仍能继续运行的拒绝路径。历史证据见 [PROGRESS](../../experiments/rcore-learning/PROGRESS.md)。
本次整理只核对文档与源码，没有重复运行这些验证。

本章实现没有浮点/向量现场管理、S-mode 嵌套 trap、完整页表隔离或通用进程生命周期。
第二章顺序覆盖单装载窗口的批处理管理器按选择略过。第三章实现两个应用常驻和协作/抢占调度；
第四章才逐步引入内存所有权、Sv39、用户 ELF、跳板和独立页表的真实 U-mode 执行。

## 阅读入口

- [第三章总结：多任务与定时器](CHAPTER03_TASKS_AND_TIMER.md)。
- [Bare、Sv39 与 Trap 的当前重点梳理](BARE_SV39_TRAP.md)：对照旧 Bare 路径和 B19 的分页用户返回路径。
- [特权级机制](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter2/1rv-privilege.html)、[用户应用](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter2/2application.html)、[实现特权级切换](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter2/4trap-handling.html)。
- [RISC-V Supervisor 规范](https://docs.riscv.org/reference/isa/v20240411/priv/supervisor.html)，结合 [sources.lock.json](../../experiments/rcore-learning/sources.lock.json) 中的固定教程来源阅读。
