# VirtIO 1.2 中文翻译（节选）：设备初始化、MMIO 与块设备

> 原文：*Virtual I/O Device (VIRTIO) Version 1.2*，OASIS，2022-07-01。
>
> 本文件翻译第 **3.1–3.3 节（设备初始化/操作/清理）**、**4.2 节（Virtio Over MMIO）** 和 **5.2 节（Block Device）**，面向 TinyOS BATCH35。
>
> 翻译约定：MUST = 必须；MUST NOT = 禁止；SHOULD = 应该；SHOULD NOT = 不应；MAY = 可以。

---

## 3. 通用初始化与设备操作

本 section 先概述设备初始化，再展开每一步的细节。阅读时最好同时对照具体传输层（PCI/MMIO/Channel I/O）章节。

### 3.1 Device Initialization（设备初始化）

#### 3.1.1 Driver Requirements: Device Initialization（驱动要求）

驱动**必须**按以下顺序初始化设备：

1. 重置设备。
2. 设置 `ACKNOWLEDGE` 状态位：guest OS 已经注意到该设备。
3. 设置 `DRIVER` 状态位：guest OS 知道如何驱动该设备。
4. 读取设备特性位（feature bits），并把 OS/驱动理解的特性子集写回设备。在这一步中，驱动**可以**读取（但**不得**写入）设备特定配置字段，以检查自己是否能支持该设备，然后再决定是否接受它。
5. 设置 `FEATURES_OK` 状态位。此步骤之后，驱动**不得**再接受新的特性位。
6. 重新读取设备状态，确认 `FEATURES_OK` 位仍然置位；否则说明设备不支持该特性子集，设备不可用。
7. 执行设备特定设置：发现设备的 virtqueue、可选的按总线设置、读取并可能写入设备的 virtio 配置空间、填充 virtqueue。
8. 设置 `DRIVER_OK` 状态位。此时设备进入 “live” 状态。

如果上述任何步骤发生不可恢复的错误，驱动**应该**设置 `FAILED` 状态位，表示已经放弃该设备（之后可以重置设备重新开始）。这种情况下驱动**不得**继续初始化。

在设置 `DRIVER_OK` 之前，驱动**不得**向设备发送任何 buffer available notification。

#### 3.1.2 Legacy Interface: Device Initialization（传统接口说明）

传统设备不支持 `FEATURES_OK` 状态位，因此设备没有一种优雅方式来表示“不支持某组特性”。它们也没有清晰机制来结束特性协商，这意味着设备会在第一次使用时最终确定特性，且之后不能引入会根本改变设备初始操作方式的特性。

传统驱动实现经常在设置 `DRIVER_OK` 之前就使用设备，有时甚至在把特性位写给设备之前就使用设备。

结果是：步骤 5 和 6 被省略，步骤 4、7、8 被合并。因此使用传统接口时：

- 过渡期驱动**必须**执行 3.1 节描述的初始化序列，但省略步骤 5 和 6；
- 过渡期设备**必须**支持驱动在步骤 4 之前写设备配置字段；
- 过渡期设备**必须**支持驱动在步骤 8 之前使用设备。

### 3.2 Device Operation（设备操作）

操作设备时，设备配置空间中的每个字段都可能由驱动或设备修改。

只要这种配置变化由设备触发，驱动就会收到通知。这样驱动可以缓存设备配置，除非收到通知，否则不需要进行昂贵的配置读取。

#### 3.2.1 Notification of Device Configuration Changes（设备配置变化通知）

对于设备特定配置信息可能变化的设备，当设备特定配置发生变化时，会发送配置变化通知。

此外，设备设置 `DEVICE_NEEDS_RESET`（见 2.1.2 节）时也会触发该通知。

### 3.3 Device Cleanup（设备清理）

驱动设置 `DRIVER_OK` 状态位之后，设备所有已配置的 virtqueue 都被视为 live。设备被重置后，它的所有 virtqueue 都不再是 live。

#### 3.3.1 Driver Requirements: Device Cleanup（驱动要求）

对于 live virtqueue 中已经暴露给设备（但尚未被设备使用）的 buffer，驱动**不得**修改其 virtqueue 条目。

因此，驱动在移除已暴露 buffer 之前，**必须**确保 virtqueue 不是 live 的（通过重置设备）。

---

## 4.2 Virtio Over MMIO（基于 MMIO 的 Virtio）

没有 PCI 支持的虚拟环境（嵌入式设备模型中很常见）可以使用简单的**内存映射设备（virtio-mmio）**，而不是 PCI 设备。

内存映射 virtio 设备的行为基于 PCI 设备规范，因此大多数操作（设备初始化、队列配置、buffer 传输）几乎相同。下面描述现有差异。

### 4.2.1 MMIO Device Discovery（MMIO 设备发现）

与 PCI 不同，MMIO 没有通用的设备发现机制。对于每个设备，guest OS 需要知道寄存器位置和中断号。使用扁平设备树（flattened device tree）时，推荐的绑定示例如下：

```dts
// 示例：位于 0x1e000、占用 512 字节、中断 42 的 virtio_block 设备
virtio_block@1e000 {
    compatible = "virtio,mmio";
    reg = <0x1e000 0x200>;
    interrupts = <42>;
};
```

### 4.2.2 MMIO Device Register Layout（MMIO 设备寄存器布局）

MMIO virtio 设备提供一组内存映射控制寄存器，后面跟着设备特定配置空间，如表 4.1 所示。

所有寄存器值均按**小端序（Little Endian）**组织。

#### 表 4.1：MMIO 设备寄存器布局

| 名称 | 偏移 | 方向 | 功能说明 |
|---|---:|---|---|
| `MagicValue` | `0x000` | R | 魔数 `0x74726976`（“virt” 字符串的小端序表示） |
| `Version` | `0x004` | R | 设备版本号，现代设备为 `0x2`；传统设备为 `0x1` |
| `DeviceID` | `0x008` | R | VirtIO 子系统设备 ID；`0x0` 表示静态地址上的占位设备 |
| `VendorID` | `0x00c` | R | VirtIO 子系统厂商 ID |
| `DeviceFeatures` | `0x010` | R | 设备支持的特性位。读取返回 32 位，具体页由 `DeviceFeaturesSel` 选择：选择 0 返回 bit 0–31，选择 1 返回 bit 32–63 |
| `DeviceFeaturesSel` | `0x014` | W | 选择要读取的设备特性页 |
| `DriverFeatures` | `0x020` | W | 驱动理解并启用的特性位。写入 32 位，具体页由 `DriverFeaturesSel` 选择 |
| `DriverFeaturesSel` | `0x024` | W | 选择要写入的驱动特性页 |
| `QueueSel` | `0x030` | W | 选择虚拟队列索引；第一个队列为 0 |
| `QueueNumMax` | `0x034` | R | 所选队列支持的最大元素数量；0 表示队列不可用 |
| `QueueNum` | `0x038` | W | 驱动要使用的队列元素数量 |
| `QueueReady` | `0x044` | RW | 写 1 通知设备可以执行该队列的请求；读回最后写入的值 |
| `QueueNotify` | `0x050` | W | 通知设备某队列有新 buffer；未协商 `VIRTIO_F_NOTIFICATION_DATA` 时写入队列索引 |
| `InterruptStatus` | `0x060` | R | 中断状态位：bit 0 = used buffer notification；bit 1 = configuration change notification |
| `InterruptACK` | `0x064` | W | 写位掩码确认已处理的中断事件 |
| `Status` | `0x070` | RW | 设备状态；写 0 触发设备重置 |
| `QueueDescLow` | `0x080` | W | 所选队列 Descriptor Area 的 64 位物理地址低 32 位 |
| `QueueDescHigh` | `0x084` | W | 所选队列 Descriptor Area 的物理地址高 32 位 |
| `QueueDriverLow` | `0x090` | W | 所选队列 Driver Area（available ring）的物理地址低 32 位 |
| `QueueDriverHigh` | `0x094` | W | 所选队列 Driver Area 的物理地址高 32 位 |
| `QueueDeviceLow` | `0x0a0` | W | 所选队列 Device Area（used ring）的物理地址低 32 位 |
| `QueueDeviceHigh` | `0x0a4` | W | 所选队列 Device Area 的物理地址高 32 位 |
| `SHMSel` | `0x0ac` | W | 选择共享内存区域 |
| `SHMLenLow` | `0x0b0` | R | 共享内存区域长度低 32 位 |
| `SHMLenHigh` | `0x0b4` | R | 共享内存区域长度高 32 位 |
| `SHMBaseLow` | `0x0b8` | R | 共享内存区域物理基地址低 32 位 |
| `SHMBaseHigh` | `0x0bc` | R | 共享内存区域物理基地址高 32 位 |
| `QueueReset` | `0x0c0` | RW | 协商 `VIRTIO_F_RING_RESET` 后，写 1 可单独重置所选队列 |
| `ConfigGeneration` | `0x0fc` | R | 设备特定配置空间的版本/代数；用于检测配置读取是否原子 |
| `Config` | `0x100+` | RW | 设备特定配置空间，按字节对齐访问 |

> 代码里如果写成 `QueueDriverlLow`（多了一个 `l`），那是拼写问题；规范中的名字是 `QueueDriverLow`。

#### 4.2.2.1 Device Requirements: MMIO Device Register Layout（设备要求）

- 设备**必须**在 `MagicValue` 返回 `0x74726976`。
- 设备**必须**在 `Version` 返回 `0x2`。
- 设备**必须**在事件发生期间，在 `InterruptStatus` 中设置对应位，直到驱动通过写 `InterruptACK` 对应位来确认中断。
- 未发生的事件对应位**必须**为 0。
- 重置时，设备**必须**清除 `InterruptStatus` 的所有位，并清除所有队列 `QueueReady` 中的 ready 位。
- 如果存在驱动看到不一致配置状态的风险，设备**必须**改变 `ConfigGeneration` 的返回值。
- 当 `QueueReady` 为 0 时，设备**不得**访问虚拟队列内容。
- 如果协商了 `VIRTIO_F_RING_RESET`，设备在重置时 `QueueReset` 必须呈现 0；队列通过 `QueueReady` 启用后，`QueueReset` 也必须呈现 0。
- 当向 `QueueReset` 写 1 时，设备**必须**重置队列；重置进行期间，`QueueReset` 必须持续呈现 1；重置完成后，`QueueReset` 和 `QueueReady` 都必须呈现 0（见 2.6.1 节）。

#### 4.2.2.2 Driver Requirements: MMIO Device Register Layout（驱动要求）

- 驱动**不得**访问表 4.1 未描述的内存位置（配置空间以设备规范为准）；**不得**写只读寄存器（方向 R），**不得**读只写寄存器（方向 W）。
- 驱动访问表 4.1 中的控制寄存器时，**必须**只使用 32 位宽且对齐的读写。
- 对于设备特定配置空间：8 位字段用 8 位访问；16 位字段用 16 位且对齐的访问；32/64 位字段用 32 位且对齐的访问。
- 如果 `MagicValue` 不是 `0x74726976`，驱动**必须**忽略该设备（可以报告错误）。
- 如果 `Version` 不是 `0x2`，驱动**必须**忽略该设备（可以报告错误）。
- 如果 `DeviceID` 为 0，驱动**必须**忽略该设备，但**不得**报告错误。
- 读 `DeviceFeatures` 之前，驱动**必须**先写 `DeviceFeaturesSel`。
- 写 `DriverFeatures` 之前，驱动**必须**先写 `DriverFeaturesSel`。
- 驱动写入的 `QueueNum` **必须**小于或等于设备在 `QueueNumMax` 中给出的值。
- 当 `QueueReady` 非 0 时，驱动**不得**访问 `QueueNum`、`QueueDescLow/High`、`QueueDriverLow/High`、`QueueDeviceLow/High`。
- 停止使用队列时，驱动**必须**向 `QueueReady` 写 0，并读回该值以确保同步。
- 驱动**必须**忽略 `InterruptStatus` 中未定义的位。
- 处理完中断后，驱动**必须**向 `InterruptACK` 写入已处理事件的位掩码，且**不得**设置未定义位。
- 如果协商了 `VIRTIO_F_RING_RESET`，驱动向 `QueueReset` 写 1 后，**不得**认为队列重置完成，直到读回 `QueueReset` 为 0。之后，在确保其他 virtqueue 字段配置正确后，驱动**可以**通过向 `QueueReady` 写 1 重新启用队列，并**可以**使用与重置前不同的驱动可写队列配置值。

### 4.2.3 MMIO-specific Initialization And Device Operation（MMIO 特定初始化与设备操作）

#### 4.2.3.1 Device Initialization（设备初始化）

##### 4.2.3.1.1 Driver Requirements: Device Initialization（驱动要求）

- 驱动**必须**通过读取并检查 `MagicValue` 和 `Version` 开始设备初始化。
- 如果两者都有效，驱动**必须**读取 `DeviceID`；如果其值为 0，**必须**中止初始化，且**不得**访问任何其他寄存器。
- 不期望共享内存的驱动**不得**使用共享内存寄存器。
- 后续初始化**必须**遵循 3.1 节描述的过程。

#### 4.2.3.2 Virtqueue Configuration（虚拟队列配置）

驱动通常按以下方式初始化虚拟队列：

1. 向 `QueueSel` 写入队列索引（第一个队列为 0）。
2. 读取 `QueueReady`，确认返回值为 0，即队列尚未被使用。
3. 从 `QueueNumMax` 读取最大队列元素数；如果返回 0，说明队列不可用。
4. 分配并清零队列内存，确保物理连续。
5. 把队列大小写入 `QueueNum`，通知设备。
6. 把队列 Descriptor Area、Driver Area、Device Area 的物理地址分别写入：
   - `QueueDescLow/QueueDescHigh`
   - `QueueDriverLow/QueueDriverHigh`
   - `QueueDeviceLow/QueueDeviceHigh`
7. 向 `QueueReady` 写 `0x1`。

#### 4.2.3.3 Available Buffer Notifications（可用缓冲区通知）

未协商 `VIRTIO_F_NOTIFICATION_DATA` 时，驱动向 `QueueNotify` 写入要通知的 16 位 virtqueue 索引，即发送 available buffer notification。

协商了 `VIRTIO_F_NOTIFICATION_DATA` 时，驱动向 `QueueNotify` 写入以下 32 位值：

```c
le32 {
    vqn : 16;        // virtqueue index
    next_off : 15;
    next_wrap : 1;
};
```

各字段定义见 2.9 节 Driver Notifications。

#### 4.2.3.4 Notifications From The Device（来自设备的通知）

内存映射 virtio 设备使用单一专用中断信号；当 `InterruptStatus` 中至少一位被置位时，该信号被拉高。设备通过这种方式向驱动发送 used buffer notification 或配置变化通知。

##### 4.2.3.4.1 Driver Requirements: Notifications From The Device（驱动要求）

- 收到中断后，驱动**必须**读取 `InterruptStatus`，判断中断原因。
- `used buffer notification` 位被置位时，**应该**把它解释为每个 active virtqueue 的 used buffer notification。
- 处理完中断后，驱动**必须**向 `InterruptACK` 写入对应已处理事件的位掩码来确认中断。

---

## 5.2 Block Device（块设备）

virtio 块设备是一个简单的虚拟块设备（即磁盘）。读、写以及其他特殊请求被放入它的某个队列，由设备处理（通常可以乱序，除非另有说明）。

### 5.2.1 Device ID（设备 ID）

块设备的 Device ID 为 **2**。

### 5.2.2 Virtqueues（虚拟队列）

- 队列 0：request queue（请求队列）；
- ……
- 队列 N-1：request queue N。

如果没有协商 `VIRTIO_BLK_F_MQ`，则 `N = 1`；否则 `N` 由配置字段 `num_queues` 指定。

### 5.2.3 Feature bits（特性位）

| 特性 | 位 | 含义 |
|---|---:|---|
| `VIRTIO_BLK_F_SIZE_MAX` | 1 | 任意单个 segment 的最大大小在 `size_max` 中 |
| `VIRTIO_BLK_F_SEG_MAX` | 2 | 一个请求中最多 segment 数在 `seg_max` 中 |
| `VIRTIO_BLK_F_GEOMETRY` | 4 | 磁盘几何信息在 `geometry` 中 |
| `VIRTIO_BLK_F_RO` | 5 | 设备只读 |
| `VIRTIO_BLK_F_BLK_SIZE` | 6 | 磁盘块大小在 `blk_size` 中 |
| `VIRTIO_BLK_F_FLUSH` | 9 | 支持 cache flush 命令 |
| `VIRTIO_BLK_F_TOPOLOGY` | 10 | 设备导出最佳 I/O 对齐信息 |
| `VIRTIO_BLK_F_CONFIG_WCE` | 11 | 设备可在 writeback/writethrough 缓存模式间切换 |
| `VIRTIO_BLK_F_MQ` | 12 | 支持多队列 |
| `VIRTIO_BLK_F_DISCARD` | 13 | 支持 discard；最大值在 `max_discard_sectors` / `max_discard_seg` |
| `VIRTIO_BLK_F_WRITE_ZEROES` | 14 | 支持 write zeroes；最大值在 `max_write_zeroes_sectors` / `max_write_zeroes_seg` |
| `VIRTIO_BLK_F_LIFETIME` | 15 | 支持提供存储寿命信息 |
| `VIRTIO_BLK_F_SECURE_ERASE` | 16 | 支持 secure erase；最大值在 `max_secure_erase_sectors` / `max_secure_erase_seg` |

传统接口还有：

- `VIRTIO_BLK_F_BARRIER`（0）：支持请求屏障；
- `VIRTIO_BLK_F_SCSI`（7）：支持 SCSI packet 命令。

> 注意：传统接口中，`VIRTIO_BLK_F_FLUSH` 也被称为 `VIRTIO_BLK_F_WCE`。

### 5.2.4 Device configuration layout（设备配置布局）

设备容量（以 512 字节扇区为单位）**总是存在**。其他字段是否存在取决于上面的特性位。

`num_queues` 字段仅在设置了 `VIRTIO_BLK_F_MQ` 时存在，表示队列数量。

配置结构：

```c
struct virtio_blk_config {
    le64 capacity;

    le32 size_max;
    le32 seg_max;

    struct virtio_blk_geometry {
        le16 cylinders;
        u8 heads;
        u8 sectors;
    } geometry;

    le32 blk_size;

    struct virtio_blk_topology {
        // 每个物理块中的逻辑块数（log2）
        u8 physical_block_exp;
        // 第一个对齐逻辑块的偏移
        u8 alignment_offset;
        // 建议的最小 I/O 大小（以块为单位）
        le16 min_io_size;
        // 最佳（建议最大）I/O 大小（以块为单位）
        le32 opt_io_size;
    } topology;

    u8 writeback;
    u8 unused0;
    u16 num_queues;

    le32 max_discard_sectors;
    le32 max_discard_seg;
    le32 discard_sector_alignment;
    le32 max_write_zeroes_sectors;
    le32 max_write_zeroes_seg;
    u8 write_zeroes_may_unmap;
    u8 unused1[3];
    le32 max_secure_erase_sectors;
    le32 max_secure_erase_seg;
    le32 secure_erase_sector_alignment;
};
```

如果协商了 `VIRTIO_BLK_F_DISCARD`，`max_discard_sectors`、`discard_sector_alignment` 以 512 字节为单位。`max_write_zeroes_sectors` 在协商 `VIRTIO_BLK_F_WRITE_ZEROES` 后以 512 字节为单位；secure erase 相关字段同理。

### 5.2.5 Device Initialization（设备初始化）

1. 从 `capacity` 读取设备大小。
2. 如果协商了 `VIRTIO_BLK_F_BLK_SIZE`，读取 `blk_size`，得到驱动应使用的最佳扇区大小。协议单位始终是 512 字节，这个值只影响性能。
3. 如果设备设置了 `VIRTIO_BLK_F_RO`，任何写请求都会失败。
4. 如果协商了 `VIRTIO_BLK_F_TOPOLOGY`，读取 `topology`，确定物理块大小和最佳 I/O 长度；同样只影响性能。
5. 如果协商了 `VIRTIO_BLK_F_CONFIG_WCE`，可以通过 `writeback` 读取或设置缓存模式：0 = writethrough，1 = writeback。重置后的缓存模式可能是 writeback 或 writethrough；协商后读取 `writeback` 可确定实际模式。
6. 如果协商了 `VIRTIO_BLK_F_DISCARD`，读取 `max_discard_sectors` 和 `max_discard_seg`，确定最大 discard 扇区数和最大 discard segment 数；`discard_sector_alignment` 可用于按对齐拆分请求。
7. 如果协商了 `VIRTIO_BLK_F_WRITE_ZEROES`，读取 `max_write_zeroes_sectors` 和 `max_write_zeroes_seg`。
8. 如果协商了 `VIRTIO_BLK_F_MQ`，读取 `num_queues`。
9. 如果协商了 `VIRTIO_BLK_F_SECURE_ERASE`，读取 secure erase 相关最大值和对齐字段。

#### 5.2.5.1 Driver Requirements: Device Initialization（驱动要求）

- 如果驱动没有能力发送 `VIRTIO_BLK_T_FLUSH` 命令，**不应**协商 `VIRTIO_BLK_F_FLUSH`。
- 如果既没有协商 `VIRTIO_BLK_F_CONFIG_WCE` 也没有协商 `VIRTIO_BLK_F_FLUSH`，驱动**可以**推断存在 writethrough 缓存。
- 如果没有协商 `VIRTIO_BLK_F_CONFIG_WCE`，但协商了 `VIRTIO_BLK_F_FLUSH`，驱动**应该**假定存在 writeback 缓存。
- 驱动**不得**在设置 `FEATURES_OK` 设备状态位之前读取 `writeback`。

#### 5.2.5.2 Device Requirements: Device Initialization（设备要求）

- 设备**应该**总是提供 `VIRTIO_BLK_F_FLUSH`；如果提供 `VIRTIO_BLK_F_CONFIG_WCE`，则**必须**提供 `VIRTIO_BLK_F_FLUSH`。
- 如果协商了 `VIRTIO_BLK_F_CONFIG_WCE` 但没有协商 `VIRTIO_BLK_F_FLUSH`，设备**必须**把 `writeback` 初始化为 0。
- 设备**必须**把填充字节 `unused0` 和 `unused1` 初始化为 0。

### 5.2.6 Device Operation（设备操作）

驱动把请求放入 virtqueue，设备使用这些请求（不一定按顺序）。

每个请求格式如下：

```c
struct virtio_blk_req {
    le32 type;
    le32 reserved;
    le64 sector;
    u8 data[];
    u8 status;
};
```

请求类型：

```c
#define VIRTIO_BLK_T_IN            0
#define VIRTIO_BLK_T_OUT           1
#define VIRTIO_BLK_T_FLUSH         4
#define VIRTIO_BLK_T_GET_ID        8
#define VIRTIO_BLK_T_GET_LIFETIME  10
#define VIRTIO_BLK_T_DISCARD       11
#define VIRTIO_BLK_T_WRITE_ZEROES  13
#define VIRTIO_BLK_T_SECURE_ERASE  14
```

`sector` 表示读/写起始偏移（乘以 512 字节）。对于非读/写命令，该字段不使用，设为 0。

- `VIRTIO_BLK_T_IN`：设备把从块设备读取的扇区内容写入 `data`，长度必须是 512 的倍数。
- `VIRTIO_BLK_T_OUT`：设备把 `data` 的内容写入块设备，长度必须是 512 的倍数。

最终状态字节由设备写入：

```c
#define VIRTIO_BLK_S_OK     0
#define VIRTIO_BLK_S_IOERR  1
#define VIRTIO_BLK_S_UNSUPP 2
```

- `VIRTIO_BLK_S_OK`：成功；
- `VIRTIO_BLK_S_IOERR`：设备或驱动错误；
- `VIRTIO_BLK_S_UNSUPP`：设备不支持该请求。

#### 5.2.6.1 Driver Requirements: Device Operation（驱动要求）

- 驱动**不得**提交会导致读写越过 `capacity` 的请求。
- 如果设备提供 `VIRTIO_BLK_F_RO`，驱动**应该**接受它。
- 对于 `VIRTIO_BLK_T_FLUSH`，驱动**必须**把 `sector` 设为 0，并且**不应**在请求中包含任何数据。
- 对于 `VIRTIO_BLK_T_IN` / `VIRTIO_BLK_T_OUT`，`data` 长度**必须**是 512 的倍数。
- 对于 discard/secure erase/write zeroes，`data` 长度**必须**是 `struct virtio_blk_discard_write_zeroes` 大小的倍数。
- 对于 `VIRTIO_BLK_T_GET_ID`，`data` 长度**必须**是 20 字节。
- discard/secure erase/write zeroes 请求中的 segment 数量不得超过对应的 `max_*_seg`。
- 如果协商了 `VIRTIO_BLK_F_CONFIG_WCE`，驱动可以通过向 `writeback` 写 0/1 切换 writethrough/writeback。写 0 后，驱动**不得**假设任何 volatile 写已经提交到持久后端存储。
- discard 命令的 `unmap` 位**必须**为 0。
- 驱动**不得**对 discard 后读取返回的数据做任何假设。
- 如果多 segment 的 discard/write zeroes 请求返回 `VIRTIO_BLK_S_IOERR`，驱动**不得**假设各个 segment 是成功、失败还是根本未被处理。

#### 5.2.6.2 Device Requirements: Device Operation（设备要求）

- 如果提供了 `VIRTIO_BLK_F_RO`，设备对写请求**必须**把状态设为 `VIRTIO_BLK_S_IOERR`，并且**不得**写入任何数据。
- 如果 discard/secure erase/write zeroes 命令设置了任何未知标志，设备**必须**把状态设为 `VIRTIO_BLK_S_UNSUPP`。
- 对于 discard 命令，如果 `unmap` 标志被设置，设备**必须**返回 `VIRTIO_BLK_S_UNSUPP`。
- 对于 discard 命令，设备**可以**释放后端存储中指定范围的扇区。
- 对于 write zeroes 命令，如果设置了 `unmap`，设备**可以**像收到 discard 一样释放指定范围；命令完成后，对指定范围的读取**必须**返回 0，无论 `unmap` 是否设置。
- 如果 write zeroes 请求不可能导致一个或多个扇区被释放，设备**应该**清除 `write_zeroes_may_unmap` 字段。
- 设备**必须**在更新 used `idx` 之前写入状态字节。
- 设备**必须**在更新 used `idx` 之前，从第一个设备可写 buffer 开始，向描述符写入至少 `len` 字节。
- 设备**可以**写入超过 `len` 字节。

> 关于缓存/持久化的完整语义（writeback/writethrough、`VIRTIO_BLK_F_FLUSH`、crash 一致性等）见原文 5.2.6.2 节。

#### 5.2.6.4 Legacy Interface: Framing Requirements（传统接口分帧要求）

使用传统接口且未协商 `VIRTIO_F_ANY_LAYOUT` 的过渡期驱动：

- **必须**用一个 8 字节描述符包含 `type`、`reserved`、`sector`，然后用描述符放 `data`，最后用一个独立的 1 字节描述符放 `status`；
- SCSI 命令还有额外限制：`sense` 必须放在一个独立的 96 字节设备可写描述符中，`errors`、`data_len`、`sense_len`、`residual` 必须放在一个独立的设备可写描述符中。

见 2.7.4 节。

---

## 与 TinyOS BATCH35 的对应关系

### 1. MMIO 初始化

`virtio_block_init()` 对应 4.2.3.1 / 4.2.3.2：

- 先读 `MagicValue`、`Version`、`DeviceID`；
- 重置设备：`Status = 0`，轮询读回 0；
- 设置 `ACKNOWLEDGE`、`DRIVER`；
- 读 `DeviceFeatures` 页 1，要求 `VIRTIO_F_VERSION_1`（bit 32，即页 1 的 bit 0）；
- 可选接受 `VIRTIO_BLK_F_RO`（页 0 的 bit 5）；
- 设置 `FEATURES_OK` 并读回确认；
- `QueueSel = 0`，检查 `QueueNumMax >= 8`；
- `QueueNum = 8`；
- 把 `g_descriptors`、`g_available`、`g_used` 的 guest 物理地址写入 `QueueDesc*`、`QueueDriver*`、`QueueDevice*`；
- `QueueReady = 1`；
- 读 `ConfigGeneration` 前后一致的 `capacity`（`0x100`/`0x104`）；
- `Status |= DRIVER_OK`。

### 2. 一次块读

`virtio_block_read()` 对应 5.2.6 和 2.7.13：

- 请求头 `virtio_blk_req`：`type = VIRTIO_BLK_T_IN(0)`、`reserved = 0`、`sector`；
- 512 字节数据 buffer；
- 1 字节状态（初值 `0xFF`）；
- 三个描述符：
  1. 请求头：设备可读，`flags = NEXT`，`next = 1`；
  2. 数据：设备可写，`flags = NEXT | WRITE`，`next = 2`；
  3. 状态：设备可写，`flags = WRITE`；
- 把链头 0 写入 `g_available.ring[g_available.idx % kQueueSize]`；
- 递增 `g_available.idx`；
- 内存屏障后向 `QueueNotify` 写 0；
- 轮询 `g_used.idx`；
- 读取 `g_used.ring[...]` 的 `id`、`len`；
- 检查 `status == VIRTIO_BLK_S_OK(0)`；
- 把驱动私有数据 buffer 的 512 字节复制到调用方 `destination`。

## 待续章节

- 2.8 Packed Virtqueues（BATCH35 不需要）
- 5.1 Network Device（网络设备）
- 其他设备类型与传输层
