# BATCH36 向 QEMU VirtIO 块设备写入扇区

2026-11-21 交付任务。驱动实现、固定 P0 和三道理解题均已收尾。

## 1. 为什么先做块写

B35 的内核可以把指定扇区读到内存。以后文件系统修改目录项或 inode 时，内核也需要把一扇区的新字节交给磁盘。B36 先打通这条路径。启动时的测试代码写入指定扇区，立刻通过 B35 的读接口读回，再到宿主机检查磁盘镜像中实际改变的字节。

本批只实现一个 512 字节扇区的写入和读写交替。块缓存、FLUSH 命令和掉电后的持久化语义留到后续。宿主机在 QEMU 正常退出后复核镜像，覆盖的是这次正常运行中数据确实进入了镜像文件。

### 跟一条写扇区 7 的请求

1. `kernel/boot/main.cpp` 的全局 C 链接函数 `kernel_main` 已经激活内核根页表。隔离验收中的启动探针在 S 态调用 `kernel/drivers/virtio_block.cpp` 新增的 `tinyos::drivers::virtio_block_write`，交给驱动一个扇区号和一份 512 字节的 `source`。扇区 `7` 表示镜像里的第 `3584` 字节开始的一扇区。`source` 是探针持有的内核缓冲区。
2. `virtio_block_write` 先确认设备就绪、介质可写、扇区合法且 `source` 长度恰好为 512。驱动把 `source` 复制到自己的静态数据区，再填写请求头。这样设备取得的是驱动自己持有的地址，调用就算超时返回，设备仍可安全访问这份静态内存。
3. 驱动沿用 B35 的描述符链 `0 → 1 → 2`。描述符 `0` 指向请求头，QEMU 从中取得操作类型和扇区号。描述符 `1` 指向驱动的 512 字节数据区。写盘时 QEMU **读取**这块内存，再把字节写到镜像。描述符 `2` 指向结果字节，QEMU 用它报告写盘结果。`WRITE` 表示设备可写该描述符指向的内存，所以写盘请求的数据描述符 `1` 只设置 `NEXT`。[VirtIO 1.2 块请求协议 §5.2.6](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)。
4. 驱动把链头编号 `0` 放进 `g_available.heads[]` 的当前提交槽，经屏障推进 `g_available.index`，再通知队列 `0`。QEMU 完成后把结果写进 `status_byte`，在 `g_used.elements[]` 写入完成记录，然后推进 `g_used.index`。本次设备只写回 1 字节状态，因此成功写入的 `completion.length` 是 **1**。读盘时设备还会写入 512 字节数据，完成长度才是 **513**。[VirtIO 1.2 已用环 §2.7.8](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html)。
5. 驱动消费写请求的完成项后，探针调用同文件的 `tinyos::drivers::virtio_block_read`，读取同一扇区。下一次读从同一份 `last_used_index` 继续消费。链头编号仍是 `0`，可用环和已用环里的槽号随请求推进。这也是读写共用提交与完成代码的原因。

中文参考见 [MMIO 与块设备节选](VIRTIO_1_2_CN_MMIO_BLOCK.md)。本批用到的是块请求中的 `OUT`、只读特性 `RO` 和已用环的完成长度。

## 2. 工程约定

在 `include/drivers/virtio_block.hpp` 增加下面的声明，所属命名空间为 `tinyos::drivers`。

```cpp
int virtio_block_write(
    std::uint64_t sector,
    std::span<const unsigned char> source
) noexcept;
```

| 条件 | 结果 |
| --- | --- |
| 成功写一扇区 | 驱动返回 `0`。完成项属于本次请求，长度为 `1`，`status_byte == 0`。 |
| 设备未就绪、介质只读、`source.size() != 512` 或 `sector >= sector_count` | 驱动在复制数据和提交描述符前返回 `-1`，队列计数保持原值。 |
| 设备归还了本次请求，但状态或长度检查失败 | 驱动消费完成项并返回 `-1`。 |
| 提交后等待超时，或索引、链头编号无法确认本次请求已经归还 | 驱动设置 `g_ready = false` 后返回 `-1`。驱动保留静态请求内存，后续读写入口都拒绝再提交。 |

驱动只读调用方的 `source`。提交后返回 `-1` 表示驱动未确认写入成功。设备可能已经改动目标扇区，需要时由调用方另行核验。现有 `virtio_block_read` 的长度、越界、失败时保留目标等契约照常适用。

## 3. 实现任务与自查

### 任务一 · 保存介质是否只读

在 `kernel/drivers/virtio_block.cpp` 的 `tinyos::drivers::virtio_block_init` 中，现有 `accepted_page0` 已经保留设备提供的 `VIRTIO_BLK_F_RO`。初始化成功时，在保存 `g_mmio_base` 和 `g_sector_count` 的同一处，把这个位转成驱动私有的 `g_read_only`。`virtio_block_write` 用它判断能否提交写请求。`virtio_block_read` 继续允许读只读介质。

协作者在暂停启动的 QEMU 上读取了低 32 位 `DeviceFeatures`。`readonly=on` 时第 5 位为 1，`readonly=off` 时为 0。这个预检只核对了特性位，实际写入由固定 P0 验证。

### 任务二 · 让两种请求共用已有队列

在 `kernel/drivers/virtio_block.cpp` 把 `tinyos::drivers::virtio_block_read` 中填请求头、填三项描述符、发布、等完成、核对完成项和消费计数的代码收进同文件的私有函数。可以命名为 `tinyos::drivers::submit_block_request`，参数说明扇区号以及请求是 `IN` 还是 `OUT`。`IN` 的协议值为 `0`，`OUT` 为 `1`。把现有 512 字节数据区放到两个公开入口和私有提交函数都能访问的文件作用域。请求头、状态字节和 `last_used_index` 可以继续静态保存在共享的提交函数里。

| 同一处代码要根据方向选择的字段 | 读 `IN` | 写 `OUT` |
| --- | --- | --- |
| 请求头 `req.type` | `0` | `1` |
| `g_descriptors[1].flags` | `WRITE \| NEXT`，QEMU 写数据区 | `NEXT`，QEMU 读数据区 |
| 成功时 `completion.length` | `513` | `1` |

两个方向的 `g_descriptors[0]` 都让 QEMU 读请求头，`g_descriptors[2]` 都让 QEMU 写状态字节。驱动每次重新设置数据描述符的 `flags` 和 `status_byte`。检查读写交替时，写请求清除了上一条读请求留下的设备写权限。保留 B35 的两道提交屏障、完成屏障和 `QueueNotify = 0`。读写双方使用同一份 `last_used_index`，一次只等待一条请求。

### 任务三 · 接入读写入口各自的复制

- `kernel/drivers/virtio_block.cpp` 的 `tinyos::drivers::virtio_block_read` 先按原有条件校验，再由私有提交函数完成 `IN`。只有成功时，这个读入口才把驱动数据区复制给 `destination`。用 B35 探针确认扇区 `0` 和 `1` 仍能完整读出。
- 同文件新增的 `tinyos::drivers::virtio_block_write` 先按第 2 节校验，再把 512 字节从 `source` 复制进驱动数据区。这个写入口调用私有提交函数发送 `OUT`，并把提交函数返回的结果交给调用方。检查只读拒绝、容量边界和长度错误都发生在复制与发布之前。

正式启动继续运行 B35 的读探针。协作者在隔离构建副本里的 `kernel/boot/private/boot_stages.cpp`，用全局 C 链接函数 `boot_probe_block_read` 临时调用写接口。这样同一份正式驱动可以用两种镜像验证，默认无盘启动和原有探针仍按现有方式工作。

## 4. 固定 P0 与验收方法

协作者用两份新生成的 64 KiB 临时镜像执行固定 P0。初始扇区内容沿用 B35 公式 `(17 + sector * 53 + offset) & 0xff`。写入测试使用扇区 7，新的第 `offset` 个字节为 `(0xa5 ^ offset) & 0xff`。这 512 字节里包含零字节和高位为 1 的字节，用完整字节比较检查结果。

| P0 | 通过条件 |
| --- | --- |
| 可写镜像与队列复用 | 临时探针先拒绝 `write(128, 512 字节)` 和 `write(7, 511 字节)`，两次均返回 `-1`。随后写扇区 7 返回 `0`，读回同一扇区返回 `0`，逐字节等于新模式。设备提交、归还、驱动消费各推进两次。 |
| 宿主镜像核对 | QEMU 正常退出后，宿主机逐字节对比整个镜像。扇区 7 等于新模式，其余 127 个扇区保留初始值。 |
| 只读镜像 | 同样的 512 字节写入返回 `-1`，再读扇区 7 取得初始值。队列只处理这一次读请求，退出后宿主镜像全量一致。 |
| B35 与默认回归 | 正式 `BLOCK_PROBE=1` 用原 B35 只读镜像读扇区 0、拒绝 128、读扇区 1，无盘时沿用原诊断与启动。`BLOCK_PROBE=0` 的 kdbg、user 两种构建通过。`make test` 和 `make test-process` 通过。 |

协作者在隔离副本接入写入探针后，用以下命令准备镜像、编译并启动 QEMU。`before.raw` 留作比对，QEMU 使用 `write.raw`。只读验证时把 `readonly=off` 改为 `readonly=on`，并挂载 `readonly.raw`。

```bash
fixture_dir=$(mktemp -d /tmp/tinyos-b36.XXXXXXXX)
python3 - "$fixture_dir" <<'PY'
import sys
from pathlib import Path

directory = Path(sys.argv[1])
original = bytes((17 + sector * 53 + offset) & 0xff
                 for sector in range(128) for offset in range(512))
for name in ('before.raw', 'write.raw', 'readonly.raw'):
    (directory / name).write_bytes(original)
PY
make -j4 BLOCK_PROBE=1 CONSOLE_MODE=kdbg
qemu-system-riscv64 -machine virt -m 128M -nographic -bios default \
    -kernel kernel.elf -global virtio-mmio.force-legacy=false \
    -drive if=none,format=raw,readonly=off,id=b36disk,file="$fixture_dir/write.raw" \
    -device virtio-blk-device,drive=b36disk,bus=virtio-mmio-bus.0
```

两次运行均正常退出后，宿主机用下面的比较确认改动范围。

```bash
python3 - "$fixture_dir" <<'PY'
import sys
from pathlib import Path

directory = Path(sys.argv[1])
original = (directory / 'before.raw').read_bytes()
expected = bytearray(original)
expected[7 * 512:8 * 512] = bytes((0xa5 ^ offset) & 0xff for offset in range(512))
assert (directory / 'write.raw').read_bytes() == expected
assert (directory / 'readonly.raw').read_bytes() == original
PY
```

- **P1（选做）**在隔离副本用临时探针观察写请求超时后读写入口都提前拒绝，学习设备可能迟到时驱动如何保留内存。
- **P2（选做）**读写交替跨过 8 个环槽，观察读写共用的消费计数如何回绕。

## 5. 验收后的三道理解题

1. 隔离探针在 S 态向可写镜像提交 `virtio_block_write(7, source)`。请沿着描述符 `0 → 1 → 2` 说清 QEMU 对每块内存执行的操作。为什么数据描述符的 `WRITE` 位和读盘时相反，成功写盘的 `completion.length` 应该是多少？
2. `virtio_block_write` 刚成功写完，探针紧接着调用 `virtio_block_read`。两个请求都从描述符 `0` 开始。请分别说明 QEMU 在哪个可用环槽看到第二个请求，驱动从哪个已用环槽消费第二个完成项。如果两个入口各保存一份从零开始的 `last_used_index`，第二次会读到什么？
3. 在同一个 S 态探针里，第一次写因介质只读返回 `-1`。另一次启动挂可写盘，设备已经收到写请求，但驱动轮询超时也返回 `-1`。请说明这两种情况下调用方能否改用自己的 `source`，驱动何时可以复用内部静态数据区，以及调用方对磁盘扇区内容各能确定什么。

## 6. 实现与固定 P0 结果（2026-11-21）

作者起草读写拆分后，授权协作者完成接线。`kernel/drivers/virtio_block.cpp` 的私有 `tinyos::drivers::submit_block_request` 负责填写描述符、发布请求和核对完成项。读写入口在调用之前校验各自的参数。描述符 `1` 始终指向同一个 `g_dma_buffer`。写入口先复制输入，读入口只在成功后复制输出。两个入口共享 `last_used_index`。驱动在初始化成功时保存 `RO` 位。

- 可写镜像拒绝越界写和 511 字节写，随后成功写入扇区 7 并逐字节读回。提交、设备归还、驱动消费计数均为 2。已用环两项的 `id` 均为 0，长度依次为 `1`、`513`。QEMU 经 QMP 正常退出后比较完整的 64 KiB 镜像，只有扇区 7 发生改变。
- 只读镜像拒绝同样的扇区 7 写入，读回原内容。三个计数均为 1，唯一完成项长度为 `513`。退出后整份镜像保持原样。两种镜像也都提前拒绝 511 字节的目标读取，目标哨兵保持。
- 正式 B35 探针仍通过扇区 0、拒绝 128、扇区 1，三个计数均为 2。无盘时报告一次初始化失败并继续启动。`BLOCK_PROBE=0` 的 `kdbg` 与 `user` 两种配置均构建无警告并启动到提示符。
- `make test` 通过 139 个宿主场景和 3 项 CTest。`make test-process` 通过 27 项。

块设备结果与隔离探针见 `.cache/batch36-write.JDt5KLDx/results.json` 和同目录的日志。QMP 读取队列内存核对计数和完成项，隔离修改只在启动探针，驱动与工作区源码一致。宿主日志见 `.cache/batch36-regressions.95yhmQeP/host.log`，进程回归见 `.cache/process-regression/20261121-185321-4/results.json`。

## 7. 三题原答与集中澄清（2026-11-21）

### 第 1 题 · 写盘请求的三项描述符

> 标识符0，qemu会读取req的内存，识别是应该写还是读，reversed是占位对齐用不读，sector说明从哪一个扇区开始，到了1的时候就会开始动作，向buffer写或者从buffer读内容，等到完成就在3处status_type写回是否完成；现在单进程读占用的时候不能写，成功写盘，completion.length是扇区大小

作者正确指出请求头提供操作方向和起始扇区，数据区由设备按方向读写。`reserved` 是协议保留字段，驱动填 `0`。第三项描述符的编号是 `2`，QEMU 向它指向的 `status_byte` 写结果，然后发布已用环完成项。当前驱动一次只允许一条请求在途，因为读写共用描述符和静态请求内存。

写盘时 QEMU 从描述符 `1` 指向的数据区读取 512 字节，所以驱动只为该项设置 `NEXT`。`completion.length` 统计设备写回客体内存的字节数，成功写盘时只有 1 字节状态。读盘时 QEMU 还向数据区写入 512 字节，完成长度为 `513`。

### 第 2 题 · 提交槽和完成槽

> 第二个请求槽    因为在设定g_avai.heads槽位之后会会++g_available.index;，驱动从observed_index处取出结果

第二次提交用 `g_available.heads[1]`，其中存放链头编号 `0`。第一次完成已被消费，旧 `last_used_index` 为 `1`。QEMU 写完 `g_used.elements[1]` 后把 `g_used.index` 提到 `2`。驱动观察到 `observed_index == 2`，用旧消费计数读槽 `1`，确认归还后才把 `last_used_index` 更新为 `2`。若读写各有一份从零开始的计数，读入口观察到设备计数 `1` 时可能误取槽 `0` 的旧写完成项。若此时设备计数已到 `2`，它会因计数不符合预期而禁用队列。

### 第 3 题 · 只读拒绝、超时与缓冲区归属

> 都不能，因为这两种都是无法确定到底会读到什么内容，与其去读还不如重新申请直到读取成功，驱动的静态缓冲只是存应该被操纵的内容，比如读，硬件会写进这块内存，写，驱动也会塞进这块内存交给硬件，而磁盘内容是持久化的数据，不会随意改动

作者正确说明了读盘时设备写驱动缓冲区，写盘时驱动先填入待写数据。两种失败返回后，调用方都可以立即修改或复用自己的 `source`，因为 QEMU 得到的是驱动静态缓冲区的地址。

- **只读拒绝**发生在复制和提交之前。这次请求没有交给设备，目标扇区不会被本次调用改动。驱动仍可用同一静态缓冲区处理后续读取。
- **提交后超时**表示驱动无法确认设备已归还请求。QEMU 可能仍在访问静态请求内存，也可能已经改动目标扇区。驱动保留该内存并设置 `g_ready = false`，后续读写都会提前拒绝。只有在安全恢复设备和队列、确认旧请求无法再访问这些内存后，驱动才能复用它们。当前实现没有自动恢复路径。

只读结论来自第 6 节的 QEMU 验收。超时结论来自当前代码的返回路径，属于本批选做验证范围。写入是否经得起掉电是另一项持久化保证。
