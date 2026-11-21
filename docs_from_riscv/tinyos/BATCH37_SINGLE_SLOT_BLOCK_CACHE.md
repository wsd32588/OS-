# BATCH37 单槽块缓存与显式写回

2026-11-21 交付任务。作者完成缓存模块，固定 P0 通过，三道理解题已归档。

## 1. 从重复访问扇区 7 开始

B36 的 `kernel/drivers/virtio_block.cpp` 提供 `tinyos::drivers::virtio_block_read` 和
`tinyos::drivers::virtio_block_write`。如果上层连续读两次扇区 `7`，直接调用驱动就会提交两条
VirtIO 请求。块缓存把最近的一份扇区内容留在内存，第二次访问可以直接取得这份数据。本批一个块
就是一个 512 字节扇区，只保留一个缓存槽。

1. S 态启动探针先激活内核根页表，再通过 `tinyos::storage::BlockCache::read`（实现放在
   `kernel/storage/block_cache.cpp`）读扇区 `7`。槽为空，缓存调用底层读。B36 驱动等 QEMU
   把数据送进自己的 `g_dma_buffer`，再将它复制到缓存拥有的 `data_`。缓存记录 `sector_ = 7`，
   标记 `valid_ = true`、`dirty_ = false`，然后复制 512 字节给调用方。第二次读 `7` 时直接从
   `data_` 复制，这次称为**命中**。
2. 同文件的 `tinyos::storage::BlockCache::write` 接收扇区 `7` 的完整新内容，将其复制到
   `data_` 并设 `dirty_ = true`。**脏**表示缓存有一份还没确认写回的内容。此时再从缓存读 `7`，
   会取得新内容。驱动还没有收到写盘请求。
3. 同文件的 `tinyos::storage::BlockCache::flush` 按 `sector_` 写回当前槽。B36 驱动先把
   `data_` 复制到自己的 DMA 缓冲区，再交给 QEMU 写盘。底层返回 `0` 后，缓存保留扇区号和内容，
   只清除 `dirty_`。因此下一次读 `7` 仍能命中。
4. 如果脏槽 `7` 在读 `8` 时必须腾出，缓存先写回 `7`。写回失败时保留扇区号、数据和脏标记，
   本次读 `8` 直接失败。等写回能成功，再尝试读取 `8`。这一阶段称为**替换**。

缓存层的 `data_` 跨多次调用保留一个扇区。驱动的 `g_dma_buffer` 为单条 VirtIO 请求暂存数据。
两块数组用途和寿命约束不同，缓存只通过 B36 的公开读写接口接触设备。

可配套阅读《操作系统：设计与实现》第三版第 5 章中 MINIX 3 的「块缓存」（5.6.5），关注缓存中
如何识别块和记录修改。rCore 第 6 章的 `easy-fs` 也使用块号与修改标记，本批先用一个槽练习显式
处理写回结果。[rCore 块缓存层](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter6/2fs-implementation.html)

## 2. 接口和状态

新建 `include/storage/block_cache.hpp`，所属命名空间为 `tinyos::storage`。下面是建议的完整接口
和初始状态。新对象绑定一个设备，初始槽为空。

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace tinyos::storage {

struct BlockCacheIo {
    int (*read)(std::uint64_t, std::span<unsigned char>) noexcept{};
    int (*write)(std::uint64_t, std::span<const unsigned char>) noexcept{};
};

class BlockCache {
public:
    constexpr BlockCache(std::uint64_t sector_count, BlockCacheIo io) noexcept
        : sector_count_{sector_count}, io_{io} {}

    BlockCache(const BlockCache&) = delete;
    BlockCache& operator=(const BlockCache&) = delete;
    BlockCache(BlockCache&&) = delete;
    BlockCache& operator=(BlockCache&&) = delete;

    int read(std::uint64_t sector, std::span<unsigned char> destination) noexcept;
    int write(std::uint64_t sector, std::span<const unsigned char> source) noexcept;
    int flush() noexcept;

private:
    static constexpr std::size_t kSectorSize = 512;
    std::uint64_t sector_count_{};
    BlockCacheIo io_{};
    std::uint64_t sector_{};
    bool valid_{};
    bool dirty_{};
    unsigned char data_[kSectorSize]{};
};

}
```

- `io.read` 由构造者提供，必须有效。`io.write == nullptr` 表示只读绑定。容量与回调在缓存对象的
  寿命内保持固定。同一设备由一个缓存统一管理，调用方逐次调用接口。QEMU 验收只用直接读来观察
  写回前后的设备内容。
- 设备回调同步处理一整个扇区，`0` 成功、`-1` 失败。底层读失败时必须保留传入目标的全部字节。
  两个回调返回后都不再借用传入的 span。B36 的公开读写接口满足此约定，因此本批可以直接把
  `data_` 交给底层读用于回填。
- `valid_` 说明当前槽已经持有一个扇区。初始 `sector_` 的数值虽然为 `0`，只有 `valid_` 为真才
  能靠它判定命中。`dirty_ == true` 要求 `valid_ == true`。有效槽的 `sector_` 与 `data_`
  始终属于同一扇区。
- 两个入口只接受长度恰好为 512 的 span 和 `sector < sector_count_`。写入口还要检查可写绑定。
  无效请求在修改缓存或调用底层前返回 `-1`。读返回 `-1` 时保留调用方的 `destination`。
- `write` 返回 `0` 表示缓存已接收副本，调用方可立即复用 `source`。`flush` 返回 `0` 表示该槽的
  修改已得到底层写入确认。掉电持久化以后单独处理。缓存不在析构函数里发起 I/O，需要保留数据的
  调用方在结束缓存寿命前显式写回并处理返回值。

## 3. 实现任务

作者在 `kernel/storage/block_cache.cpp` 实现下列三个成员。协作者负责在 `BLOCK_PROBE=1` 的
构建分支接入缓存对象文件，以及宿主测试和隔离 QEMU 探针。正式 B35 探针保留原路径。

| 函数 | 操作顺序 | 自查现象 |
| --- | --- | --- |
| `BlockCache::flush` | 空槽或干净槽直接返回 `0`。脏槽用 `io_.write(sector_, data_)` 写回。成功才清 `dirty_`，保留驻留扇区。 | 连续两次成功写回同一份修改，底层只收到一次写。失败时保留旧扇区、字节和脏标记。 |
| `BlockCache::read` | 校验参数。命中时直接复制给 `destination`。未命中时先用 `flush` 处理旧槽，再调用 `io_.read(sector, data_)`。读成功才更新扇区号和有效标记，再复制给调用方。 | 旧写回失败时没有新读。旧写回成功但新读失败时，旧槽仍有效且干净，调用方的目标缓冲区保持原样。空槽回填失败仍为空。 |
| `BlockCache::write` | 校验参数和可写能力。未命中时先用 `flush` 处理旧槽。然后复制 `source` 的全部 512 字节，设置新扇区号、有效标记和脏标记。 | 整扇区覆盖不需要预读目标扇区。命中连续写在一次 `flush` 前均只改内存。若旧写回失败，旧槽完整保留。 |

建议按表的顺序实现。写回使用旧槽的 `sector_`。如果提前填入新扇区号，旧数据就可能被写到错误
的位置。写回 `-1` 时保留脏数据，因为 B36 的超时可能发生在设备已经开始处理之后。底层停用时，
缓存仍可从命中的槽读取已有数据。下一次需要访问设备的操作会按底层返回值处理失败。

## 4. 固定 P0 与验证命令

P0 由协作者负责。宿主测试调用真实缓存模块，用内存模拟设备记录每次回调的方向、扇区和结果。
QEMU 用 B36 的真实 VirtIO 驱动，唯一临时接线在隔离启动探针。

| 场景 | 通过条件 |
| --- | --- |
| 宿主 · 空槽、命中和干净替换 | 从空槽读 `0`，再读 `0`、`1`，三次数据各正确。底层只读 `0`、`1` 各一次，无写。空槽和干净槽的 `flush` 成功且无 I/O。 |
| 宿主 · 整扇区写和显式写回 | 从空槽写 `7`，再次写 `7`，再读取 `7`，读到最后一份写入。底层仍无读写。连续调用两次 `flush`，只写一次 `7`，字节精确匹配，随后读 `7` 仍命中。再次写脏 `7` 后写 `8`，底层只追加一次写 `7`，读 `8` 命中新数据。 |
| 宿主 · 替换失败与重试 | 写脏 `7` 后让底层第一次写 `7` 失败。读 `8` 返回 `-1`，保留调用方哨兵，底层未读 `8`。读 `7` 命中旧脏数据。再读 `8` 时允许写回 `7`，故意让读 `8` 失败，旧 `7` 仍命中。下一次合法读 `8` 只追加一次底层读。三个替换尝试的累计轨迹为 `W7(失败), W7(成功), R8(失败), R8(成功)`。 |
| 宿主 · 提交前拒绝 | 511 字节读写、`sector == sector_count_` 的读写、只读绑定的写各返回 `-1`。拒绝前后的 I/O 计数相同。读目标哨兵保持，已有缓存槽仍可命中。 |
| QEMU · 写回边界 | 挂一份 128 扇区可写临时镜像。缓存读 `7` 两次、写 `7`、缓存读回新内容、用驱动读出旧盘内容、两次 `flush`、用驱动读出新盘内容。真实设备依次收到 `IN, IN, OUT, IN`，提交、归还、消费均为 `4`。正常退出后比较整份镜像，只有扇区 `7` 与原镜像不同。 |
| 旧行为 | `make test`、`make test-process` 通过。默认 `kdbg`、`user` 构建通过，正式 B35 探针仍能读已知扇区。 |

协作者完成接线后使用以下验证入口。QEMU 写回探针单独挂载可写临时镜像，沿用 B36 的镜像生成
公式和退出后全镜像比较方法。[B36 验证命令](BATCH36_VIRTIO_BLOCK_WRITE.md#4-固定-p0-与验收方法)

```bash
make test
make test-process
make -j4 BLOCK_PROBE=1 CONSOLE_MODE=kdbg
make -j4 BLOCK_PROBE=0 CONSOLE_MODE=kdbg
make -j4 BLOCK_PROBE=0 CONSOLE_MODE=user
```

## 5. 验收后的三道理解题

1. 缓存刚创建，`sector_` 初始化为 `0`。第一次和第二次读扇区 `0`，分别由哪层提供数据？
   `valid_` 在两次调用之间起什么作用？
2. 缓存已有脏槽 `7`，调用方发来完整的扇区 `8` 新内容。写回旧槽成功时，底层一共收到哪些请求？
   紧接着从缓存读 `8` 与直接从磁盘读 `8`，两边可能看到什么？
3. 另一场景里，缓存有脏槽 `7`，调用方把带哨兵的数组交给 `read(8, destination)`。分别说明
   写回 `7` 失败，以及写回 `7` 成功但读 `8` 失败时，槽内的扇区号、字节、脏标记和调用方目标
   各是什么状态。两种情况下一次重新读 `8` 时，缓存会先尝试哪条操作？假设底层允许再次请求。

## 6. 实现与固定 P0 结果（2026-11-21）

作者完成 `include/storage/block_cache.hpp` 和 `kernel/storage/block_cache.cpp` 中的
`tinyos::storage::BlockCache`。协作者把缓存对象接入 `BLOCK_PROBE=1` 构建，并补齐宿主测试
`test/test_block_cache.cpp`。正式 B35 启动探针保持原路径，B37 的写回探针在隔离副本运行。

- 宿主新增四组均通过。命中和干净替换只读 `0`、`1` 各一次。整扇区写在显式写回前只改缓存。
  模拟设备记录失败替换轨迹 `W7(-1), W7(0), R8(-1), R8(0)`，前两次失败均保留调用方目标，
  旧槽先保留为脏、成功写回后保留为干净。短 span、扇区越界和只读写入均提前拒绝。
- 隔离 QEMU 在写回前分别从缓存和驱动读到新、旧两份扇区 `7`。两次 `flush()` 只产生一次
  写盘，底层随后读到新内容。提交、归还、消费计数均为 `4`，已用环长度依次为
  `513, 513, 1, 513`。QMP 正常退出后，宿主全量比较 64 KiB 镜像，只有扇区 `7` 改变。
- 正式 B35 探针仍完成读 `0`、拒绝 `128`、读 `1`，无盘报初始化失败后继续启动。默认 `kdbg`
  与 `user` 两种配置无警告构建并启动到提示符。GNU 宿主共通过 143 个场景、3 项 CTest，
  `make test-process` 通过 27 项。

宿主证据在 `.cache/batch37-host.H1qzDqjL/test.log` 与同目录下的
`build/Testing/Temporary/LastTest.log`。隔离探针、串口与队列观测见
`.cache/batch37-qemu.py18mbz5/results.json`。缓存头和实现、VirtIO 驱动及 Makefile 的隔离快照
与工作区哈希一致。进程回归见 `.cache/process-regression/20261121-231258-3/results.json`。

## 7. 三题原答与集中澄清（2026-11-21）

### 第 1 题 · 空槽和命中

> 第一次从硬盘，第二次从缓存获取，valid_标识是否可以从缓存读取数据

第一次读扇区 `0` 时槽还无效，缓存向驱动读入 `0`。成功后，槽记录 `valid_ = true` 和
`sector_ = 0`。第二次读 `0` 命中，直接复制槽中内容。完整命中条件是 `valid_ && sector_ == sector`。

### 第 2 题 · 整扇区覆盖与延迟写回

> 一次写和一次读，需要flush和一次read,但是本次应该是read里直接调用flush，缓存和磁盘内数据不同？

三个调用合计确实是一写一读。`BlockCache::write(8, source)` 先用 `flush()` 写回旧扇区 `7`，
再把 `8` 的完整新内容复制到槽里并标脏。`source` 已提供全部 512 字节，无需预读扇区 `8`。
之后的缓存读命中 `8`，得新数据且无底层 I/O。探针直接调用驱动读 `8` 时，底层才收到这一次读，
得到的是磁盘上的旧数据。

### 第 3 题 · 换槽失败后的状态与重试

> 写回7失败时保留槽内内容，读取8失败时保留sector_,destination里面有写入的一部分内容，下次读8，第一条底层IO是清缓存

两种失败都让 `destination` 保持原样。`BlockCache::read` 先尝试写回，再把读请求交给底层，
最后才把 `data_` 复制到调用方。底层读失败时，B36 的驱动也会保留传入的 `data_`。

| 本次失败位置 | 留在缓存里的数据 | 重试读 `8` 时第一条底层 I/O |
| --- | --- | --- |
| 写回 `7` 失败 | 扇区号 `7`、原缓存字节、有效且仍脏 | 再写回 `7` |
| `7` 已写回，读 `8` 失败 | 扇区号 `7`、同样的缓存字节、有效且干净 | 直接读 `8` |

第二种情况下，重试可能仍经过 `flush()`。干净槽的 `flush()` 直接返回 `0`，保留缓存槽且无
设备 I/O。此处重试以底层仍能接收请求为前提。第 6 节的宿主失败轨迹已覆盖两个分支。
