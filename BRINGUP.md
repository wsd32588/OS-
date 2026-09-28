# TinyOS 最小重做清单

> 用途：在空目录里按顺序重建当前 TinyOS。
> 每一步都给出「目标 / 可观察结果 / 验证方式 / 常见坑」。
> 细节事实看 `MEMORY.MD`，GDB 流程看 `DEBUGGING.MD`。
>
> 原则：**没跑通上一步，不要进入下一步；一次只引入一个概念。**

## 0. 固定约定

- QEMU `virt` + OpenSBI，内核入口 `0x80200000`。
- RAM `0x80000000..0x88000000`，即 `-m 128M`；不要随便改，否则 DTB 地址、PMM 上界都会变。
- boot hart 0；Device Tree 在 `0x87e00000`。
- RV64，`rv64imac_zicsr_zifencei`，ABI `lp64`，`medany`，`-mno-relax`。
- 4 KiB 页，Sv39，16 KiB boot stack。
- 内核编译：`-ffreestanding -fno-stack-protector -fno-pie -Iinclude -MMD -MP`。
- 纯净逻辑（PMM、Sv39）放 `test/` 用宿主机编译器验证；`satp`、`sfence.vma`、真实 page fault 才进 QEMU。

## 阶段总览

| # | 阶段 | 目标 | 最小验证 |
|---|---|---|---|
| 1 | ELF + boot stack | 能进入 C | `readelf` 入口 `0x80200000` |
| 2 | UART | 能打印 | QEMU 看到 `Hello TinyOS!` |
| 3 | TrapFrame + stvec | 能接 trap | `stvec` 指向 `trap_entry` |
| 4 | SBI timer | 能收定时器中断 | 周期出现 `[timer] tick` |
| 5 | PMM | 能分配物理页 | available pages > 0；`make test` |
| 6 | Sv39 纯逻辑 | PTE 编解码正确 | `make test` 通过 |
| 7 | 抢占式调度 | A/B 轮转 | A/B + tick + `entry returned` |
| 8 | 当前停止点 | 先不开 `satp` | 不写 U-mode / ELF |

---

## 阶段 1：最小 ELF + boot stack

- [ ] `linker.ld`：BASE `0x80200000`，`ENTRY(_start)`，`.text.entry` 放在最前，导出页对齐的 `__kernel_start`、`__kernel_end`。
- [ ] `kernel/entry.S`：`_start` → `la sp, boot_stack_top` → `call kernel_main` → 停机循环；`.bss.stack` 里放 16 KiB 栈，按 16 字节对齐。
- [ ] `kernel/main.c`：空 `kernel_main` 或只做停机。

**可观察**：`make` 成功；`kernel.elf` 是 RV64 ELF，入口 `0x80200000`。

**验证**：
```sh
make
riscv64-linux-gnu-readelf -h kernel.elf | grep Entry
riscv64-linux-gnu-readelf -lW kernel.elf | grep LOAD
```

**坑**：`_start` 必须 global；`boot_stack_top` 要 16 字节对齐；`call` 不返回；此时还没有 UART，不要急着打印。

---

## 阶段 2：UART hello

- [ ] `drivers/uart.c`：UART0 基址 `0x10000000`；THR offset 0；轮询 LSR offset 5 的 THRE（bit 5）后再写 THR。
- [ ] 提供 `uart_putchar`、`uart_puts`、`uart_put_hex`。
- [ ] `kernel_main` 打印 Hello、hart id、DTB 地址、内核范围。

**可观察**：
```text
Hello TinyOS!
Boot hart: 0x0000000000000000
Device tree: 0x0000000087e00000
Kernel range: 0x0000000080200000 - 0x0000000080208000
```

**验证**：
```sh
timeout 6s make qemu     # 退出码 124 正常
```
退出 QEMU：`Ctrl+A`，松开后按 `X`。

**坑**：UART 寄存器是 1 字节偏移；必须先轮询 THRE；`uart_put_hex` 固定打 16 位并不影响正确性。

---

## 阶段 3：TrapFrame + stvec

- [ ] `include/trap_offsets.h`：TrapFrame 各字段偏移 + 总大小 272，作为 C 与汇编的唯一来源。
- [ ] `include/trap.h`：`struct trap_frame`；用 `offsetof`/`sizeof` 加编译期断言。
- [ ] `kernel/trap_entry.S`：
  - `sp -= 272`；
  - 保存 30 个通用寄存器（`sp` 由 frame 地址隐式表示）+ `sepc`/`sstatus`/`scause`/`stval`；
  - `mv a0, sp; call trap_handler`；
  - `mv sp, a0;` 恢复 CSR 和寄存器；`sp += 272; sret`。
- [ ] `kernel/trap.c`：`trap_init` 写 `stvec`（direct mode）；未知 trap 打印 `=== TRAP ===`、`scause/sepc/stval`，然后停机。

**可观察**：正常启动；GDB 里 `stvec` 指向 `trap_entry`；临时放一条非法指令，能看到 `=== TRAP ===`。

**验证（GDB）**：
```gdb
p/x $stvec
x/8i $stvec
p/x $sepc
p/x $sstatus
```

**坑**：保存/恢复顺序必须和 C struct 完全一致；`sp` 不进 frame；`sepc` 偏移 240、`sstatus` 248；`stvec` 低 2 位是模式；`sret` 依赖 `SPP`/`SPIE`；可以故意改错 `a0` 偏移，确认静态断言会拦住。

---

## 阶段 4：SBI timer + S 定时器中断

- [ ] `kernel/sbi.c`：`ecall`，`a7 = 0x54494D45`（TIME 扩展），`a6 = 0`，`a0 = stime`。
- [ ] `kernel/timer.c`：`csrr time`；`timer_set_next(now + 10_000_000)`；`csrs sie, (1<<5)` 开 STIE；`timer_stop` 用 `csrc` 清 STIE。
- [ ] `kernel/trap.c`：`scause` 中断位 63 + code 5 → `timer_set_next`，打印 `[timer] tick`。
- [ ] 调用顺序：`trap_init()` → 任务准备 → `timer_init()`。

**可观察**：QEMU 周期输出 `[timer] tick`。

**验证（GDB）**：
```gdb
p/x $scause   # 预期 0x8000000000000005
p/x $sie      # 预期 0x20
p/x $sstatus
```

**坑**：S 模式 `ecall` 进入 OpenSBI，不是本项目 `stvec`（旧 `task_yield` 的教训）；全局中断要在 `sret` 后由 `SPIE` 打开；`timer_stop` 只清 STIE。

---

## 阶段 5：PMM

- [ ] `include/pmm.h`：`PAGE_SIZE`、`pmm_init`、`pmm_alloc_page`、`pmm_available_pages`、`pmm_free_page`。
- [ ] `kernel/pmm.c`：
  - `align_up(start)` / `align_down(end)`；
  - 顺序分配：`next_free_page` 前进，返回前清零；
  - intrusive free list：释放页的页首存 `next`，分配时优先复用并清零；
  - 释放校验：NULL、页对齐、下界、高水位，线性扫描拒绝重复释放。
- [ ] `kernel_main`：`pmm_init(__kernel_end, device_tree)`，打印 available pages。
- [ ] `test/test_pmm.c`：`aligned_alloc` 页对齐内存；基本边界 + 固定种子 20,000 次 alloc/free 压力测试。

**可观察**：QEMU 打印 `PMM initialized. Available pages: 0x...`（当前约 `0x7bf8`，不要写死）；`make test` 全部通过。

**验证**：
```sh
make test
timeout 6s make qemu
```

**坑**：PMM 上界用 DTB 地址，依赖 `-m 128M`；free list 的 `next` 会覆盖页内容，所以分配时必须清零；`pmm_init` 要重置 free list 和计数；宿主机测试必须用页对齐内存。

---

## 阶段 6：Sv39 纯逻辑（先不写 satp）

- [ ] `include/vm.h`：`PAGE_SHIFT=12`、`PPN_SHIFT=10`、`VPN_BITS=9`、`LEVEL_COUNT=3`、PTE flags、`Sv39Pte`、`Sv39PageTable`，`_Static_assert` 页表大小 = 4096。
- [ ] `kernel/vm.c`：
  - canonical 判断；
  - VPN 索引；
  - `sv39_make_pte` / `sv39_pte_physical_address`；
  - `sv39_pte_flags`、`sv39_pte_is_valid`、`sv39_pte_is_leaf`。
- [ ] `test/test_vm.c`：size、canonical、vpn、PTE 往返、valid/leaf、固定种子 10,000 次压力测试。

**必须记住的公式**：

```text
PTE = ((pa >> 12) << 10) | (flags & 0x3ff)
pa  = ((pte >> 10) & ((1<<44)-1)) << 12

VA: [38:30] VPN[2] [29:21] VPN[1] [20:12] VPN[0] [11:0] offset
canonical: bit63:39 必须等于 bit38
PTE flags 在 [9:0]，PPN 在 [53:10]
```

**可观察**：`make test` 通过；`kernel/vm.c` 用项目 flags 加 `-Wpedantic` 单独编译 0 warning。

**坑**：是 PPN 不是 PNN；flags 只占低 10 位；页号要放 bit 10；44 位 PPN 不要溢出；`Sv39PageTable` 类型只保证“大小一页”，不保证实例地址 4 KiB 对齐；宿主机测试里不要用 `uart_puts`。

**验证**：
```sh
make test
make compdb     # 让 clangd 同时看到内核和宿主测试
```

---

## 阶段 7：抢占式调度

- [ ] `include/sched.h` + `kernel/sched.c`：最多 2 个任务、每任务 4 KiB 栈、`READY/RUNNING/DEAD`。
- [ ] `task_create`：在任务栈顶构造初始 TrapFrame：
  - `sepc = task_trampoline`；
  - `sstatus = SPP | SPIE`（任务仍回 S 模式）；
  - `gp = read_gp()`（继承内核 gp）。
- [ ] `scheduler_start`：选第一个 READY，`task_enter(frame)`。
- [ ] `kernel/trap_entry.S` 增加 `task_enter`：`mv sp, a0; j trap_restore`。
- [ ] `sched_on_timer`：保存当前 frame；RUNNING → READY；找下一个 READY；返回它的 frame；没有 runnable 时打印 `[scheduler] no runnable tasks`、`timer_stop()`、停机。
- [ ] `task_trampoline`：调用任务入口；返回后打印 `[task] entry returned`；`task_exit()` 标记 DEAD 并 `wfi`。

**可观察**：A/B 交替输出 + `[timer] tick`；B 跑完 10 次后打印 `[task] entry returned`；全部 DEAD 后打印 `[scheduler] no runnable tasks`，且 `sie = 0`。

**验证**：
```sh
timeout 6s make qemu
make debug     # 另一个终端 gdb-multiarch kernel.elf
```

**坑**：任务仍在 S 模式（`SPP=1`）；不要把 S 模式 `ecall` 当 yield（会进 OpenSBI）；任务栈要 16 字节对齐；初始 frame 放在栈顶；`gp` 要继承；无 runnable 的停机循环还不是 idle task。

---

## 阶段 8：当前停止点

到这里就是当前 TinyOS 的全部范围。**先不要做**：

- 真正的 `satp` / `sfence.vma` / 页错误处理；
- U-mode、syscall、ELF 装载；
- 堆、C++ runtime、全局对象；
- 真正的 idle task、多核、设备驱动。

完成后才进入：

1. 给 `vm` 写真正的页表 walker/map（宿主机测试，注入页分配函数）；
2. 在 QEMU 里 identity-map 内核、写 `satp`、`sfence.vma`；
3. 再进入 U-mode + syscall。

---

## 速查：高频坑

- **TrapFrame**：偏移以 `trap_offsets.h` 为单一来源；`sp` 不存；272 字节；`sepc=240`、`sstatus=248`。
- **sret**：会 `SIE = SPIE`、按 `SPP` 决定特权级，然后把 `SPP` 清零；任务仍在 S 模式。
- **S 模式 ecall**：进 OpenSBI，不进 `stvec`。这是旧 `task_yield` 失败的原因。
- **timer**：`sie.STIE` 和 `sstatus.SIE` 是两个不同开关；`sret` 后才真正打开全局中断。
- **PMM**：start 向上取整、end 向下取整；free list 的 next 存在页首；无并发保护。
- **Sv39**：PTE 低 10 位 flags，PPN 从 bit 10 开始；`(pa>>12)<<10`；解码 `<<12`；canonical 看 bit 38。
- **宿主测试**：用 `printf/fprintf`，不要 `uart_puts`；头文件名要一致；不要把 `.h` 传给编译器（会生成 PCH）；失败用 `EXIT_FAILURE`。
- **Makefile**：`-MMD -MP` 自动依赖；`make compdb` 一次捕获内核 + 宿主测试两套构建；`make clean` 会删产物，不要当普通验证步骤。
- **Git**：提交前先 `git status`；不要提交 `.o/.d/kernel.elf/compile_commands.json/.cache`。

---

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

GDB 详细流程见 `DEBUGGING.MD`；当前已验证事实和待办见 `MEMORY.MD`。
