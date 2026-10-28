# 第二批：第一章收尾——格式化输出与 panic 诊断

BATCH01 的启动、栈、BSS、SBI 输出和正常/失败关机已验收。
现在完成官方第一章最后一节的格式化输出，再进入第二章批处理系统。
本批核心代码仍由作者写；协作者集中审查并重复运行必要验证。

## 先理解整个输出路径

目前 rust_main 自己把字符串拆成字节。以后打印寄存器、地址、错误位置时，
不应每次自己写数字转换与字节循环，因此把这项工作集中到 console 模块。

```mermaid
flowchart LR
    M[print! / println!] --> A[format_args! 生成借用的 Arguments]
    A --> P[console::print]
    P --> F[core::fmt::Write::write_fmt]
    F --> S[作者实现 write_str]
    S --> B[遍历字符串字节]
    B --> C[sbi::console_putchar]
    C --> E[SBI / 固件输出]
```

panic 的路径是：panic! → 已有 panic_handler → 格式化消息与文件/行号 →
shutdown(true)。异常时能说明出错位置，才方便下一章检查应用切换和 trap。

格式化算法来自 core；我们实现的是“格式化出来的文本往哪里写”。
`format_args!` 不分配堆内存，`Arguments` 借用参数，也不是拥有文本的 String。
本批当场把它传给 print；不用 format!、alloc 或堆分配器。

## 文件范围与完成点

| 文件 | 本批工作 |
| --- | --- |
| 新建 `os/src/console.rs` | Console、Write 实现、print 函数、两个宏 |
| 修改 `os/src/main.rs` | 声明模块，替换手写输出循环，增加格式化/栈/BSS日志，补 panic 诊断 |

已有 SBI 接口、入口汇编、链接脚本和 Cargo 依赖继续使用。
布局日志只用现有 boot_stack_lower_bound/top、sbss/ebss；不要求额外重写全部链接段。
彩色日志、log crate、日志等级按教程练习视为选做，不阻塞进入第二章。

## A. 实现文本输出适配器

在 console.rs 中从下面骨架开始：

```rust
use core::fmt::{self, Write};

struct Console;

impl Write for Console {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        // TODO: 逐字节调用 crate::sbi::console_putchar，完成后返回 Ok(())。
        todo!("write formatted text to SBI");
    }
}

pub fn print(arguments: fmt::Arguments<'_>) {
    // TODO: 创建可变 Console，调用 write_fmt(arguments)，显式处理返回值。
    todo!("consume formatting arguments");
}
```

你需要理解的 Rust：

- trait 声明一组行为；`impl Write for Console` 给 Console 实现该行为。
- `&mut self` 借用输出对象，`&str` 借用文本；Console 自己不必保存文本或申请堆内存。
- `fmt::Result` 是 `Result<(), fmt::Error>`，写完用 Ok(())；不是写入字节数。
- `write_fmt` 是 trait 提供的方法，作用域要有 `use core::fmt::Write`。
- `Arguments<'_>` 这里表示带借用的格式化参数。先按骨架使用，无需本批学习完整生命周期语法。

字符串用 `text.bytes()`。我们的 console_putchar 接收 u8，不能把 `char as u8` 当作
UTF-8 编码；一个 Unicode 字符可能由多个字节组成。官方参考的 chars/c as usize
匹配它自己的接口，不直接复制到这里。

**错误路径提前约定**：print 检查 write_fmt 的 Result；若 Err，用原始字节循环输出
固定 ASCII 文本 `[console] format failed\n`，随后 shutdown(true)。
这条诊断不能调用 print!/println!、unwrap、expect 或 panic!，避免格式化失败再进入
panic 输出，形成递归。当前 write_str 在 SBI 字节调用之后返回 Ok(())；这不等于
实现了底层设备传输错误检测，SBI legacy 接口本批没有这层错误回报。

## B. 接入 print! / println!

先看清语法：macro_rules! 定义宏；`$($argument:tt)*` 捕获一串 token，
交给 format_args! 解析；`$crate` 指向定义宏的 crate。双大括号让展开结果成为一个表达式块。

下面给出宏接线，核心输出仍是你上一步写的两个函数：

```rust
#[macro_export]
macro_rules! print {
    ($($argument:tt)*) => {{
        $crate::console::print(core::format_args!($($argument)*));
    }};
}

#[macro_export]
macro_rules! println {
    () => {{
        $crate::print!("\n");
    }};
    ($($argument:tt)+) => {{
        $crate::print!("{}\n", core::format_args!($($argument)+));
    }};
}
```

println 利用 Arguments 的 Display 支持追加换行，不必先分配一个字符串。
宏使用 `#[macro_export]` 后出现在 crate 根；main.rs 写 `mod console;`，
调用时先统一用 `crate::println!`，避免与其它宏的作用域混淆。
不要把带借用的 Arguments 随意塞进全局、返回给外层或延迟到参数失效之后再用。
当前版本支持某些 let 初始化中的临时生命周期延长，不必记成“一律不能存局部变量”。

## C. 正常路径与布局日志

rust_main 保持 clear_bss 最先执行，把原手写字节循环替换成：

```rust
crate::println!("[kernel] Hello rCore!");
crate::println!("[kernel] value={} hex={:#x}", 42, 42);
// TODO: 输出下面两个地址范围。
// 最后仍调用 sbi::shutdown(false)。
```

在现有 unsafe extern 块补两条外部静态符号声明：boot_stack_lower_bound 和 boot_stack_top。
沿用 clear_bss 的 addr_of! → usize 获取地址，不读取符号位置的字节。
输出两行（变量名由你选择）：

```text
[kernel] stack [0x..., 0x...) bytes=65536
[kernel] bss [0x..., 0x...)
```

格式提示：十六进制地址用 `{:#x}`，大小用十进制 `{}`；半开区间大小是 top-bottom。
不要硬编码 BATCH01 曾观测到的具体地址：增加 console 后镜像布局可能变化。
普通 BSS 允许为空，sbss==ebss 合法；栈范围必须排除在普通 BSS 外。
不为“让日志好看”强行增添全局变量或改变链接布局。

## D. panic 必须留下诊断

改已有 handler，不再新增第二个 `#[panic_handler]`：

```rust
#[panic_handler]
fn panic(information: &PanicInfo<'_>) -> ! {
    // TODO: information.location() 是 Option，处理 Some 与 None。
    // Some 时输出：[panic] <文件>:<行号> <消息>
    // None 时输出：[panic] <消息>
    // 消息来自 information.message()，可直接用 {} 格式化。
    // 最后调用 sbi::shutdown(true)。
    todo!("panic diagnostic and failed shutdown");
}
```

这个 todo 只在写代码期间占位，交回前必须移除，尤其不能在 panic handler 中执行它。
当前 Rust 的 message() 返回可 Display 的 PanicMessage，不是 Option；不用 unwrap。
location 才需要 if let Some。panic 不返回、不继续执行正常关机，不尝试恢复运行。
本批只打印标准消息、字符串和整数，不额外实现自己的 Display 类型。

## 提前列齐的验收要求

**P0 由协作者承担**，作者整批写完后集中交回；只有正常/失败两次 QEMU 运行：

| 场景 | 预期 |
| --- | --- |
| 正常启动 | Hello 一次；value=42、hex=0x2a；栈/BSS两行与本次 ELF 符号一致；退出 0，无 panic |
| 临时副本在 clear_bss 后、正常输出前触发 `panic!("BATCH02 probe value={}", 7)` | 一次 panic 诊断包含实际文件、行号与 value=7；正常 Hello 不出现；失败关机退出码预期 255，由实测确认 |

静态集中检查：没有遗留 todo!、不分配堆、不用递归的格式化错误处理；栈仍 64 KiB、16 字节对齐。
`println!()` 空参数分支由编译/宏检查确认，不新增故障矩阵；作者不用写持久测试脚本。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
make inspect
timeout 8s make run
```

非 ASCII 显示先不列为作业：协作者临时试验中，这个固件/QEMU 输出组合的中文原始字节
出现额外转码，具体原因尚未定位。write_str 仍按 UTF-8 字节实现；本批 P0 使用 ASCII，
不把固件输出兼容问题当作作者代码不合格，也不为此扩展环境排查。

## 三个理解小测（完成后回答）

1. 哪一层负责把 42 转成十进制/十六进制字符？哪一层把这些字符送到 SBI？为什么不用 String？
2. 为什么实现 write_str 就可以使用 write_fmt？Arguments 拥有什么、借用什么？
3. panic 输出为什么不能使用会再次 panic 的错误处理？从 panic! 到失败关机追踪一次完整路径。

完成这一批，第一章收尾。第二章随后按“特权级与用户执行环境 → 第一个用户应用 →
trap 上下文保存/恢复 → 批处理装载与切换”推进，不一次引入全部实现。

来源：[第一章最后一节](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter1/6print-and-shutdown-based-on-sbi.html)、
[Write](https://doc.rust-lang.org/core/fmt/trait.Write.html)、
[format_args!](https://doc.rust-lang.org/core/macro.format_args.html)、
[PanicInfo](https://doc.rust-lang.org/core/panic/struct.PanicInfo.html)。
网页 Rust 版本可能变化；上述签名及宏接线另已在本机 1.98.1 的临时副本编译/运行核对。
