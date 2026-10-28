# 第一批：从复位到 Rust 入口，再输出并关机

本批位于官方第 0 章和第 1 章。先理解启动链，再补核心代码。
只建立基本执行环境，内核还没有自己的用户程序、调度器或页表。
最终可见目标：固件 banner 之后出现一次 `[kernel] Hello rCore!`，QEMU 正常退出 0。

## 先理解为什么要这样做

在 Linux 中，装载器、栈、标准库、退出服务由已有系统提供。
裸机 Rust 程序没有这些支持，需要自己建立入口、内存布局与最小运行时。
本批借固件的 SBI 服务输出，先把“代码如何成为正在执行的内核”讲清楚。

**准备阶段**：Rust/Cargo + 链接脚本生成 ELF；objcopy 去掉 ELF 元数据形成 bin。
QEMU 在 CPU 运行前把固件和内核 bin 放入模拟 RAM；此处是 QEMU 预加载，
不是 RustSBI 替我们解析内核 ELF。链接地址必须与加载地址对应。

**执行阶段**：本批 QEMU virt 从复位地址 0x1000 开始，经固件进入 S-mode 内核：

```mermaid
flowchart LR
    R[QEMU 复位代码 0x1000] --> F[RustSBI M-mode 0x80000000]
    F --> E[内核 _start S-mode 0x80200000]
    E --> S[设置 sp 指向启动栈顶]
    S --> M[rust_main]
    M --> B[清除普通 BSS]
    B --> C[逐字节 SBI console 输出]
    C --> X[SBI system_reset 关机]
```

SBI 是 S-mode 调用 M-mode 固件服务的接口。sbi-rt 负责参数寄存器和 ecall 封装，
固件处理服务后返回；最后关机请求正常情况下结束模拟器。
它与后面 U-mode 应用调用 S-mode 内核的 syscall 属于不同层级。

| 文件/模块 | 输入与输出 | 为什么存在 |
| --- | --- | --- |
| `os/.cargo/config.toml` | 裸机目标、链接参数 → 正确架构的 ELF | 不链接宿主 Linux 运行环境 |
| `os/src/linker.ld` | 各输入段 → 内存地址与 sbss/ebss 符号 | 入口顺序、代码/数据布局、清零边界 |
| `os/src/entry.asm` | 固件交接后的 CPU → 有合法栈的 Rust 调用 | Rust 函数会用栈，不能假定固件栈可用 |
| `os/src/main.rs` | Rust 入口 → 清零、输出、关机 | 自己承担最小运行时责任 |
| `os/src/sbi.rs` | 一个字节/关机原因 → 固件调用 | 把硬件相关服务边界放在一处 |
| `scripts/cargo.sh`、`Makefile` | 源码 → ELF/bin → QEMU | 协作者负责的环境接线 |

## 阅读与 Rust 最小补齐

先读[第零章](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter0/index.html)的
OS 职责、系统调用与执行环境，再按第一章“执行环境 → 移除标准库 → 第一条指令 →
函数调用 → SBI 输出与关机”的顺序读。参考源码固定快照在 `.cache/reference/os/src/`；
已有完整 clone 仍保留，但不从 main 成品开始抄。

这批只需以下 Rust 知识，其他按后续章节补：

- `#![no_std]` 保留 core，不提供依赖宿主 OS 的 std；不等于不能用任何 Rust 语言功能。
- `#![no_main]` 不走常规 main 启动入口；导出 rust_main 供汇编调用。
- `fn ... -> !` 表示不返回，适用于内核入口、panic 与关机后的兜底。
- `mod`、`use`、`global_asm!` 分别组织模块、引用名字、把汇编纳入编译。
- 字节串是静态 u8 序列；`for &byte in b"..."` 按值取每个字节，无需分配堆内存。
- 链接器地址不是 Rust 自动分配的变量。`unsafe extern "C"` 声明外部符号；
  edition 2024 导出入口使用 `#[unsafe(no_mangle)]`。
- 原始指针不携带借用器能检查的有效范围，解引用要 unsafe；unsafe 不会自动证明安全。
  对 BSS 操作需明确范围、可写性、单 hart 和不触碰当前栈。

可先口头解释：`&[u8]` 与 `&mut [u8]` 各允许什么？`byte` 和 `&byte` 的类型有什么区别？
不要求先做完 Rust Book 或新增宿主测试项目。

## 作者本次整批完成的代码

环境接线与链接脚本已放好。你主要填写三个源文件；未完成时 `compile_error!` 阻止生成可运行镜像。
先写完下面 A/B/C，再移除该 guard。不要把 todo! 留在正常路径，它会进入 panic。

### A. 入口与启动栈：entry.asm

完成点：

1. 在 `.text.entry` 导出 `_start`，在第一条 Rust 调用前设置 sp 为 boot_stack_top。
2. 在 `.bss.stack` 保留 4096 × 16 字节，导出 boot_stack_lower_bound/top；栈顶保持 16 字节对齐。
3. 调用 rust_main；Rust 入口不返回，可在汇编调用后加停留循环作为兜底。

关键骨架（填操作数与栈声明，不直接复制成品）：

```asm
    .section .text.entry
    .globl _start
_start:
    # la <寄存器>, <符号>
    # call <导出的 Rust 入口>
    # 若意外返回，留在兜底循环

    .section .bss.stack
    .balign 16
    # 栈底标签 -> .space <字节数> -> 栈顶标签
```

栈向低地址增长；开始时 sp 指向上边界。Rust 函数保存寄存器/局部变量后，sp 才进入栈内。
本批只有一个 hart，不提前做多核启动。

### B. 清除普通 BSS：clear_bss

完成点：在任何依赖零初值的静态数据使用前，把 `[sbss, ebss)` 每个字节写 0。
边界来自给定链接脚本；不自己猜固定地址，不把符号位置的内容误当地址。

```rust
unsafe extern "C" {
    static sbss: u8;
    static ebss: u8;
}
// core::ptr::addr_of!(sbss) 获取链接符号地址；不需要读 sbss 的内容。
// 用整数地址的半开区间遍历，每个地址转为 *mut u8 并 write_volatile(0)。
// 为 unsafe 块写 SAFETY 注释，说明本批假定如何保证范围正确。
```

链接脚本先放 `.bss.stack`，再定义 sbss，因此启动栈不在清零区间。
这不是可选细节：clear_bss 本身正在使用启动栈，不能把自己的栈帧擦掉。
bin 不携带未初始化 BSS 的初始字节，所以运行时仍需履行清零责任。

### C. SBI 输出与正常/失败关机

在 sbi.rs 中实现两个已有函数：

- `console_putchar(byte: u8)`：调用 `sbi_rt::legacy::console_putchar(usize::from(byte))`。
  在这一次调用局部标注 `#[allow(deprecated)]`，解释为匹配本章固件；不全局屏蔽警告。
- `shutdown(failure: bool) -> !`：使用 sbi_rt 的 `system_reset`、`Shutdown`、`NoReason`、
  `SystemFailure`。false 选 NoReason，true 选 SystemFailure。若调用返回，留在无限循环；
  不正常返回，也不要用 panic/unreachable! 导致 panic handler 再次递归关机。

注意：NoReason 与 SystemFailure 是两个不同的结构体类型，不是同一枚举的两个值。
不能用 `let reason = if failure { SystemFailure } else { NoReason };` 合并为一个变量。
应把完整 system_reset 调用分别放进 if/else 分支，让每次调用使用对应的具体类型。

rust_main 依次 clear_bss、循环输出 `b"[kernel] Hello rCore!\n"`、shutdown(false)。
已有 panic handler 用 shutdown(true)，失败不能冒充正常完成。
暂不自行写 asm! SBI 寄存器包装、println 宏或 logger；这些在当前目标之后逐步解释。

## 必要验证与完成点

作者整批写完后集中交回，下面 **P0 由协作者运行**，不要求作者写脚本：

| P0 | 检查 | 预期 |
| --- | --- | --- |
| 构建/静态布局 | ELF header、符号和入口反汇编 | ELF64 RISC-V；entry 与 _start 为 0x80200000；先设 sp 后 call；64 KiB 栈、16 字节对齐；sbss 不含栈 |
| 正常启动 | 限时观察 make run | 一行目标 Hello；正常退出 0，不是 124；没有 panic/fault |
| 一条失败路径 | 协作者在临时副本中在输出前触发 panic | 不打印目标 Hello，以失败原因关机；RustSBI/QEMU 失败退出码由运行确认，不写死未经验证的数字 |

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
make inspect
make disasm
timeout 8s make run
```

手动退出 QEMU：Ctrl+A 后按 X。timeout 124 只表示观察超时，不能当作正常关机验收。
不执行根目录 make，也不运行 make clean。没有额外测试矩阵或持久化测试框架作业。
P1 选做：预测漏设 sp 或误把启动栈清零的后果，先口头解释即可（新增代码 0 行）。

## 收尾的三个理解小测

整批完成并验证后再回答，允许口头、画路径或追踪代码：

1. ELF/bin 各由谁使用？QEMU 预加载与 RustSBI 特权交接分别做什么？为何三个入口地址不同？

**Answer 1**

_1. 使用者划分：_
   - ELF 带有符号表等调试元数据，由链接器生成，供开发者使用 `gdb` 或 `readelf` 调试分析。
   - BIN 是无 ELF 元数据的原始镜像, 包含代码、只读数据、已初始化数据和必要间隔。
   - 本批还作为 objcopy 的输入；ELF 也可供装载器按程序头装载。

_2. 预加载与特权交接：_
   - QEMU 预加载负责模拟硬件上电，并将 `rustsbi.bin` 部署在 `0x80000000`，将内核 `os.bin` 部署在 `0x80200000`。
   - RustSBI 在 M-Mode 下运行，初始化底层硬件并提供 SBI 服务。随后通过修改特权级状态，将 CPU 降级至 S-Mode，并跳转到 `0x80200000` 正式完成交接。

_3. 三个入口地址不同的根本原因：_ 
   - 三个地址是本批QEMU virt / 固件 / 连接脚本的配套约定，并非RISC-V ISA为所有及其规定的地址，0x80000000是此平台RAM起点。
   - 固件保留器前部，内核使用后面的RAM。间隔 2MiB 不等于固件文件大小恰好为 2MiB，也不意味着整个RAM被 M-mode 固件独占   
   - `0x1000`：QEMU 模拟 CPU 的硬件上电复位地址（Reset Vector），固化了最开期的几条引导跳转指令。
   - `0x80000000`：本平台 RAM 的起点，RustSBI 位于其前部保留区域。
   - `0x80200000`：内核在 S-Mode 下的起点。该地址特意向后偏移了 2MB，目的是为了保护前端加载的 RustSBI 固件内存不被内核覆盖污染。

2. 为什么 Rust 第一条函数调用前要准备 sp？为什么清 BSS 的边界不能覆盖 boot stack？

**Answer 2**

_1. 必须提前准备 sp 的原因：_
   - Rust 函数（如 rust_main）在编译后遵循 RISC-V ABI/调用约定。函数在运行时需要利用栈（Stack Frame）来存储局部变量，参数常通过寄存器传递，需要时才走栈，函数需要保存的局部数据/寄存器取决于编译结果。若在调用前不将有效内存地址加载至 sp 寄存器，函数一开机执行压栈操作无效 sp 或擦除当前栈可能造成非法访问、内存破坏或错误返回，具体表现取决于执行路径和编译结果。标准 ABI 要求入口 sp 保持16字节对齐。

_2. 清 BSS 边界不能覆盖 boot stack 的原因：_
   - 执行清 BSS 的初始化逻辑本身就运行在 Rust 函数上下文中，高度依赖当前 sp 对应的 boot stack 来维持其循环变量和控制流。
   - 无效 sp或者擦除当前栈可能造成非法访问、内存破坏或者错误返回，一旦 BSS 的清零边界覆盖了正在使用的 boot stack 区间，清零操作（write_volatile）在推进时，会将自身函数赖以生存的栈帧数据（包括返回地址和局部变量）抹除为 0。无效 sp 或擦除当前栈可能造成非法访问、内存破坏或错误返回，具体表现取决于执行路径和编译结果。
   - 不能断言一定立即发生异常、死锁或者跳到零地址，不要依赖某次编译恰好没有用栈。

3. 从 console_putchar 到 ecall 再返回，当前代码跨越了哪两个特权级？和后面的用户 syscall 有何关系？

**Answer 3**

_1. 跨越的特权级：_
   当前代码跨越了 S-Mode (Supervisor，内核态) 和 M-Mode (Machine，机器态) 两个特权级。而用户syacall是从 U-Mode（User，用户态）到 S-Mode。
   内核在 S-Mode 下通过执行 ecall 指令触发陷入，由运行在最高特权级 M-Mode 的 RustSBI 固件接管并操作物理串口硬件，完成后通过 mret 指令返回 S-Mode。

_2. 与后续用户 Syscall 的关系与区别：_
   - 特权边界不同：后续的用户 Syscall 是 U-Mode (用户态) 向 S-Mode (内核态) 的跨越，目的是保护操作系统免受恶意应用的破坏；而当前代码是 S-Mode 向 M-Mode 的跨越，目的是实现硬件抽象。
   - 依赖与拦截关系：用户 Syscall 是进入 S-Mode 内核的通道。绝大多数 Syscall (如内存分配、进程调度) 在 S-Mode 内部即可由内核完全处理并返回，并不会下穿到 M-Mode。
   - sys_write 必须经 SBI”和“只有输出才用 SBI”都不成立：内核也可直接驱动 UART，timer 设置、关机等同样可能用 SBI。TinyOS 当前 syscall_write 直接调用 uart_putchar，timer_set_next 则调用 sbi_set_timer，正好提供两个对照。
   - 链式触发：若内核选择通过 SBI 输出，则可能形成 U→S→M，再逐层返回；若内核直接驱动 UART，则输出路径不必进入 M-mode，console_putchar (SBI Call)，从而跨越到 M-Mode 借助固件完成最终的硬件输出。


## 答案复核（2026-10-12）

三题的执行路径主线正确。保留上面的作者原答，下面集中修正几个表述：

- Answer 1：BIN 是无 ELF 元数据的原始镜像，包含代码、只读数据、已初始化数据及必要间隔，
  不是“纯机器指令”。本批 ELF 用于 objcopy 输入和调试/布局检查；ELF 也可被装载器
  根据程序头直接装载，不是只能给开发者使用的调试文件。
- Answer 1：三个地址是本批 QEMU virt / 固件 / 链接脚本的配套约定，
  不是 RISC-V ISA 为所有机器规定的地址。0x80000000 是此平台 RAM 起点；
  固件保留其前部，内核使用后面的 RAM。间隔 2 MiB 不等于固件文件大小恰为 2 MiB，
  也不意味着整个 RAM 被 M-mode 固件独占。
- Answer 2：“借用与函数调用规范”应改成“RISC-V ABI/调用约定”；Rust 借用规则是另一层概念。
  参数常通过寄存器传递，需要时才走栈；函数需要保存的局部数据/寄存器取决于编译结果。
  标准 ABI 要求入口 sp 保持 16 字节对齐。
- Answer 2：无效 sp 或擦当前栈可能造成非法访问、内存破坏或错误返回，
  不能断言一定立即异常、死锁或跳到零地址。不要依赖某次编译恰好没有用栈。
- Answer 3：本批 console 路径的 S→M→S 正确；用户 syscall 是 U→S。
  “sys_write 必须经 SBI”和“只有输出才用 SBI”都不成立：内核也可直接驱动 UART，
  timer 设置、关机等同样可能用 SBI。TinyOS 当前 syscall_write 直接调用 uart_putchar，
  timer_set_next 则调用 sbi_set_timer，正好提供两个对照。

依据：[objcopy 原始 binary](https://sourceware.org/binutils/docs/binutils/objcopy.html)、
[本教程启动约定](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter1/3first-instruction-in-kernel1.html)、
[RISC-V ABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/)。
无需重做三题；对照上述修正再梳理一次即可。

本批完成不等于第一章全部完成。下一批仍按本章补格式化输出、panic 诊断和布局理解，再进入批处理。

官方依据：[启动实践](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter1/4first-instruction-in-kernel2.html)、
[函数调用与栈](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter1/5support-func-call.html)、
[SBI 输出/关机](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter1/6print-and-shutdown-based-on-sbi.html)。
历史代码片段可能有旧 Rust 语法，以 sources.lock.json 对应源码的 stable/edition 2024 为准。
