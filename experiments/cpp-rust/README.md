# C++ / Rust 试水模块

这是当前 TinyOS 纯算法的独立语言实验。运行结果来自宿主测试；Rust 另外完成了 RISC-V 裸机目标交叉编译。这里的模块还没有链接到 `kernel.elf`，因此这些结果不能证明 C++/Rust 已在 QEMU 内核中执行。

## 这一版写了什么

| 模块 | C++17 | Rust | 对照来源 |
| --- | --- | --- | --- |
| Sv39 基础 | `constexpr` 函数、命名空间 | `const fn`、模块 | `kernel/vm.c` 的 canonical/VPN/PTE 基础函数 |
| PMM | `Pmm` 对象封装状态，借用调用者页池 | `Pmm<'a>` 独占借用页池，`Option` / `Result` 表达结果 | `kernel/pmm.c` 的顺序分配、LIFO 复用、清零、重复释放检查 |

PMM 返回页索引。索引 `i` 对应池中的第 `i` 页，测试将 C 的返回指针换算为相同索引后比较。空闲链表仍存在已释放页的开头，没有额外的堆分配或位图。C++ 使用 `INVALID_PAGE` 表示耗尽，Rust 使用 `None`。

试验 PMM 接收已经按 4 KiB 对齐的 `Page` 数组，因此没有移植 C 的任意地址区间对齐接口。它们保留分配策略，但不是可直接替换 C 接口的实现。页池由调用者拥有，分配器不负责释放整个池。

Rust 的独占借用会阻止多个分配器同时操作同一个池，也会阻止拿着 `page_mut()` 返回的引用时释放那一页。页索引没有代数标记：释放后不能继续保留旧索引作为有效分配凭证，因为该索引可能被复用。C++ 同样要求调用者正确管理索引和页指针的生命周期。

Sv39 编解码刻意保留当前 C 版本的 `0xff` flags 掩码，RSW 位不会被编码进去。三级 walker、map/query/unmap/reclaim、CSR、trap、UART 和调度器未移植。

## 怎么跑

在仓库根目录执行：

```bash
make -C experiments/cpp-rust test
make -C experiments/cpp-rust check-freestanding
make -C experiments/cpp-rust check-riscv
```

需要 `cc`、`g++`（或用 `CXX=clang++`）、`ar`、`rustc`。不使用第三方包、不需要 Cargo 下载依赖。`check-riscv` 需要已安装的 `riscv64gc-unknown-none-elf` Rust 目标。

- `test`：构建原 C 模块作为对照库，分别运行 C++、Rust 测试。Sv39 每种语言检查边界、全部 1024 个低位组合以及 20,000 个固定种子输入；PMM 每种语言检查耗尽、非法释放、重复释放、清零和 20,000 次固定种子分配/释放操作。每次操作还检查剩余页数。
- `check-freestanding`：C++ 用 `-ffreestanding -fno-exceptions -fno-rtti` 编译；Rust 编译 `no_std` 库，模块本体禁止 `unsafe`。测试程序可以使用宿主标准库；Rust 测试的 C FFI 调用使用局部 `unsafe`。
- `check-riscv`：把 Rust 库编译为 RISC-V 裸机目标的 `.rlib`，采用 `panic=abort`。这只确认目标库可编译，不含启动入口、panic handler、链接脚本或可启动镜像。该 Rust 目标的 `gc`/LP64D 与当前 C 内核的 `imac`/LP64 配置不同，正式混合链接前需要统一架构与 ABI。

构建产物全部放在本目录的 `build/` 并由本地 `.gitignore` 忽略。根目录 Makefile、C 内核和 sleep 草稿都未修改。

## 已验证（2026-10-02）

- GCC/G++ 14.3.0、rustc 1.98.1。
- 上述三个命令全部通过；C++、Rust 对照测试无失败。
- C++ 的 freestanding 对象目前只编译到宿主 x86-64；本机未发现 `riscv64-linux-gnu-g++`。Rust RISC-V 裸机目标编译通过。
- 尚未验证混合语言链接、QEMU 执行、panic/异常路径或完整页表管理。

## 阅读顺序

先对照 `cpp/sv39.hpp` 和 `rust/sv39.rs`，这两份几乎逐函数对应。再看 `cpp/pmm.hpp` / `cpp/pmm.cpp` 与 `rust/pmm.rs`，比较对象成员、独占借用和错误返回。最后看 `tests/`，那里把新版本和当前 C 实现放在同样的输入下执行。
