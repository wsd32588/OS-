# BATCH35 从 QEMU VirtIO 块设备读取扇区

> 中文节选：[Virtqueue 与 Split Virtqueue](VIRTIO_1_2_CN_SPLIT_VIRTQUEUE.md)、[设备初始化、MMIO 与块设备](VIRTIO_1_2_CN_MMIO_BLOCK.md)

2026-11-20 交付任务。2026-11-21 完成驱动、启动探针、固定 P0 和三题归档。

## 1. 为什么先做块读

TinyOS 的普通文件现在来自编进内核的只读字节。内核找到一个内置文件后，可以直接从那段内存复制数据。磁盘镜像里的字节由 QEMU 的设备管理。内核想读取未来的超级块、inode 或目录项，得先能告诉块设备读哪个扇区，再确认设备把对应字节写到了内存。

B35 对应 rCore 第 6 章的块设备接入。TinyOS 先在启动时通过驱动读取测试镜像的第 0、1 扇区。每次读 512 字节，两次读复用同一个队列。块写入、块缓存、磁盘 inode、目录树、持久文件和接入现有 FD 依次留给后续批次。[rCore 第 6 章的块设备接入](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter6/3using-easy-fs-in-kernel.html)。

### 一次读请求经过哪里

1. CPU 在 S 态启动。`kernel/boot/private/boot_stages.cpp` 的全局 C 链接函数 `boot_init_kernel_vm` 为内核建立根页表。目前它映射 UART 页 `0x10000000`，VirtIO 寄存器位于下一页 `0x10001000`。开启本批探针时，这个函数为相邻的 VirtIO 页也建立恒等映射。随后同文件的全局 C 链接函数 `boot_enable_paging` 激活内核根页表。
2. `kernel/boot/main.cpp` 的全局 C 链接函数 `kernel_main` 在分页生效之后、启动用户任务之前调用新增的 `kernel/boot/private/boot_stages.cpp` 全局 C 链接函数 `boot_probe_block_read`。这时 CPU 仍在 S 态，活动根页表是刚才建立的内核根。探针调用新增的 `kernel/drivers/virtio_block.cpp` 中 `tinyos::drivers::virtio_block_init`，让驱动通过 MMIO 寄存器识别并配置 QEMU 的块设备。MMIO 让 CPU 通过对指定地址的读写来访问设备寄存器。
3. 探针把扇区号和内核目标缓冲区交给 `kernel/drivers/virtio_block.cpp` 的 `tinyos::drivers::virtio_block_read`。驱动在内核的共享内存里填写请求头和描述符，再向 `QueueNotify` 寄存器写队列编号。QEMU 设备从描述符读取物理地址，据此读取请求头，并通过 DMA 把磁盘字节和状态写入驱动持有的内存。CPU 访问 MMIO 时会经过自己的根页表，QEMU 设备访问这批 DMA 内存时使用驱动提供的客体物理地址。
4. 设备通过已用环发布完成，驱动在 S 态轮询 `used.idx`。驱动确认完成项和状态后，把私有 DMA 数据区的 512 字节复制进探针提供的目标缓冲区，再允许下一次读复用这份请求内存。探针校验完整字节序列，最后把控制权交回 `kernel_main` 的启动流程。

这里的 split virtqueue 包含三块共享内存。描述符表记录每块请求内存的地址、长度和访问方向。驱动在可用环里公布这条链的头编号，设备在已用环里归还该编号和实际写入的长度。两个环各有自己的索引，驱动推进 `avail.idx`，设备推进 `used.idx`。协议字段见 [VirtIO 1.2 第 2.7、3.1、4.2、5.2 节](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)。

阅读提示可聚焦《操作系统：设计与实现》第三版里设备寄存器、轮询与 DMA 的部分，对照 CPU 写寄存器、设备访问请求内存、CPU 检查完成并复制字节这三个阶段。

## 2. 工程约定

本批使用 QEMU RISC-V `virt` 的固定 MMIO 槽位、现代 VirtIO MMIO（`Version = 2`）、split virtqueue 0 和轮询完成。驱动同时最多提交一个请求，启动探针是唯一调用方。启动探针使用 `BLOCK_PROBE=1` 启用，默认值为 `0`。设备发现以后可扩展到设备树，本批在 QEMU 命令中明确选择已实测的槽位。[QEMU RISC-V virt 平台说明](https://www.qemu.org/docs/master/system/riscv/virt.html)。

新增 `include/drivers/virtio_block.hpp` 声明以下内核接口，定义放在 `kernel/drivers/virtio_block.cpp`。

```cpp
#include <cstdint>
#include <span>

namespace tinyos::drivers {

int virtio_block_init(std::uintptr_t mmio_base,
                      std::uint64_t& sector_count) noexcept;
int virtio_block_read(std::uint64_t sector,
                      std::span<unsigned char> destination) noexcept;

} // namespace tinyos::drivers
```

| 操作 | 成功 | 失败 |
| --- | --- | --- |
| 初始化一次设备 | 返回 `0`，把设备报告的 512 字节扇区总数写入 `sector_count`，后续可读 | 返回 `-1`，设备维持不可读，`sector_count` 保留原值 |
| 读取一个扇区 | 目标长度恰好 512，且 `sector < sector_count`。返回 `0`，目标获得该扇区的 512 字节 | 未就绪、长度错误、越界、设备报告失败或轮询超时时返回 `-1`，目标缓冲区保留原字节 |

驱动在填写并发布请求前校验就绪状态、扇区号和目标长度。DMA 始终进入驱动自己持有的请求内存。驱动用完成后的复制承担“失败时目标保持原字节”的约定。对可能没有进展的轮询设置有限等待。提交之后若超时或无法确认完成项属于本次请求，驱动把设备标为本次启动不可用，保留全部队列和请求内存供可能迟到的 DMA 使用。启动期间禁止重新初始化或复用这份内存。

## 3. 实现任务与自查

### 任务一：接好构建开关和内核映射

1. 在 `Makefile` 增加 `BLOCK_PROBE ?= 0`，只接受 `0` 和 `1`。通过现有的 `build/kernel_config.h` 生成规则写出 `TINYOS_BLOCK_PROBE`。当值为 `1` 时，把 `kernel/drivers/virtio_block.o` 加入 `OBJS`，使它参与现有的配置头、依赖文件和链接规则。两种 `CONSOLE_MODE` 继续独立选择控制台。自查时连续切换 `BLOCK_PROBE=1` 和 `BLOCK_PROBE=0`，确认启动接线随配置重编译。
2. 在 `boot_init_kernel_vm` 中，探针启用时为 `[0x10001000, 0x10002000)` 添加一页恒等映射。`kernel/boot/private/kernel_vm.c` 的全局 C 函数 `map_identity_range` 接受起点及不包含在内的终点。同文件的全局 C 函数 `identity_mapping_is_present` 可验证映射。这页沿用 UART 的 `R | W | A | D`，省略 `U` 和 `X`。保留 UART 映射和已有根页表激活顺序。自查时用启用探针但无盘的 QEMU 启动，探针应读到空槽位并报告初始化失败，随后进入正常启动流程。

### 任务二：初始化现代设备和请求队列

在 `kernel/drivers/virtio_block.cpp` 的 `tinyos::drivers::virtio_block_init` 中实现以下顺序。寄存器名字和偏移查 [VirtIO 1.2 第 4.2.2 节的表 4.1](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)。

1. 用 32 位 MMIO 访问核对 `MagicValue = 0x74726976`、`Version = 2` 和 `DeviceID = 2`。`DeviceID = 0` 表示槽位空。当前 QEMU 10.2.1 已用 HMP 在启动前核对这三个寄存器，挂盘为 `0x74726976 / 2 / 2`，报告容量 `128` 扇区，无盘为 `0x74726976 / 2 / 0`。
2. 驱动把设备 `Status` 写成 `0`，有限等待设备读回 `0`，然后按序累加 `ACKNOWLEDGE` 和 `DRIVER`。读取设备特性，要求并协商 `VIRTIO_F_VERSION_1`（第 32 位，特性页 1 的第 0 位）。若设备提供 `VIRTIO_BLK_F_RO`，也接受该位。其他特性保持关闭。驱动累加 `FEATURES_OK`，读回确认设备接受这一组特性。
3. 选择 `QueueSel = 0`，检查 `QueueNumMax` 容得下本批的 8 个描述符。驱动清空并设置描述符表、可用环和已用环，分别满足 16、2、4 字节的最小对齐。驱动用 `QueueNum` 和三组低、高 32 位地址寄存器公布队列长度和各区起始物理地址，最后置 `QueueReady = 1`。轮询模式可用可用环的 `VRING_AVAIL_F_NO_INTERRUPT` 减少完成通知。已有 PLIC 和 trap 流程继续负责原来的设备。轮询时若读到设备已置位的通知，驱动可通过 `InterruptACK` 清掉对应位。
4. 从设备配置区读容量的低、高 32 位。驱动在两次读的前后各检查一次 `ConfigGeneration`，只有代数一致才接受该容量，重试次数有限。容量单位已经是 512 字节扇区。队列和容量都准备好后，驱动累加 `DRIVER_OK`，才向调用方写出容量并允许读。已识别的设备在协商或队列配置失败时，驱动按规范报告 `FAILED` 并返回 `-1`。自查时，给定测试镜像应得到容量 `128`，无盘时应在进入协商前返回。

描述符表、环和请求内存由驱动静态持有，布局校验用固定宽度字段与编译期断言。现有 `linker.ld` 把 `.bss` 纳入内核可写范围，`kernel/boot/entry.S` 的 `_start` 在调用 `kernel_main` 前清零该段。本批依赖这段内存在内核根下恒等映射，所以这些对象的地址数值可按此约定交给设备。驱动内部留清楚的转换位置，以便检查写给设备的是物理地址。

### 为什么这些内存要静态、清零并恒等映射

`BATCH35` 是第 35 批学习任务，`BAT35` 只是简写；它不是内核 API、变量或函数名，而是 `docs_from_riscv/tinyos/BATCH35_VIRTIO_BLOCK_READ.md` 这份任务单的编号。本批对应 rCore 第 6 章的块设备接入。

VirtIO split virtqueue 的 descriptor table、available ring、used ring，以及请求头/数据/状态，都是 **CPU 和设备共同访问的共享内存**。它们不能像普通局部变量那样放在栈上：

- 设备可能在本函数返回后继续 DMA，栈帧已经失效；
- 地址写入队列寄存器后不能再移动；
- 每次请求重新分配/释放会让设备可能访问已复用内存；
- 驱动需要在编译期检查字段偏移和 16/2/4 字节对齐。

所以驱动把它们声明为 `static`，放进 `.bss`：

- `static` 保证地址固定、生命周期覆盖整个内核；
- `.bss` 是 `NOLOAD`，不占 ELF 文件大小；
- `kernel/boot/entry.S` 在 `kernel_main` 前逐字节清零 `.bss`；
- `avail.idx` / `used.idx` 等字段必须从 0 开始，否则设备可能把随机垃圾当成待处理描述符。

最后一个关键是**恒等映射**。VirtIO 设备 DMA 使用 guest physical address（客体物理地址），而 CPU 代码里拿到的是内核虚拟地址。当前内核根页表把内核物理内存和 `[__kernel_end, DTB)` 都做成了 `VA == PA`，所以：

```text
reinterpret_cast<uintptr_t>(&g_descriptors) == g_descriptors 的 guest PA
```

驱动才能把 `&g_descriptors`、`&g_available`、`&g_used` 的数值直接写进 `QueueDesc`、`QueueDriver`、`QueueDevice` 寄存器。如果以后内核迁到高半区、`VA != PA`，这些地方必须改成显式的 `virt_to_phys()`，不能直接把 C++ 指针数值交给设备。

另外，`0x10000000`（UART）和 `0x10001000`（VirtIO MMIO）是 **设备寄存器页**，需要 CPU 能读写并做恒等映射；它们不是 DMA 数据区。DMA 数据区是上面这些 `.bss` 里的普通 RAM。

### 任务三：提交一条读请求并回收完成项

在 `kernel/drivers/virtio_block.cpp` 的 `tinyos::drivers::virtio_block_read` 中先执行第 2 节的输入校验，再填一条由三个描述符串起的请求。

| 描述符 | 数据 | 设备访问方向 |
| --- | --- | --- |
| 请求头 | 16 字节，依次为 `type = VIRTIO_BLK_T_IN`（0）、保留字段 `0`、64 位 `sector` | 设备读 |
| 数据 | 512 字节的驱动私有缓冲区 | 设备写 |
| 状态 | 1 字节，可用 `0xff` 作为初值，完成时由设备填写 | 设备写 |

驱动把描述符链的头编号写入 `avail.ring`，再发布新的 `avail.idx` 并通知队列 `0`。驱动保留自己的上一次已消费位置，用环下标访问当前项，用 16 位计数跟踪双方的推进。描述符表和环项的写入要先对设备可见，设备才可以看到新的 `avail.idx`。发布 `avail.idx` 后也要保证它先于 MMIO 门铃对设备可见。设备写出新的 `used.idx` 后，驱动用屏障保证随后读到该完成项及它对应的数据。RV64 可用带编译器 `memory` clobber 的 `fence iorw, iorw` 实现本批需要的顺序。轮询用设备可更新的内存访问方式重新读取 `used.idx`，MMIO 寄存器也使用显式访问。

读取完成项时核对链头编号、已写长度和请求状态。成功需要设备确认写了 512 字节数据与 1 字节状态，且状态为 `VIRTIO_BLK_S_OK`（0）。驱动消费已确认归还的环项，成功时向调用方复制整扇区。自查时，驱动应能先读扇区 `0`，拒绝扇区 `128`，再读扇区 `1`。成功项对应的两份结果不同，拒绝路径前后的队列位置和调用方缓冲区一致。[VirtIO 1.2 第 2.7.8 节的 used ring 与第 5.2.6 节的块请求协议](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)。

### 任务四：在启动时校验设备返回的字节

在 `kernel/boot/private/boot_private.h` 声明 `boot_probe_block_read`，在 `kernel/boot/private/boot_stages.cpp` 定义它，并让 `kernel/boot/main.cpp` 的 `kernel_main` 仅在启用探针时调用。调用顺序为 `boot_enable_paging` → `boot_probe_block_read` → 启动用户任务。探针缓冲区用于 CPU 比较，由于 DMA 写入的是驱动的静态数据区，探针可用局部数组。

探针从驱动取得容量，确认是测试镜像的 `128`，再校验扇区 `0` 的完整内容。接着探针用一个哨兵值填满目标缓冲区，尝试读取扇区号 `sector_count`，确认返回 `-1` 且每字节仍为哨兵值。最后校验扇区 `1` 的完整内容。以下串口行用于固定验收，两次读取与越界检查分别对应前三行。初始化失败时输出第四行。其他失败输出一条带阶段的 `[block]` 诊断并结束探针。探针返回后，`kernel_main` 继续既有启动流程。

```text
[block] sector 0 ok
[block] out-of-range rejected
[block] sector 1 ok
[block] init failed
```

## 4. 本机自查

在仓库根目录运行下面的命令。脚本为 QEMU 生成一张 64 KiB 的临时只读镜像。第 `sector` 扇区偏移 `offset` 的期望值为 `(17 + sector * 53 + offset) & 0xff`。每个扇区从自己的起始值计算，512 字节里包含 NUL 等二进制值。

```bash
make -j4 BLOCK_PROBE=1 CONSOLE_MODE=kdbg
image=$(mktemp /tmp/tinyos-b35.XXXXXXXX)
python3 - "$image" <<'PY'
import sys
from pathlib import Path

data = bytes((17 + sector * 53 + offset) & 0xff
             for sector in range(128) for offset in range(512))
Path(sys.argv[1]).write_bytes(data)
PY
qemu-system-riscv64 -machine virt -m 128M -nographic -bios default \
    -kernel kernel.elf -global virtio-mmio.force-legacy=false \
    -drive if=none,format=raw,readonly=on,id=b35disk,file="$image" \
    -device virtio-blk-device,drive=b35disk,bus=virtio-mmio-bus.0
```

用 `Ctrl+A` 再按 `X` 退出 QEMU。用同一个内核重复启动时可以去掉 `-drive` 和 `-device` 两个选项，验证空槽位。下一次常规构建使用 `make -j4 BLOCK_PROBE=0 CONSOLE_MODE=kdbg` 或 `make -j4 BLOCK_PROBE=0 CONSOLE_MODE=user`。

## 5. 固定 P0 与选做验证

作者完成驱动后，协作者接入启动探针并执行以下 P0。

| P0 | 通过条件 |
| --- | --- |
| 正常块读与队列复用 | 挂载给定镜像，驱动报告 128 扇区。在调度器启动前依次核对两份 512 字节内容，三条对应的 `[block]` 成功或拒绝日志按序各出现一次。 |
| 拒绝越界及保留目标 | `sector == sector_count` 返回 `-1`，整份目标缓冲区保留哨兵值。审查确认驱动在发布前拒绝，两次有效读共提交两次。 |
| 空槽位 | 使用同一个启用探针的内核启动，无 `-drive` 与 `-device`，只出现一条 `[block] init failed`，启动继续到控制台。 |
| 默认配置回归 | `BLOCK_PROBE=0` 下 `kdbg` 和 `user` 两种构建均通过，沿用各自的无盘启动流程。运行 `make test` 和 `make test-process`，保留 B34 的 139 个宿主场景、3 个 CTest 项与 27 个旧进程集成场景。 |

- **P1（选做）**用临时探针故意缩短等待，观察提交超时后驱动是否禁止下一次读，并检查设备仍可能访问的内存归属。少量探针改动，练习请求生命周期。
- **P2（选做）**连续读取超过 8 次，观察可用环和已用环的槽号回绕。少量测试代码，练习区分环容量与 16 位计数。

### 固定 P0 结果（2026-11-21）

- 挂载只读镜像后，S 态启动探针取得容量 128，逐字节核对扇区 0、1，中间对扇区 128 的调用返回 `-1`，目标的 512 个哨兵字节保持原值。三条 `[block]` 日志在启用分页后、启动调度器前按序出现。
- 内核进入 `kdbg>` 后，通过 QEMU Monitor 读取三个 16 位计数。驱动提交的 `g_available.index`、设备归还的 `g_used.index` 和驱动消费的 `last_used_index` 都是 `2`。两条有效读各提交并完成一次，越界请求在提交前返回。
- 同一探针内核在无盘时输出一次 `[block] init failed`，随后进入 `kdbg>`。切换到 `BLOCK_PROBE=0` 后，两种控制台配置均无告警链接，无盘分别进入 `kdbg>` 和 `tinyos>`。无效配置 `BLOCK_PROBE=2` 在构建前被拒绝。
- 隔离 `make test` 通过 139 个宿主场景和 3 个 CTest 项，QEMU 旧进程回归通过 27 项。构建、串口和计数记录在 `.cache/batch35-boot.cfY9yMlR/`，宿主和进程记录分别在 `.cache/batch35-host.ZH7ekss4/` 和 `.cache/batch35-process-final.3UbKuWt3/results/`。

## 6. 验收后的三道理解题

1. `kernel/boot/main.cpp` 的 `kernel_main` 已激活内核根页表，此时 CPU 要访问 `QueueNotify`，QEMU 设备也要访问刚公布的描述符表。两边各使用哪一种地址，哪一边会经过 CPU 的 `satp`？如果将来队列缓冲区改放在非恒等映射的区域，`kernel/drivers/virtio_block.cpp` 的 `tinyos::drivers::virtio_block_init` 要改变哪一步？
2. `kernel/drivers/virtio_block.cpp` 的 `tinyos::drivers::virtio_block_read` 已写好三个描述符，准备把扇区 1 的请求交给设备。请说明三个描述符的设备访问方向，以及设备或 CPU 在什么顺序错误下可能看到上一条请求的头、旧数据或旧状态。分别指出提交和完成两边应检查哪一个索引。
3. `kernel/drivers/virtio_block.cpp` 的 `tinyos::drivers::virtio_block_read` 遇到两个返回 `-1` 的场景。第一种是启动探针请求 `sector_count`，第二种是合法扇区的请求已经通知设备，但轮询超时。请比较这两种情况下描述符、DMA 数据区和调用方缓冲区分别由谁持有，以及哪些内存可以立刻用于下一次调用。

## 7. 三题原答与集中澄清（2026-11-21）

### 作者原答

#### 第一题

> qemu使用uint64_t, cpu 使用uintptr_t,cpu部分，因为要切换到qemu使用，QueueDesc部分

#### 第二题

> ```text
> 使用小端序进行访问，    // 驱动确认 QEMU 写了 512 字节数据与 1 字节状态，且状态 0 表示读盘成功。
>    if (completion.length != 513 || status_byte != 0) {
>        return -1;
>    }
>
>    // 驱动只在成功时把 DMA 缓冲区复制到调用方的 destination，保留失败时的原内容。
>    for (std::size_t i = 0; i < sizeof(buffer); ++i) {
>        destination[i] = buffer[i];
>    }这俩互换位置，提交见检查索引1，完成检查索引2
> ```

#### 第三题

> 第一种情况下描述符是系统持有，因为还未写入请求，DMA是驱动持有，还没有进行事务，buffer归调用发
> 第二种情况是硬件与系统共同持有，DMA硬件持有，buffer硬件持有

### 集中澄清

1. **区分 CPU 地址翻译和设备访问内存。** `kernel/drivers/virtio_block.cpp` 的 `tinyos::drivers::virtio_block_read`
   在 S 态向 `g_mmio_base + QueueNotify` 写队列号。CPU 用 `satp` 指定的内核根页表翻译这个虚拟地址。
   QEMU 收到通知后，用 `tinyos::drivers::virtio_block_init` 先前写入 `QueueDescLow/High` 的客体物理地址读取描述符表。
   两种整数类型只承载地址数值。当前恒等映射使队列内存的虚拟地址和客体物理地址数值相同。
   将来改成非恒等映射时，初始化函数要分别取得描述符表、可用环和已用环的物理地址，再写入
   `QueueDesc`、`QueueDriver` 和 `QueueDevice` 各自的高低位寄存器。
2. **区分描述符的设备权限和两个环的索引。** 小端序决定多字节字段的编码。QEMU 能否写对应内存由描述符的
   `WRITE` 位决定。QEMU 读取描述符 0 指向的请求头，写入描述符 1 指向的数据区和描述符 2 指向的状态字节。
   驱动写完请求头、描述符和 `g_available.heads[]` 后，经屏障推进 `g_available.index`，防止 QEMU 看到新索引却读到旧请求。
   QEMU 写完数据、状态及完成项后推进 `g_used.index`。驱动把它和 `last_used_index` 比较，经屏障后用旧的
   `last_used_index` 取完成槽，避免提前读取旧结果。驱动先检查完成长度和状态，再复制到 `destination`，这样失败时目标缓冲区保持原值。
3. **区分驱动持有请求内存与设备获得访问权。** 两种情况下，驱动都持有静态描述符表、请求头、内部的 512 字节 `buffer`
   和 `status_byte`。调用方都持有 `destination`。越界时，`virtio_block_read` 在发布前返回，QEMU 未收到这条请求，
   驱动下次可以立即使用这组描述符和 DMA 内存。提交后超时时，QEMU 已经取得这些地址，仍可能读请求头或写内部的
   `buffer` 和 `status_byte`。驱动将 `g_ready` 置为 `false`，保留这些内存，停止提交新请求。
   两条失败路径都没有复制数据到 `destination`，所以调用方都可以立即使用原来的缓冲区。
