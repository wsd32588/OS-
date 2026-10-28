# 第一章总结：启动链与最小运行时

整理日期：2026-10-24。对应 [BATCH01](../../experiments/rcore-learning/BATCH01.md) 与
[BATCH02](../../experiments/rcore-learning/BATCH02.md)，并保留第零章所需的执行环境概念。
这里按职责重新组织已完成内容；原批次中的作者回答、审查记录与历史验证仍保留在原处。

第一章解决的是：没有现成操作系统替程序安排入口、栈、输出与退出时，怎样让一段 Rust
代码成为能够启动、说明自身状态并正常结束的内核。第一章完成时只有 S-mode 内核，
还没有用户程序、任务调度或页表；后续章节新增的功能不能倒推为第一章已经具备。

## 1. 从源文件到正在运行的内核

整个过程分为生成镜像、预加载与 CPU 执行三个阶段：

```text
Rust / 汇编 + 链接脚本
    → ELF：入口、装载布局、代码与数据、符号等
    → objcopy -O binary
    → 原始 BIN 镜像

QEMU 在 CPU 开始执行前
    → 把 RustSBI 固件放入 RAM
    → 把内核 BIN 放入 RAM

CPU 执行
    → 复位入口 → M-mode 固件 → S-mode 内核 _start
    → 汇编设置 sp → rust_main → 清 BSS → 输出 → 请求关机
```

链接器安排“各段和符号应该位于哪里”，加载方负责“把文件内容放到对应内存”，
CPU 才负责“从某处取指执行”。链接成功并不等于镜像已装载；镜像在 RAM 中也不等于 CPU
已经跳到入口。三者必须配套。

当前 [Makefile](../../experiments/rcore-learning/Makefile) 仍将内核 ELF 转成 BIN，
用 `-device loader,...,addr=0x80200000` 预加载。RustSBI 在这条路径中不负责解析内核 ELF。
第四章内核自行解析用户 ELF，是另一层装载行为。

### ELF 与 BIN 各保留什么

| 产物 | 保留的信息 | 本项目用途 |
| --- | --- | --- |
| ELF | 文件头、程序头、代码与数据；构建选项决定是否还有符号及调试信息 | 链接结果、readelf/objdump/GDB 检查、objcopy 输入；也能供装载器按程序头装载 |
| 原始 BIN | 从可装载内容提取的字节及布局间隔，没有 ELF 头等结构 | 由已知地址的加载规则放进 RAM |

BIN 包含代码、只读数据、已初始化数据及必要间隔，不能说成“只有机器指令”。
BSS 通常没有对应的文件初值字节，由运行时建立零初值。
ELF 是一种可执行和目标文件格式，并非只能在 Linux 中使用；这个裸机内核本身就生成 ELF。
BIN 没有 ELF 入口字段，加载方必须另行知道地址安排。

### 三个启动地址的分工

| 地址 | 这里发生什么 | 谁规定 |
| --- | --- | --- |
| `0x1000` | QEMU virt 的复位引导代码开始执行 | 当前模拟平台 |
| `0x80000000` | RAM 前部的 RustSBI 固件入口区域，M-mode 执行 | 平台与固件装载约定 |
| `0x80200000` | 内核 `_start`，由固件交接后以 S-mode 执行 | 固件、加载命令与内核链接脚本共同约定 |

这些不是 RISC-V ISA 为所有机器统一规定的固定入口。`0x80000000` 是当前平台的 RAM
起点，不代表整段 RAM 都只能由 M-mode 使用。前部留给固件、内核向后偏移 2 MiB，
也不能推出固件文件恰好有 2 MiB。

## 2. 最小执行环境由哪些模块组成

| 模块 | 核心责任 | 不承担的责任 |
| --- | --- | --- |
| [entry.asm](../../experiments/rcore-learning/os/src/entry.asm) | 导出 `_start`，准备启动栈后调用 Rust | 不解析文件、不决定 CPU 从 M 到 S 的交接 |
| [linker.ld](../../experiments/rcore-learning/os/src/linker.ld) | 入口位置、各段布局、链接符号与重叠断言 | 不执行指令、不修改 `sp` |
| [main.rs](../../experiments/rcore-learning/os/src/main.rs) | 清 BSS、组织启动、提供 panic handler | 不代替汇编保存尚未建立的执行现场 |
| [console.rs](../../experiments/rcore-learning/os/src/console.rs) | 把格式化文本送到输出后端 | 不分配 String、不实现完整设备驱动 |
| [sbi.rs](../../experiments/rcore-learning/os/src/sbi.rs) | 字节输出、关机等固件调用边界 | 不等于用户 syscall 接口 |
| [scripts/cargo.sh](../../experiments/rcore-learning/scripts/cargo.sh) | 固定编译器检查与本地 Cargo 环境 | 不升级全局工具链 |

当前 `main.rs` 已包含后续内存与调度初始化，`linker.ld` 已增加跳板页布局。
上表讲的是这些模块从第一章形成的职责，不表示文件仍只有首章代码。

`#![no_std]` 让程序不依赖宿主 OS 的标准库；`core` 仍可提供整数、切片、Option、Result、
格式化等能力。`#![no_main]` 不走常规程序启动流程，由自己的 `_start` 决定如何进入 Rust。
它们不会自动生成栈、输出后端或堆分配器。

## 3. 栈：先有可用内存，再有函数调用

入口的关键两条指令是：

```assembly
la sp, boot_stack_top
call rust_main
```

第一条真正修改 CPU 的 `sp`；第二条进行普通函数调用，设置返回地址 `ra`。
`call` 不会改变特权级。固件已在进入 `_start` 前完成 M→S 交接。

Rust 编译出的函数可能建立栈帧、保存寄存器或使用栈上传参。不能依赖某次优化恰好省掉栈访问。
本项目保留 64 KiB 启动栈，向低地址增长，初始 `sp` 指向栈区域的上边界：

```text
低地址  boot_stack_lower_bound
        可用栈空间：65536 字节
高地址  boot_stack_top ← 初始 sp，指向区间末尾后一字节
```

标准 RISC-V ABI 要求函数边界 `sp` 保持 16 字节对齐。链接器为标签安排好地址，
汇编还必须显式把这个地址装进寄存器；两者缺一不可。

### `.balign 16` 与 `.align 4`

当前 RISC-V GNU 汇编语义下：

```assembly
.balign 16       # 按 16 字节边界对齐
.align 4        # 按 2^4 = 16 字节边界对齐
```

两者在这里能达到相同的对齐结果，单位表达不同；`.balign` 直接写字节数。
不要推广为所有架构或所有汇编器的 `.align` 都使用同一种单位；也不要把拼写写成 `.blign`。

## 4. BSS：建立零初值，避开正在使用的栈

启动时需要清零的是链接器界定的半开区间 `[sbss, ebss)`。
链接符号提供的是边界地址，不是让程序读取边界处某个字节后把它当长度或地址。

```rust
let start = core::ptr::addr_of!(sbss) as usize;
let end = core::ptr::addr_of!(ebss) as usize;
let mut current = start;

while current < end {
    unsafe { (current as *mut u8).write_volatile(0); }
    current += 1;
}
```

这是说明循环的简化片段；实际安全前提还包括边界位于可写 RAM、单 hart 初始化、
范围有界且没有其他访问者。`unsafe` 只是允许执行编译器无法自行证明的操作，
不是自动验证地址合法。`write_volatile` 保留写操作，也不提供原子性或互斥。

关键布局顺序是：

```text
.bss.stack：先放启动栈
sbss
普通 BSS：静态零初值数据、随后增加的其他静态区域
ebss
```

`clear_bss` 自己已经是 Rust 调用链的一部分，正在使用启动栈。
若把这段栈一起清零，就可能擦掉返回地址或局部状态。具体后果取决于编译结果和执行路径，
不能断言一定立刻死锁或一定返回零地址。

静态数组不一定都要排除：第二章准备的用户栈、应用内核栈开始时尚未运行，
可以放在普通 BSS 中先清零，再构造现场。判断依据是“此时是否正在使用”。

## 5. 格式化输出：算法与设备去向分开

输出一个整数的完整路径：

```text
println!("value={} hex={:#x}", 42, 42)
    → format_args! 构造带借用的 Arguments
    → console::print
    → core::fmt::Write::write_fmt
    → Console::write_str
    → text.bytes()
    → sbi::console_putchar
    → RustSBI 输出
```

`core::fmt` 把整数转换成十进制或十六进制文本；作者实现 `write_str`，决定文本往哪里送。
trait 提供的默认 `write_fmt` 可以复用 `write_str`，不必重写数字格式化。
`Arguments` 是借用参数的格式描述，不是拥有完整文本的 `String`，这条路径不需要堆。

这里遍历 UTF-8 字节，用 `text.bytes()`，不能简单将每个 `char` 截断成 `u8`。
首章必要验证使用 ASCII；曾观测到固件/QEMU 组合的中文输出兼容问题，原因未定位，
不能把它自动归为 Rust 字节循环的实现错误。

宏定义中的 `$argument` 是捕获的宏变量，`$crate` 指向定义宏的 crate；
美元符号不是一般意义上“函数与宏的区分标记”。调用端的 `println!` 中 `!` 才是宏调用语法。
空参数分支 `println!()` 只输出 `\n`，非空分支才将格式化结果再追加换行。

原始指针可使用指针格式 `{:p}`；若按整数地址用 `{:#x}` 输出，需要先显式转成 `usize`，
不能指望 `*const u8` 实现整数的 `LowerHex`。

## 6. SBI 与 panic：正常结果和失败结果都要能收尾

内核输出在本实现中通过 SBI 请求固件服务：

```text
S-mode 内核 → ecall → M-mode RustSBI → mret → S-mode 内核
```

现代 SBI 接口用 `a7` 放 EID、`a6` 放 FID，其余寄存器按接口放参数。
`sbi-rt` 负责这些寄存器约定的包装；它不改变服务方的职责。
当前输出使用匹配固件的 legacy console 接口，关机使用 `system_reset`。

`Shutdown` 代表复位动作；`NoReason` 或 `SystemFailure` 代表原因。
`system_reset<T, R>` 的泛型与 trait 约束让动作、原因以类型化方式传入，
最后 `raw()` 转为 SBI 规定的数字。两种原因类型不同，无需强行合成同一个局部变量。
成功关机不返回；若固件调用意外返回，外层循环保证 `shutdown(...) -> !` 的不返回契约。

panic 的路径是：

```text
panic!(...) → #[panic_handler] → 打印位置与消息 → shutdown(true)
```

`PanicInfo::location()` 是 Option，`message()` 提供可格式化的消息。
panic handler 的目标是留下诊断并以失败结束，不能再留 `todo!()`。
输出错误分支也不能使用 `unwrap`、`expect` 或再次 `panic!`：否则失败处理再次调用格式化，
可能递归进入 panic。当前 `console::print` 的失败分支用原始字节输出固定 ASCII 诊断，
随后失败关机。legacy 字节输出没有在这里提供完整的设备传输错误检测。

## 7. 验证所证明的范围

第一章已经完成的必要验证包括：ELF/入口布局、先设置 `sp` 再调用 Rust、64 KiB 栈及对齐、
普通 BSS 排除活动启动栈；正常 Hello 与格式化/布局日志；临时副本的 panic 诊断与失败关机。
这些结果记录在原批次和 [PROGRESS](../../experiments/rcore-learning/PROGRESS.md)，
本次整理未重新运行 QEMU。

`timeout 8s make run` 是在外层给命令设置最长等待时间；Makefile 的 `run` 本身没有这个超时。
QEMU 主动退出 `0` 才是该正常路径的结束；信号 15 与 `124` 通常表示外部 timeout 到期。
打印 Hello 或 application exited 单行日志，不能单独证明模拟器已经主动关机。

第一章建立的是能启动和诊断的地基，没有给用户代码单独的运行环境或内存隔离。
第二章在此基础上保留内核控制权，同时为应用提供用户入口、用户栈和 syscall 服务。

## 阅读入口

- [第二章总结：用户运行时与 Trap](CHAPTER02_USER_AND_TRAP.md)。
- [第一条指令与加载地址](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter1/3first-instruction-in-kernel1.html)、[函数调用与栈](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter1/5support-func-call.html)。
- [SBI 输出与关机](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter1/6print-and-shutdown-based-on-sbi.html)、[RISC-V ABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/)。
- [固定来源与环境](../../experiments/rcore-learning/sources.lock.json)；在线教程中的 Rust 写法可能随版本变化，以本地已固定源码和当前实现为准。
