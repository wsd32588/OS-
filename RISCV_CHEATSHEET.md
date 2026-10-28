# RISC-V 使用清单（TinyOS / RV64）

这份清单面向本仓库当前的 RV64 S-mode 内核开发。目标不是替代规范，而是在写汇编、trap、页表和 syscall 时，先用一两分钟确认最容易写错的规则。

## 0. 资料与项目基线

本地官方资料：

- [`docs_from_riscv/riscv-spec.pdf`](docs_from_riscv/riscv-spec.pdf)：非特权 ISA，查整数指令、访存、跳转、`ECALL`、`FENCE.I`、CSR 指令等。
- [`docs_from_riscv/riscv-privileged.pdf`](docs_from_riscv/riscv-privileged.pdf)：特权架构，查 CSR、trap、中断、`sstatus`、`satp`、Sv39、`SFENCE.VMA` 等。

本项目编译目标：

```text
ISA:       rv64imac_zicsr_zifencei
ABI:       lp64
机器:      QEMU virt
固件:      OpenSBI（M-mode）
内核:      S-mode，入口 0x80200000
分页:      Sv39，4 KiB 基础页
字节序:    little-endian
```

注意三类规则的来源不同：

1. **ISA/特权架构规则**：CPU 必须遵守，例如 `sret`、`scause`、PTE 格式。
2. **ABI/工具链约定**：编译器和函数共同遵守，例如 `a0` 传参、栈 16 字节对齐。
3. **TinyOS 自己的约定**：本项目自行设计，例如 syscall 编号、`sscratch` 的含义。

## 1. 动手前的最短检查

### 写普通汇编

- [ ] 当前代码运行在 U、S 还是 M-mode？
- [ ] 该指令属于 `-march=rv64imac_zicsr_zifencei` 吗？
- [ ] 这是实体指令还是 GNU assembler 伪指令？
- [ ] 函数调用边界是否遵守 ABI？
- [ ] `sp` 在调用点是否保持 16 字节对齐？
- [ ] 会不会误用 caller-saved 寄存器保存长期值？
- [ ] 启用 C 扩展后，代码地址只要求 2 字节对齐；数据仍按访问宽度正确对齐。

### 写 trap/中断

- [ ] `stvec` 已设置，入口地址满足对齐要求。
- [ ] 先保存所有将被修改、且返回后仍需保持的 GPR。
- [ ] 保存 `sepc`、`sstatus`、`scause`、`stval`。
- [ ] 用 `scause` 最高位区分 interrupt/exception，再看低位 cause code。
- [ ] 用保存的 `sstatus.SPP` 判断 trap 前来自 U-mode 还是 S-mode。
- [ ] 返回前恢复正确的 `sepc`、`sstatus`、`sp` 和通用寄存器。
- [ ] 只有确实处理完的同步异常才推进 `sepc`。
- [ ] `sret` 前已经准备好下一次 trap 所需的内核栈约定。

### 改活动页表

- [ ] VA、PA 都按目标页大小对齐。
- [ ] VA 是 Sv39 canonical address。
- [ ] PTE 不使用非法的 `R=0, W=1` 组合。
- [ ] 中间 PTE 与叶 PTE 分清楚。
- [ ] U-mode 页面带 `U`，内核页面不带 `U`。
- [ ] 可执行页不要同时可写；栈页不要可执行。
- [ ] 当前代码主动设置需要的 `A/D` 位。
- [ ] 修改活动页表后执行范围正确的 `sfence.vma`。
- [ ] 撤销映射时遵守 `unmap -> sfence.vma -> reclaim`。
- [ ] 把数据写成指令后，在本 hart 执行 `fence.i`。

## 2. 整数寄存器与 ABI

RV64 有 32 个整数寄存器，每个 64 位。`x0` 永远读出 0，写入被丢弃。

| ABI 名称 | 编号 | 典型用途 | 跨函数调用保持者 |
|---|---:|---|---|
| `zero` | x0 | 常数 0 | 固定不变 |
| `ra` | x1 | 返回地址 | caller |
| `sp` | x2 | 栈指针 | callee 按 ABI 恢复 |
| `gp` | x3 | global pointer | 不要随意临时使用 |
| `tp` | x4 | thread pointer | 不要随意临时使用 |
| `t0`–`t2` | x5–x7 | 临时值 | caller |
| `s0`/`fp`–`s1` | x8–x9 | 保存值/可选 frame pointer | callee |
| `a0`–`a1` | x10–x11 | 参数、返回值 | caller |
| `a2`–`a7` | x12–x17 | 参数 | caller |
| `s2`–`s11` | x18–x27 | 保存值 | callee |
| `t3`–`t6` | x28–x31 | 临时值 | caller |

常用 ABI 规则：

- 整数参数依次放入 `a0`–`a7`。
- 整数返回值通常放在 `a0`，第二个返回值可放 `a1`。
- `ra`、`a*`、`t*` 是 caller-saved；调用者需要时自行保存。
- `s*` 是 callee-saved；被调函数使用后必须恢复。
- 栈向低地址增长，函数调用边界保持 16 字节对齐。
- 普通函数只按 ABI 保存寄存器；可恢复执行的 trap 上下文通常要保存全部相关 GPR。
- `gp`、`tp` 有专门用途，不应当作普通临时寄存器。

## 3. 常用整数指令

### 算术与位运算

```asm
add  rd, rs1, rs2       # 64-bit 加法
sub  rd, rs1, rs2
addi rd, rs1, imm       # 12-bit 符号扩展立即数
and  rd, rs1, rs2
or   rd, rs1, rs2
xor  rd, rs1, rs2
andi rd, rs1, imm
ori  rd, rs1, imm
xori rd, rs1, imm
sll  rd, rs1, rs2
srl  rd, rs1, rs2       # 逻辑右移，补 0
sra  rd, rs1, rs2       # 算术右移，复制符号位
slt  rd, rs1, rs2       # 有符号小于
sltu rd, rs1, rs2       # 无符号小于
lui  rd, imm20          # 立即数放到 bit 31:12
auipc rd, imm20         # PC + (立即数 << 12)
```

RV64 的 `addw/subw/sllw/srlw/sraw` 只计算低 32 位，再把 bit 31 符号扩展到 64 位。它们不是普通的 64 位运算。

立即数易错点：

- 大多数 I-type 立即数只有 12 位并进行符号扩展。
- `li rd, large_constant` 是伪指令，可能展开成多条指令。
- 不确定展开结果时用 `riscv64-linux-gnu-objdump -d kernel.elf` 检查。

当前项目启用了 M 扩展，常用乘除法指令包括：

```asm
mul    rd, rs1, rs2     # 乘积低 XLEN 位
mulh   rd, rs1, rs2     # 有符号乘积高 XLEN 位
mulhu  rd, rs1, rs2     # 无符号乘积高 XLEN 位
mulhsu rd, rs1, rs2     # 有符号 rs1 × 无符号 rs2 的高 XLEN 位
div    rd, rs1, rs2     # 有符号除法
divu   rd, rs1, rs2     # 无符号除法
rem    rd, rs1, rs2     # 有符号余数
remu   rd, rs1, rs2     # 无符号余数
```

`mulw/divw/divuw/remw/remuw` 使用低 32 位操作数，并把 32 位结果符号扩展到 RV64。RISC-V 整数除零不会产生 trap；除法和余数会返回规范定义的结果，不能用 trap 代替软件除零检查。

### 访存

```asm
lb   rd, offset(rs1)    # 8-bit，符号扩展
lbu  rd, offset(rs1)    # 8-bit，零扩展
lh   rd, offset(rs1)    # 16-bit，符号扩展
lhu  rd, offset(rs1)    # 16-bit，零扩展
lw   rd, offset(rs1)    # 32-bit，符号扩展到 64 位
lwu  rd, offset(rs1)    # 32-bit，零扩展到 64 位
ld   rd, offset(rs1)    # 64-bit

sb   rs2, offset(rs1)
sh   rs2, offset(rs1)
sw   rs2, offset(rs1)
sd   rs2, offset(rs1)
```

- RISC-V 是 load/store 架构，算术指令不直接操作内存。
- 不要依赖未对齐访问一定成功；执行环境可以处理，也可以产生异常。
- 地址计算不会自动检查溢出或边界。

### 分支和跳转

```asm
beq  rs1, rs2, label
bne  rs1, rs2, label
blt  rs1, rs2, label    # 有符号
bge  rs1, rs2, label
bltu rs1, rs2, label    # 无符号
bgeu rs1, rs2, label

jal  rd, label          # rd <- 返回地址，PC <- label
jalr rd, offset(rs1)    # 目标地址最低位会被清零
```

常见伪指令：

```asm
mv   rd, rs             # addi rd, rs, 0
li   rd, imm            # 可能展开成多条
la   rd, symbol         # 取符号地址，可能展开成多条
j    label              # jal x0, label
call function           # 把返回地址写入 ra
ret                     # jalr x0, 0(ra)
nop                     # addi x0, x0, 0
```

## 4. GNU 汇编写法

```asm
.section .text.foo, "ax", @progbits
.global symbol
.balign 4

symbol:
    # ...
```

- `.global name`：让链接器能看到符号。
- `.section`：选择输入 section；链接脚本决定最终放置位置与 ELF segment。
- `.balign 4`：按 4 字节对齐，含义直观。
- RISC-V GNU assembler 中 `.align n` 通常表示按 `2^n` 字节对齐；不确定时优先写 `.balign`。
- 数字局部标签可重复使用：`1:` 定义，`1b` 跳到当前位置之前最近的 `1:`（backward），`1f` 跳到之后最近的 `1:`（forward）。

控制压缩指令：

```asm
.option push
.option norvc           # 这一区域禁止生成 16-bit 压缩指令
    # ...
.option pop
```

- 项目启用了 C 扩展，普通指令地址可以是 2 字节对齐。
- `.option norvc` 只约束该区域的指令编码；不要忘记 `push/pop`，避免影响后续文件内容。
- `ecall` 本身固定为 32 位，即使启用了 C 扩展。

## 5. CSR 指令

CSR 是 12 位编号的控制状态寄存器。能否访问取决于当前特权级和 CSR 属性；非法访问会触发 illegal-instruction exception。

```asm
csrr  rd, csr           # 读取，伪指令
csrw  csr, rs           # 写入，伪指令
csrs  csr, rs           # 把 rs 中为 1 的位设为 1
csrc  csr, rs           # 把 rs 中为 1 的位清为 0

csrrw rd, csr, rs       # 原子读旧值并整体写新值
csrrs rd, csr, rs       # 读旧值并置位
csrrc rd, csr, rs       # 读旧值并清位
```

例子：

```asm
csrr t0, sstatus
csrw sscratch, zero

li   t0, (1 << 5)
csrs sie, t0            # sie.STIE = 1
csrc sie, t0            # sie.STIE = 0
```

注意：

- CSR 指令提供的是 CSR 读改写原子性，不等于一般内存屏障。
- C inline asm 里的 `"memory"` clobber 约束编译器重排，不会自动生成 CPU `fence`。
- 需要硬件内存顺序时使用合适的 `fence`；需要页表同步时用 `sfence.vma`；需要取指同步时用 `fence.i`。

## 6. S-mode 常用 CSR

| CSR | 用途 | 写代码时最常问的问题 |
|---|---|---|
| `sstatus` | S-mode 状态 | trap 前来自哪一级？中断返回后是否开启？ |
| `sie` | 各类 S-mode 中断使能 | STIE/SSIE/SEIE 是否打开？ |
| `sip` | 各类 S-mode 中断 pending | 哪类中断正在等待处理？ |
| `stvec` | S-mode trap 入口 | direct 还是 vectored？BASE 是否对齐？ |
| `sscratch` | S-mode 软件自定义临时值 | 是否可用于 U/S 栈切换？ |
| `sepc` | trap 返回地址 | `sret` 将回到哪里？是否需要推进？ |
| `scause` | trap 原因 | interrupt 还是 exception？cause code 是多少？ |
| `stval` | trap 附加值 | 故障地址/指令信息是什么？ |
| `satp` | 地址翻译与 ASID | 当前分页模式、ASID、根页表 PPN 是什么？ |

### `sstatus` 常用位

| 位 | 名称 | 含义 |
|---:|---|---|
| 1 | `SIE` | 当前在 S-mode 时的 S 级全局中断使能 |
| 5 | `SPIE` | trap 前的 `SIE`，也是 `sret` 恢复来源 |
| 8 | `SPP` | trap 前特权级：0=U，1=S |
| 18 | `SUM` | 允许 S-mode load/store 访问 `U=1` 页面 |
| 19 | `MXR` | load 可把可执行页视为可读 |

关键规则：

- trap 进入 S-mode 时：`SPIE <- SIE`、`SIE <- 0`、`SPP <- 先前特权级`。
- `sret` 时：特权级由 `SPP` 决定，`SIE <- SPIE`，随后 `SPIE <- 1`、`SPP <- 0`。
- `SUM=1` 只允许 S-mode 对 U 页做数据 load/store，不允许 S-mode 从 U 页取指。
- `MXR=1` 可以让 load 读取 `X=1, R=0` 的页面；TinyOS 当前直接给代码页设置 `R|X`，不依赖 MXR。
- 当处理器运行在 U-mode 时，S-mode 是更高特权级；S 级中断的全局使能规则不要简单等同于“必须看到当前 `SIE=1`”。返回上下文仍应正确设置 `SPIE/SIE`。

## 7. Trap 的硬件行为

### CPU 自动完成的事情

当 trap 被送到 S-mode 时，硬件会完成类似以下状态转换：

```text
sepc       <- 被打断或产生异常的 PC
scause     <- interrupt 标志 + cause code
stval      <- 与异常有关的附加信息（没有信息时可为 0）
sstatus    <- 更新 SPP/SPIE/SIE
pc         <- stvec 指定的入口
privilege  <- S-mode
```

### CPU不会自动完成的事情

- 不会自动保存通用寄存器。
- 不会自动切换到内核栈。
- 不会自动创建 `TrapFrame`。
- 不会自动调用 C 函数。
- 不会自动推进 `sepc` 跳过导致异常的指令。

这些都必须由 trap 入口汇编和处理函数完成。

### `stvec`

```text
stvec.BASE = trap 基址
stvec.MODE = 0 Direct / 1 Vectored
```

- Direct：所有 trap 都进入 BASE。
- Vectored：同步异常仍进入 BASE；异步中断进入 `BASE + 4 * cause`。
- TinyOS 当前使用 Direct，因为写入的是对齐入口地址且低 MODE 位为 0。

### `scause`

RV64 中：

```text
bit 63 = 1: interrupt
bit 63 = 0: exception
bits 62:0: cause code
```

常见同步异常：

| code | 原因 |
|---:|---|
| 0 | Instruction address misaligned |
| 1 | Instruction access fault |
| 2 | Illegal instruction |
| 3 | Breakpoint |
| 4 | Load address misaligned |
| 5 | Load access fault |
| 6 | Store/AMO address misaligned |
| 7 | Store/AMO access fault |
| 8 | Environment call from U-mode |
| 9 | Environment call from S-mode |
| 12 | Instruction page fault |
| 13 | Load page fault |
| 15 | Store/AMO page fault |

常见 S-mode 中断：

| code | 原因 |
|---:|---|
| 1 | Supervisor software interrupt |
| 5 | Supervisor timer interrupt |
| 9 | Supervisor external interrupt |

trap 最小解码：

```c
is_interrupt = scause >> 63;
code = scause & ~(1UL << 63);
```

### `sepc` 与 `stval`

- 同步异常时，`sepc` 通常指向导致异常的指令。
- 中断发生在指令边界，返回时通常不手动增加 `sepc`。
- `ecall` 处理成功后需要 `sepc += 4`，否则 `sret` 后会再次执行同一条 `ecall`。
- page fault 时，`stval` 通常提供发生访问的虚拟地址。
- `stval=0` 不表示一定没有 trap；某些异常本来就没有附加值。
- 不要对所有异常统一执行 `sepc += 4`：缺页可能需要修好映射后重试原指令。

## 8. `sscratch` 与栈切换

`sscratch` 的架构定义只说它供 S-mode 软件使用；其具体含义完全由内核约定。

TinyOS 当前约定：

```text
正在运行 S-mode：sscratch = 0
正在运行 U-mode：sscratch = 当前任务内核栈顶
```

入口使用：

```asm
csrrw sp, sscratch, sp
```

结果：

- 来自 U-mode：`sp` 得到内核栈顶，用户 `sp` 暂存在 `sscratch`。
- 来自 S-mode：第一次交换得到 `sp=0`，入口识别后再交换一次恢复原内核 `sp`。

检查清单：

- [ ] 在覆盖任何原寄存器前保存它，或确保值仍可取回。
- [ ] 判断 trap 来源要用硬件保存的 `sstatus.SPP`，不能只猜 `sscratch`。
- [ ] 进入 C handler 前令 `sscratch=0`，维持 S-mode 约定。
- [ ] 返回 U-mode 前令 `sscratch=该任务内核栈顶`。
- [ ] 返回 S-mode 前令 `sscratch=0`。
- [ ] 调度切换到另一个 frame 时，依据“将返回到的 frame”重建 `sscratch`。

本项目 `TrapFrame` 当前为 288 字节，显式保存被打断时的 `sp`；C 布局和汇编偏移由静态断言校验。

## 9. 中断和定时器

一个 S-mode timer interrupt 能被观察到，通常需要同时满足：

1. M-mode/固件把相关中断委托给 S-mode。
2. 定时器比较值已经设置，时间到达后产生 pending。
3. `sie.STIE=1`。
4. 当前特权级和全局中断状态允许该中断被接收。
5. `stvec` 和 trap 上下文有效。

TinyOS 的流程：

```text
读取 time
  -> SBI TIME.set_timer 安排下一次事件
  -> 设置 sie.STIE
  -> 收到 scause(interrupt=1, code=5)
  -> handler 再次 set_timer
  -> 调度器选择下一个 TrapFrame
  -> sret
```

常用位：

```text
sie.STIE = bit 5
```

`wfi` 注意事项：

- `wfi` 是等待中断提示，不应当被当成“保证睡眠到某个特定中断”。
- 实现可以让它较早返回，代码应放在循环中并重新检查状态。
- `wfi` 不等于打开中断；中断委托和使能仍需正确配置。

`time` CSR 的可访问性受更高特权级计数器权限控制；TinyOS 当前由 OpenSBI/QEMU 环境提供可用访问。

## 10. `ecall`、syscall 与 SBI

`ecall` 只负责产生 environment-call exception。它本身不知道你想调用 syscall 还是 SBI；含义由当前特权级和处理该 trap 的软件决定。

| 发起位置 | `scause` code | 通常的接收者 |
|---|---:|---|
| U-mode | 8 | S-mode OS（若已委托） |
| S-mode | 9 | M-mode 固件/SBI（取决于委托） |
| M-mode | 11 | M-mode trap handler |

TinyOS 用户 syscall 约定（项目规则，不是 ISA 强制规则）：

```text
a7 = syscall 编号
a0 = 第一个参数
a0 = 返回值
```

处理步骤：

- [ ] 确认是 exception，不是 interrupt。
- [ ] 确认 code=8，来源确实是 U-mode。
- [ ] 从保存的 `a7/a0...` 读取编号与参数。
- [ ] 未知 syscall 返回明确错误。
- [ ] 已处理后执行 `sepc += 4`。
- [ ] 通过 TrapFrame 写回 `a0` 返回值。
- [ ] 用户指针不能直接相信；以后必须先验证范围和权限再访问。

OpenSBI 调用约定与 TinyOS syscall 是两套接口。当前 SBI v0.2 风格调用使用：

```text
a7 = Extension ID
a6 = Function ID
a0-a5 = 参数
a0 = error
a1 = value
```

因此：

- U-mode 的 `ecall` 是给 TinyOS 内核的 syscall。
- S-mode 的 `ecall` 通常是给 OpenSBI 的服务请求。
- 不要把 TinyOS syscall 编号直接当成 SBI Extension ID。

## 11. Sv39 地址结构

### 虚拟地址

Sv39 使用 39 位有效虚拟地址：

```text
63                39 38       30 29       21 20       12 11       0
+-------------------+-----------+-----------+-----------+-----------+
| bit 38 的符号扩展 |  VPN[2]   |  VPN[1]   |  VPN[0]   | page off  |
+-------------------+-----------+-----------+-----------+-----------+
       25 bits          9 bits      9 bits      9 bits      12 bits
```

canonical 规则：bit 63:39 必须全部等于 bit 38。

常用公式：

```c
offset = va & 0xfff;
vpn0 = (va >> 12) & 0x1ff;
vpn1 = (va >> 21) & 0x1ff;
vpn2 = (va >> 30) & 0x1ff;
```

每张页表 4 KiB，包含 512 个 8 字节 PTE。

| 叶 PTE 所在级别 | 页面大小 |
|---:|---:|
| level 2 | 1 GiB |
| level 1 | 2 MiB |
| level 0 | 4 KiB |

大页叶 PTE 的低位 PPN 必须满足相应对齐要求，否则是 misaligned superpage page fault。TinyOS 当前只创建 level-0 的 4 KiB 叶映射。

### `satp`

RV64 `satp`：

```text
63:60 MODE
59:44 ASID
43:0  root page-table PPN
```

```text
MODE=0: Bare
MODE=8: Sv39
```

根页表物理地址：

```c
root_pa = (satp & ((1ULL << 44) - 1)) << 12;
```

- 写 `satp` 不会隐式完成所需的 TLB 同步。
- TinyOS 当前 ASID=0，并依赖内核恒等映射来解引用页表物理页。

## 12. Sv39 PTE

```text
63        54 53                         10 9   8 7 6 5 4 3 2 1 0
+-----------+-----------------------------+-----+-+-+-+-+-+-+-+-+
| reserved/ |           PPN[2:0]          | RSW |D|A|G|U|X|W|R|V|
| extension |                             |     | | | | | | | | |
+-----------+-----------------------------+-----+-+-+-+-+-+-+-+-+
```

| 位 | 名称 | 含义 |
|---:|---|---|
| 0 | `V` | PTE 有效 |
| 1 | `R` | 可读 |
| 2 | `W` | 可写 |
| 3 | `X` | 可执行 |
| 4 | `U` | U-mode 可访问 |
| 5 | `G` | global mapping |
| 6 | `A` | accessed |
| 7 | `D` | dirty |
| 9:8 | `RSW` | supervisor 软件自用 |
| 53:10 | `PPN` | 物理页号 |

分类规则：

```text
V=0                         -> invalid
V=1 且 R=0,W=1              -> invalid/reserved
V=1 且 R=W=X=0              -> 指向下一级页表
V=1 且 R=1 或 X=1           -> 叶 PTE
```

权限检查：

- U-mode 只能访问 `U=1` 的页面。
- `U=0` 的内核页不能被 U-mode 访问。
- S-mode 默认不能对 `U=1` 页面做数据访问；需要时由 `sstatus.SUM` 控制。
- S-mode 不能从 U 页取指，即使 `SUM=1`。
- 写权限组合必须包含读权限；`W=1,R=0` 非法。
- 推荐 W^X：用户代码 `U|R|X`，用户栈 `U|R|W`。

`A/D` 位的处理取决于实现和相关扩展：硬件可能更新，也可能以 page fault 交给软件处理。TinyOS 当前创建映射时主动设置 `A`，可写页同时设置 `D`，避免依赖按需更新路径。

构造 PTE：

```c
pte = ((pa >> 12) << 10) | flags | V;
pa  = ((pte >> 10) & PPN_MASK) << 12;
```

中间 PTE 最稳妥的做法是只设置需要的 PPN 和 `V`，将 `R/W/X/U/A/D` 保持为 0。

## 13. TLB、`sfence.vma` 与 `fence.i`

### `sfence.vma`

它同步当前 hart 的地址翻译缓存与页表更新，并约束显式页表写入和后续隐式页表读取的顺序。

```asm
sfence.vma zero, zero   # 所有虚拟地址、所有 ASID
sfence.vma rs1, zero    # rs1 对应 VA、所有 ASID
sfence.vma zero, rs2    # 所有 VA、指定 ASID
sfence.vma rs1, rs2     # 指定 VA、指定 ASID
```

这里 `rs1` 提供虚拟地址，`rs2` 提供 ASID；不是物理地址。

常见场景：

| 操作 | 最低限度需要考虑的同步 |
|---|---|
| 激活新根页表 | 写 `satp` 并执行适当的 `sfence.vma` |
| 给活动页表增加映射 | 写 PTE 后刷新对应 VA |
| 修改活动映射权限/PA | 写 PTE 后刷新对应 VA |
| 解除活动映射 | 清 PTE 后刷新对应 VA，再回收页表/物理页 |
| 使用 ASID | 根据 VA/ASID 精确刷新；注意 global mapping |

TinyOS 当前没有 ASID，也只有单 hart 本地刷新，因此使用 `sfence.vma va, zero` 或全局刷新。

### `fence.i`

当一个 hart 先用 store 写入指令字节，随后要从这些字节取指时：

```asm
fence.i
```

`fence.i` 解决的是“数据写入”和“本 hart 后续取指”之间的可见性，不替代 `sfence.vma`：

- 页表映射变化：`sfence.vma`。
- 指令内容变化：`fence.i`。
- 两者都变化：两者都需要。
- 多 hart 系统还需要通知其他 hart 做远程同步；TinyOS 当前是单 hart。

## 14. 原子与内存顺序

项目启用了 A 扩展，但当前内核还没有并发锁。以后使用时先区分：

- `lr/sc`：load-reserved/store-conditional 循环。
- AMO：例如 `amoswap`、`amoadd`。
- `.aq`：约束后续内存操作不要跑到该原子操作之前。
- `.rl`：约束先前内存操作不要跑到该原子操作之后。
- `.aqrl`：同时具备 acquire/release 约束。

不要把以下概念混为一谈：

| 手段 | 约束对象 |
|---|---|
| C `volatile` | 主要约束编译器对该对象访问的优化 |
| inline asm `"memory"` | 编译器层面的内存重排 |
| `fence` | hart 观察到的一般内存/I/O 顺序 |
| `sfence.vma` | 地址翻译与页表遍历 |
| `fence.i` | 本 hart 后续取指 |

## 15. TinyOS 当前关键约定

这些是项目事实，不是通用 RISC-V 规则：

- `stvec` 使用 direct mode。
- `TrapFrame` 为 288 字节，显式保存原 `sp`。
- S-mode 运行时 `sscratch=0`。
- U-mode 运行时 `sscratch=当前任务内核栈顶`。
- trap handler 返回一个 `TrapFrame *`，允许调度器切换到另一个任务上下文。
- 用户代码 VA：`0x40000000`，权限 `U|R|X|A`。
- 用户栈 VA：`0x40001000`，栈顶 `0x40002000`，权限 `U|R|W|A|D`。
- 用户代码和栈目前与内核共享同一个根页表，隔离主要依赖 PTE 的 `U` 位。
- 当前没有 ASID、每任务页表、多 hart TLB shootdown 或安全的用户指针复制。
- 用户 syscall 当前约定 `a7=编号`、`a0=参数/返回值`。
- S-mode 调用 OpenSBI 使用 SBI 约定，不使用 TinyOS syscall 约定。

## 16. 常见故障速查

### `sret` 后立刻 instruction page fault（code 12）

- [ ] `sepc` 是 canonical user VA。
- [ ] user code PTE 存在且 `V|U|X` 正确。
- [ ] 中间页表 PTE 是合法非叶 PTE。
- [ ] `sstatus.SPP=0`。
- [ ] 写活动页表后做了 `sfence.vma`。
- [ ] 指令复制后做了 `fence.i`。
- [ ] `stval` 是否就是预期 user PC。

### 用户栈 load/store page fault（code 13/15）

- [ ] `sp` 位于已映射页内，而不是恰好落在未映射的栈顶地址上再向上访问。
- [ ] 栈 PTE 有 `V|U|R|W`，且没有非法 `W=1,R=0`。
- [ ] 访问宽度和地址对齐合理。
- [ ] `stval` 指向哪一个实际访问地址。

### `ecall` 无限重复

- [ ] handler 是否识别 exception code 8。
- [ ] 处理完成后是否执行 `sepc += 4`。
- [ ] 修改的是将被恢复的那个 TrapFrame。

### timer 没有进入 handler

- [ ] SBI `set_timer` 是否成功安排未来时间。
- [ ] `sie.STIE` 是否设置。
- [ ] OpenSBI 是否完成委托。
- [ ] `stvec` 是否有效。
- [ ] 返回上下文的 `SPIE/SIE` 是否合理。

### illegal instruction（code 2）

- [ ] 指令是否属于编译时启用的 ISA 扩展。
- [ ] 当前特权级是否允许访问该 CSR/执行该特权指令。
- [ ] 代码页内容是否正确复制，是否缺少 `fence.i`。
- [ ] `sepc` 对应位置的反汇编是什么。

### trap 返回后寄存器莫名变化

- [ ] 入口是否在覆盖临时寄存器前保存了原值。
- [ ] TrapFrame C 布局和汇编偏移是否一致。
- [ ] 恢复 `sp` 时是否仍在用旧 frame 地址取字段。
- [ ] handler/调度器返回的是不是正确任务的 frame。
- [ ] `gp`、`tp` 是否按上下文要求保存恢复。

## 17. 常用检查命令

```sh
# 强制构建，观察所有警告
make -B

# 看 ELF program headers 与 R/W/X 权限
riscv64-linux-gnu-readelf -lW kernel.elf

# 看 section、符号和地址
riscv64-linux-gnu-readelf -SW kernel.elf
riscv64-linux-gnu-nm -n kernel.elf

# 看最终实体指令；验证伪指令展开和地址
riscv64-linux-gnu-objdump -d kernel.elf
riscv64-linux-gnu-objdump -dS kernel.elf

# 限时 QEMU 集成验证
timeout 8s make qemu

# 工作区检查
git diff --check
git status --short --branch
```

调试 trap 时，至少同时记录：

```text
scause
sepc
stval
sstatus（尤其 SPP/SPIE/SIE）
satp
当前任务与 TrapFrame 地址
相关 VA 的 PTE/PA/flags
```

## 18. 去官方 PDF 搜什么

不必每次从头翻规范。根据问题在 PDF 中搜索这些英文关键词：

| 问题 | PDF | 搜索关键词 |
|---|---|---|
| 指令语义 | `riscv-spec.pdf` | 指令名，如 `JALR`, `ECALL`, `FENCE.I` |
| 寄存器用途 | `riscv-spec.pdf` | `programmers' model`, `calling convention` |
| CSR 指令 | `riscv-spec.pdf` | `CSR Instructions`, `CSRRW`, `CSRRS` |
| trap 入口/返回 | `riscv-privileged.pdf` | `Supervisor Trap`, `SRET`, `stvec`, `sepc` |
| 中断原因 | `riscv-privileged.pdf` | `scause`, `Supervisor Cause Register` |
| trap 附加值 | `riscv-privileged.pdf` | `stval` |
| 中断使能 | `riscv-privileged.pdf` | `sstatus`, `sie`, `sip` |
| 委托 | `riscv-privileged.pdf` | `medeleg`, `mideleg`, `trap delegation` |
| 分页模式 | `riscv-privileged.pdf` | `Sv39`, `satp` |
| PTE 权限 | `riscv-privileged.pdf` | `PTE`, `SUM`, `MXR`, `A bit`, `D bit` |
| TLB 刷新 | `riscv-privileged.pdf` | `SFENCE.VMA` |
| 等待中断 | `riscv-privileged.pdf` | `WFI` |

查到规范条文后，再回到以下项目文件确认 TinyOS 的具体实现：

- `kernel/trap_entry.S`：trap 保存、恢复和 U/S 栈切换。
- `include/trap.h`、`include/trap_offsets.h`：TrapFrame 布局。
- `kernel/trap.c`：cause 分发。
- `kernel/sched.c`：初始上下文和上下文切换。
- `kernel/vm.c`、`kernel/vm_arch.c`：Sv39 与 TLB 操作。
- `kernel/main.c`：当前内核/用户映射布局。
- `kernel/sbi.c`：S-mode 到 OpenSBI 的调用。
- `kernel/syscall.c`：U-mode 到 TinyOS 的 syscall。
