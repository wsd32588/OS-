# BATCH38 读取超级块与校验磁盘布局

2026-11-21 交付任务。作者实现超级块解码器，协作者负责构建接线、宿主测试和隔离 QEMU 验收。

## 1. 扇区 0 告诉内核什么

B37 的缓存能按扇区号取到 512 字节。现在内核需要知道这些扇区各自有什么用途。**超级块**是磁盘上
描述文件系统布局的记录。TinyOS v1 把它放在扇区 `0`。内核读到它以后，先核对格式和容量，再算出
后续区域的起点和长度。

在隔离验收里，`kernel/boot/private/boot_stages.cpp` 的全局 C 链接函数 `boot_probe_block_read`
由启动代码在 S 态、内核根页表激活后调用。它从 `kernel/drivers/virtio_block.cpp` 的
`tinyos::drivers::virtio_block_init` 取得设备扇区总数 `128`，再通过
`kernel/storage/block_cache.cpp` 的 `tinyos::storage::BlockCache::read` 读扇区 `0`。
驱动和缓存把 512 字节交给探针。探针把这些字节和设备容量一起传给
`kernel/storage/superblock.cpp` 的 `tinyos::storage::decode_superblock`。此时是内核 CPU 在
解析字节。只有解码成功，探针才使用返回的布局报告四个区域。

TinyOS v1 的整张文件系统镜像从设备扇区 `0` 开始，覆盖整个设备。区域顺序参考 rCore 的
`easy-fs`。TinyOS 自己定义魔数、版本和校验规则，使用 TinyOS 测试镜像做验收。本批只验证区域
边界，接下来再解释位图的位、inode 的字段和数据区的内容。[rCore 磁盘布局与超级块](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter6/2fs-implementation.html)

配套阅读《操作系统：设计与实现》第三版第 5 章 §5.6 中的「文件系统布局」。阅读时看超级块如何
定位后续区域，对照下面的扇区范围即可。[出版社目录](https://www.informit.com/store/operating-systems-design-and-implementation-9780131429383)

## 2. 磁盘格式与样例

磁盘记录恰好占一个 512 字节扇区。表中偏移以字节计，所有字段宽度为 4 字节，采用小端顺序，
即数值的最低 8 位在最低地址。偏移 `28` 到 `511` 是保留区域，v1 要求每个字节为 `0`。

| 字节偏移 | 字段 | 128 扇区样例 |
| --- | --- | --- |
| `0` | 魔数 `magic`，用于识别格式 | `0x31534f54`，磁盘上的四个字节为 `54 4f 53 31`（`TOS1`） |
| `4` | `version` | `1` |
| `8` | `total_sectors`，包括扇区 `0` | `128` |
| `12` | `inode_bitmap_sectors` | `1` |
| `16` | `inode_table_sectors` | `4` |
| `20` | `data_bitmap_sectors` | `1` |
| `24` | `data_sectors` | `121` |

各区域从扇区 `1` 起连续排列。下表用 `[起点, 结束)` 表示范围，结束值属于下一区域或是设备边界。
例如 `[2, 6)` 实际包含扇区 `2`、`3`、`4`、`5`。

| 区域 | 样例覆盖范围 | 后续用途 |
| --- | --- | --- |
| 超级块 | `[0, 1)` | 描述磁盘布局 |
| inode 位图 | `[1, 2)` | 标记哪些 inode 编号已分配 |
| inode 表 | `[2, 6)` | 存文件及目录的元数据，例如文件长度和数据块位置 |
| 数据位图 | `[6, 7)` | 标记哪些数据扇区已分配 |
| 数据区 | `[7, 128)` | 存放文件内容和目录项 |

## 3. 接口与作者实现任务

在 `include/storage/superblock.hpp` 声明以下接口，在 `kernel/storage/superblock.cpp` 实现。
返回的结构是已经解码的内存值，各区域起点由解码器计算，磁盘上只存长度。

```cpp
#pragma once

#include <cstdint>
#include <span>

namespace tinyos::storage {

struct SectorRange {
    std::uint32_t start_sector{};
    std::uint32_t sector_count{};
};

struct SuperblockLayout {
    std::uint32_t total_sectors{};
    SectorRange inode_bitmap{};
    SectorRange inode_table{};
    SectorRange data_bitmap{};
    SectorRange data{};
};

int decode_superblock(
    std::span<const unsigned char> bytes,
    std::uint64_t device_sector_count,
    SuperblockLayout& out
) noexcept;

}
```

1. **先检查输入长度，再解码字段。** `bytes.size()` 必须等于 `512`。之后按固定偏移组合每组四个
   字节。可在 `.cpp` 写一个私有小端读取函数。移位前将字节转成 `std::uint32_t`，例如高位字节
   用 `static_cast<std::uint32_t>(bytes[offset + 3]) << 24`。这样也能读取未经对齐的输入。
   项目里可参考 `kernel/user/user_elf.c` 的私有函数 `read_little_endian`。磁盘格式按上表解码，
   内存结构的对齐和填充由编译器管理。
2. **验证格式与范围。** 核对魔数和版本，确认整个保留区为零，四个区域的长度都大于零，磁盘头的
   `total_sectors` 等于驱动报告的 `device_sector_count`。v1 只有 32 位总数，超过此范围的设备
   无法通过比较。然后在 64 位中计算 `1 + 四个区域的扇区数`，结果必须等于 `total_sectors`。
   从表达式第一个加数开始就用 64 位，例如 `std::uint64_t{1} + …`。算完 32 位结果再赋给 64 位
   变量，无法找回已经回绕的高位。
3. **全部通过后才交付结果。** 从扇区 `1` 开始，逐次加前一区域的长度来算起点。所有范围通过后，
   这些起点才可以转换成 32 位保存。先填局部 `SuperblockLayout` 候选，最后一次赋给 `out` 并返回
   `0`。任何拒绝都返回 `-1`，调用方原来的 `out` 各字段保持原值。调用方以本次返回值决定能否使用
   布局。解码函数只处理传入字节，设备读取由探针负责。

本批保持既有 B35 探针和默认启动。协作者在 `BLOCK_PROBE=1` 分支接入新的对象文件，隔离 QEMU
探针以只读方式绑定现有块缓存。位图容量是否足以管理未来的 inode 和数据、分配状态是否一致，以及
根目录是否存在，由后续批次在定义了对应格式后校验。

## 4. 固定 P0 与验证入口

协作者在作者完成解码器后执行下面的验收。宿主输入由固定小端字节生成，失败用预填值检查 `out`
全部字段。QEMU 挂载两张只读的 64 KiB 测试镜像，各启动一次。

| 场景 | 通过条件 |
| --- | --- |
| 宿主 · 合法解码 | 128 扇区样例得到四对 `(起点, 长度)`，依次为 `(1,1)`、`(2,4)`、`(6,1)`、`(7,121)`。 |
| 宿主 · 格式拒绝 | 511 字节、错误魔数、未知版本或保留区非零均返回 `-1`，预填输出保持。 |
| 宿主 · 布局拒绝 | 声明总量和设备容量不同，区域长度为零，区域总和与头部声明不符，均拒绝。`total_sectors = 128` 且四个计数为 `UINT32_MAX, 1, 1, 126` 时也拒绝，即使 32 位求和会回绕到 `128`。输出保持。 |
| QEMU · 读盘与解码 | 合法镜像打印上述范围，坏魔数镜像打印一次拒绝。两份均只提交一次扇区 `0` 的读请求，提交、归还、消费计数为 `1/1/1`，随后正常进入启动流程。 |
| 既有回归 | 正式 B35 探针继续读扇区 `0`、拒绝 `128`、读扇区 `1`。默认 `kdbg`、`user` 构建通过。`make test` 与 `make test-process` 通过。 |

宿主可用 `struct.pack("<7I", 0x31534f54, 1, 128, 1, 4, 1, 121)` 生成 28 字节头部，
然后把剩余镜像填零至 `128 * 512` 字节。坏魔数镜像只替换第一个字节。正式 B35 验证继续用
它自己的测试图案镜像。隔离探针沿用 B36 的 QEMU 命令，使用 `readonly=on`。
[QEMU 挂盘命令](BATCH36_VIRTIO_BLOCK_WRITE.md#4-固定-p0-与验收方法)

```bash
make test
make test-process
make -j4 BLOCK_PROBE=1 CONSOLE_MODE=kdbg
make -j4 BLOCK_PROBE=0 CONSOLE_MODE=kdbg
make -j4 BLOCK_PROBE=0 CONSOLE_MODE=user
```

## 5. 验收后的三道理解题

1. 扇区 `0` 中魔数的四个字节依次为 `54 4f 53 31`。CPU 解码时，每个字节放到结果的哪一段？
   数据区的起点为什么是 `7`？计算用到了哪些区域长度？
2. 设备和头部都说共有 `128` 个扇区，但四个区域的长度为 `UINT32_MAX, 1, 1, 126`。分别用
   32 位和 64 位计算总长度会得到什么？为什么先做 32 位加法再赋给 `uint64_t` 仍有问题？
3. 同一个 `out` 先保存了一次成功解码的布局。随后另一个缓存对象从自己绑定的设备读出扇区 `0`，
   读成功但解码的版本校验失败。设备已经完成了哪个 I/O？`out` 保存的是哪一次的布局？调用方
   能否据此读取第二个设备的 inode 表？
