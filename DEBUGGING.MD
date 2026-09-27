# TinyOS 调试手册

本文记录在 WSL Ubuntu 中用 QEMU 和 `gdb-multiarch` 调试 TinyOS 的已验证流程。

## 1. 准备调试构建

在仓库根目录执行：

```sh
cd /home/asus/tinyos
make -B
```

`-B` 会强制重新编译，适合排除 Makefile 尚未跟踪头文件依赖所造成的陈旧对象问题。

## 2. 启动暂停态 QEMU

打开终端 A：

```sh
cd /home/asus/tinyos
make debug
```

该目标使用：

```text
-S                       启动后先暂停 CPU
-gdb tcp:127.0.0.1:2600  在本机 2600 端口等待 GDB
```

终端看似停住是正常现象；此时 QEMU 正等待 GDB。

## 3. 连接 GDB

打开终端 B：

```sh
cd /home/asus/tinyos
gdb-multiarch kernel.elf
```

在 GDB 中执行：

```gdb
set pagination off
target remote 127.0.0.1:2600
break task_yield
continue
```

`kernel.elf` 带有调试符号，因此可以直接用函数名和源码行设置断点。

## 4. 查看断点现场

```gdb
info registers pc sp ra a0 a7
p/x $sstatus
p/x $stvec
p/x $scause
p/x $sepc
p/x $medeleg
disassemble /r task_yield
```

常用命令：

- `info registers`：查看全部通用寄存器。
- `p/x $寄存器名`：以十六进制查看单个寄存器或 CSR。
- `disassemble /r 函数名`：显示指令地址、机器码和汇编。
- `x/8i $pc`：从当前 PC 开始查看 8 条指令。
- `bt`：查看调用栈；在手写上下文切换或缺少栈帧时结果可能不完整。

## 5. 单步观察 ECALL

先从反汇编中找到 `ecall` 及其下一条指令的地址，不要长期照抄旧地址；重新编译后地址可能变化。

当前实验中，函数反汇编的关键部分是：

```text
task_yield+8:   li    a7,1
task_yield+10:  ecall
task_yield+14:  nop
```

执行一次 `stepi`，停到 `ecall` 前：

```gdb
stepi
info registers pc a0 a7
```

为了保证 ECALL 处理完成后停在下一条指令，可对反汇编中 ECALL 后面的地址设置临时断点：

```gdb
tbreak *ECALL_后面一条指令的地址
stepi
```

如果 `stepi` 停进固件或 trap 入口，而没有直接到临时断点，先检查当前位置和原因，再用 `continue` 运行到临时断点。

ECALL 返回后检查：

```gdb
info registers pc a0 a7
p/x $mcause
p/x $mepc
p/x $scause
p/x $sepc
```

判断原则：

- `mcause == 9`：发生的是来自 S-mode 的 ECALL。
- `scause` 没有变成本次异常、且控制流没有进入 `trap_entry`：异常由 M-mode 固件处理，没有进入 TinyOS 的 S-mode trap handler。
- 串口出现额外字符时，应结合 ECALL 前的 `a0`、`a7` 和 SBI 调用约定解释，不能只凭输出猜测。

## 6. 暂停、继续和断点管理

```gdb
continue
```

程序运行时，在终端 B 按 `Ctrl+C` 可让 GDB 暂停虚拟 CPU。随后可以使用：

```gdb
info breakpoints
disable 1
enable 1
delete 1
```

编号以 `info breakpoints` 的实际输出为准。

## 7. 正常退出

先在 GDB 中执行：

```gdb
detach
quit
```

然后回到终端 A，按 `Ctrl+A`，松开后再按 `X`，退出 QEMU。

不要直接关闭承载 QEMU 的 WSL 终端；先退出 GDB 和 QEMU，现场更容易保持可控。

## 8. 验证 CSR 位确实被修改

不要只凭串口停止输出就推断中断已经关闭。可以在修改 CSR 的函数设置断点，例如：

```gdb
break timer_stop
continue
p/x $sie
disassemble /r timer_stop
```

用 `stepi` 执行到并越过 `csrc sie, ...`，然后再次检查：

```gdb
p/x $sie
p/x $scause
x/i $pc
```

2026-09-26 的全任务结束实验中，`timer_stop()` 调用前 `sie=0x20`，执行后 `sie=0x0`；`scause=0x8000000000000005` 表明当前仍在处理 S-mode timer interrupt。这个证据比“后面没再打印 tick”更直接。

## 本次 task_yield 实验结果

2026-09-26 的已验证现场：

```text
进入 task_yield 时：a0 = 0x41，a7 = 0
执行 li 后：         a0 = 0x41，a7 = 1
ECALL 返回后：       pc 位于 ECALL 下一条指令，a0 = 0
                     mcause = 9，scause = 0
```

QEMU 串口多输出了一个 `A`，没有出现 TinyOS 的 `[ecall]` 日志。这证明该请求进入 OpenSBI，并被 legacy SBI extension 1 当作 console putchar 处理，而不是进入 `trap_handler()` 的 S-mode ECALL 分支。
