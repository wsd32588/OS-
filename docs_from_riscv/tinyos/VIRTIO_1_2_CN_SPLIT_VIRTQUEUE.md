# VirtIO 1.2 中文翻译（节选）：Virtqueue 与 Split Virtqueue

> 原文：*Virtual I/O Device (VIRTIO) Version 1.2*，OASIS，2022-07-01。
>
> 本文件翻译第 **2.6 节 Virtqueues** 和 **2.7 节 Split Virtqueues**，面向 TinyOS BATCH35 的 VirtIO 块设备实现。全文 282 页，其余章节将按主题分批翻译。

## 翻译约定

| 英文 | 中文 |
|---|---|
| MUST | 必须 |
| MUST NOT | 禁止 / 不得 |
| SHOULD | 应该 |
| SHOULD NOT | 不应 |
| MAY | 可以 |
| virtqueue | 虚拟队列 |
| descriptor | 描述符 |
| descriptor table | 描述符表 |
| available ring | 可用环 |
| used ring | 已用环 |
| driver | 驱动 |
| device | 设备 |
| device-readable | 设备可读 |
| device-writable | 设备可写 |
| guest physical address | 客户机物理地址（guest PA） |
| used length | 已用长度 |
| available buffer notification | 可用缓冲区通知 |
| used buffer notification | 已用缓冲区通知 |

---

## 2.6 Virtqueues（虚拟队列）

VirtIO 设备上进行批量数据传输的机制被称为 **virtqueue（虚拟队列）**。每个设备可以有 0 个或多个 virtqueue。

驱动通过向队列添加 **available buffer（可用缓冲区）** 来把请求提供给设备，也就是把一个描述请求的 buffer 加入 virtqueue；驱动还可以选择触发一次 **driver event（驱动事件）**，即向设备发送 **available buffer notification（可用缓冲区通知）**。

设备执行请求，完成后把 **used buffer（已用缓冲区）** 加入队列，也就是通过把 buffer 标记为已用来通知驱动。设备随后可以触发一次 **device event（设备事件）**，即向驱动发送 **used buffer notification（已用缓冲区通知）**。

设备会报告它每个 buffer 实际写入内存的字节数，这个值称为 **used length（已用长度）**。

设备通常**不需要**按照驱动提供 buffer 的顺序来使用 buffer。

有些设备总是按照描述符被提供的顺序使用描述符。这些设备可以提供 `VIRTIO_F_IN_ORDER` 特性。如果协商了该特性，这一知识可以用于优化，或者简化驱动/设备代码。

每个 virtqueue 最多由三部分组成：

- **Descriptor Area（描述符区）**：用于描述 buffer；
- **Driver Area（驱动区）**：驱动提供给设备的额外数据；
- **Device Area（设备区）**：设备提供给驱动的额外数据。

> 注意：本规范早期版本对这些部分使用了不同名称（与 2.7 节对应）：
>
> - Descriptor Table：对应 Descriptor Area；
> - Available Ring：对应 Driver Area；
> - Used Ring：对应 Device Area。

支持两种格式：**Split Virtqueues（分离式虚拟队列，见 2.7 节）** 和 **Packed Virtqueues（打包式虚拟队列，见 2.8 节）**。

每个驱动和设备都支持 Packed 或 Split 格式中的一种，或者两种都支持。

### 2.6.1 Virtqueue Reset（虚拟队列重置）

如果协商了 `VIRTIO_F_RING_RESET`，驱动可以单独重置某个 virtqueue。重置方式与具体传输层（transport）有关。

虚拟队列重置分为两部分：驱动先重置队列，之后可以选择重新启用它。

#### 2.6.1.1 Virtqueue Reset（虚拟队列重置）

##### 2.6.1.1.1 Device Requirements: Virtqueue Reset（设备要求）

驱动重置队列之后，设备**不得**执行该 virtqueue 中的任何请求，也不得为该队列通知驱动。

设备**必须**把 virtqueue 的所有状态恢复到默认状态，包括 available 状态和 used 状态。

##### 2.6.1.1.2 Driver Requirements: Virtqueue Reset（驱动要求）

驱动告诉设备重置某个队列之后，**必须**验证该队列确实已经被重置。

队列成功重置之后，驱动**可以**释放与该 virtqueue 关联的任何资源。

#### 2.6.1.2 Virtqueue Re-enable（虚拟队列重新启用）

重新启用队列的过程，与整个设备初始化期间初始化单个队列的过程相同。

##### 2.6.1.2.1 Device Requirements: Virtqueue Re-enable（设备要求）

设备**必须**观察到驱动可能修改过的任何队列配置，例如最大队列长度。

##### 2.6.1.2.2 Driver Requirements: Virtqueue Re-enable（驱动要求）

重新启用队列时，驱动**必须**像最初发现 virtqueue 时那样配置队列资源，但**可以**使用不同的参数。

---

## 2.7 Split Virtqueues（分离式虚拟队列）

Split virtqueue 格式是本标准 1.0 版（以及更早版本）唯一支持的格式。

Split virtqueue 把虚拟队列分成若干部分。每一部分只能由驱动或设备之一写入，不能由两者同时写。当驱动让一个 buffer 可用、或设备把 buffer 标记为已用时，可能需要更新多个部分和/或某一部分内的多个位置。

每个队列都有一个 16 位的 `QueueSize` 参数，它决定队列中的条目数量，也隐含了队列的总大小。

每个 virtqueue 由三部分组成：

- **Descriptor Table（描述符表）**：占据 Descriptor Area；
- **Available Ring（可用环）**：占据 Driver Area；
- **Used Ring（已用环）**：占据 Device Area。

每一部分在 guest 内存中都是**物理连续**的，并且有不同的对齐要求。

各部分的内存对齐和大小要求（单位：字节）如下：

| Virtqueue 部分 | 对齐 | 大小 |
|---|---:|---:|
| Descriptor Table | 16 | `16 * QueueSize` |
| Available Ring | 2 | `6 + 2 * QueueSize` |
| Used Ring | 4 | `6 + 8 * QueueSize` |

“对齐”列给出每一部分的最小对齐要求。“大小”列给出每一部分的总字节数。

`QueueSize` 对应 virtqueue 中 buffer 的最大数量。`QueueSize` 的值总是 2 的幂，最大值为 **32768**。具体值由总线相关的方式指定。

驱动想向设备发送一个 buffer 时，会填充描述符表中的一个槽位（或者把多个描述符串成链），然后把描述符索引写入 available ring，最后通知设备。

设备完成一个 buffer 后，会把描述符索引写入 used ring，并发送 used buffer notification。

### 2.7.1 Driver Requirements: Virtqueues（驱动要求）

驱动**必须**保证：每个 virtqueue 部分的第一个字节的物理地址，是上表所规定对齐值的整数倍。

### 2.7.2 Legacy Interfaces: A Note on Virtqueue Layout（传统接口说明）

对于传统接口（Legacy Interface），virtqueue 布局还有额外限制：

每个 virtqueue 占据两个或更多物理连续页（通常定义为 4096 字节，但也取决于传输层；下文称为 `QueueAlign`），并由三部分组成：

```text
Descriptor Table    Available Ring (...padding...)    Used Ring
```

总线相关的 `QueueSize` 字段控制 virtqueue 的总字节数。使用传统接口时，过渡期驱动**必须**从设备读取 `QueueSize` 字段，并**必须**按照下面的公式分配 virtqueue 的总字节数（`qalign` 为 QueueAlign，`qsz` 为 QueueSize）：

```c
#define ALIGN(x) (((x) + qalign) & ~qalign)
static inline unsigned virtq_size(unsigned int qsz)
{
    return ALIGN(sizeof(struct virtq_desc) * qsz + sizeof(u16) * (3 + qsz))
         + ALIGN(sizeof(u16) * 3 + sizeof(struct virtq_used_elem) * qsz);
}
```

这种布局会因填充（padding）浪费一些空间。使用传统接口时，过渡期设备和驱动**必须**使用下面的 `virtq` 布局来定位 virtqueue 的各个元素：

```c
struct virtq {
    /* 实际的描述符，每个 16 字节 */
    struct virtq_desc desc[QueueSize];

    /* 可用描述符链头组成的环，带有自由递增索引 */
    struct virtq_avail avail;

    /* 填充到下一个 QueueAlign 边界 */
    u8 pad[Padding];

    /* 已用描述符链头组成的环，带有自由递增索引 */
    struct virtq_used used;
};
```

### 2.7.3 Legacy Interfaces: A Note on Virtqueue Endianness（传统接口字节序）

使用传统接口时，过渡期设备和驱动**必须**使用 guest 的本机字节序作为字段和 virtqueue 的字节序。这与本规范为非传统接口规定的小端序（little-endian）不同。这里假设宿主已经知道 guest 的字节序。

### 2.7.4 Message Framing（消息分帧）

用描述符表示消息的“分帧方式”与 buffer 的内容无关。例如，一个网络发送 buffer 可以由 12 字节头部加网络包组成。最简单的方式是在描述符表中放一个 12 字节的 output descriptor，再放一个 1514 字节的 output descriptor；但如果头部和数据包在内存中相邻，也可以用一个 1526 字节的 output descriptor；甚至可以使用三个或更多描述符（不过这种情况下效率可能降低）。

注意，有些设备实现会对描述符总长度设置较大但合理的限制（例如基于宿主 OS 的 `IOV_MAX`）。实践中这通常不是问题：如果驱动把网络包拆成 1500 个单字节描述符，这种不合理行为不会得到同情。

#### 2.7.4.1 Device Requirements: Message Framing（设备要求）

设备**不得**对描述符的具体排列方式做假设。设备**可以**对它允许的描述符链长度设置合理限制。

#### 2.7.4.2 Driver Requirements: Message Framing（驱动要求）

驱动**必须**把所有设备可写的描述符元素放在所有设备可读的描述符元素之后。

驱动**不应**使用过多描述符来描述一个 buffer。

#### 2.7.4.3 Legacy Interface: Message Framing（传统接口说明）

历史上的初始驱动实现使用了简单布局，设备也开始依赖这种布局，尽管本规范有不同措辞。此外，virtio_blk 的 SCSI 命令规范曾要求从消息边界推断字段长度（见 5.2.6.3 节传统接口设备操作）。

因此，使用传统接口时，`VIRTIO_F_ANY_LAYOUT` 特性向设备和驱动表示：不对分帧方式做任何假设。未协商该特性时，过渡期驱动的要求在各设备章节中给出。

### 2.7.5 The Virtqueue Descriptor Table（虚拟队列描述符表）

描述符表描述驱动正在给设备使用的 buffer。`addr` 是一个物理地址，buffer 可以通过 `next` 串成链。每个描述符描述一个 buffer：该 buffer 对设备而言要么是只读的（**device-readable，设备可读**），要么是只写的（**device-writable，设备可写**）；但一条描述符链可以同时包含设备可读和设备可写 buffer。

提供给设备的内存的实际内容取决于设备类型。最常见的是：数据以一个供设备读取的头部开始（包含小端序字段），并以一个供设备写入的状态尾部结束。

```c
struct virtq_desc {
    /* 地址（guest 物理地址） */
    le64 addr;
    /* 长度 */
    le32 len;

/* 表示该 buffer 通过 next 字段继续链下去 */
#define VIRTQ_DESC_F_NEXT     1
/* 表示该 buffer 是设备只写的（否则是设备只读） */
#define VIRTQ_DESC_F_WRITE    2
/* 表示该 buffer 包含一组 buffer 描述符（间接描述符） */
#define VIRTQ_DESC_F_INDIRECT 4

    /* 上面定义的 flags */
    le16 flags;
    /* 当 flags & NEXT 时使用的下一个描述符索引 */
    le16 next;
};
```

描述符表中的描述符数量由该 virtqueue 的 queue size 定义；这也是描述符链可能的最大长度。

如果协商了 `VIRTIO_F_IN_ORDER`，驱动按环顺序使用描述符：从表中偏移 0 开始，到表尾后回绕。

> 注意：传统接口的 [Virtio PCI Draft] 把该结构称为 `vring_desc`，把常量称为 `VRING_DESC_F_NEXT` 等，但布局和值完全相同。

#### 2.7.5.1 Device Requirements: The Virtqueue Descriptor Table（设备要求）

设备**不得**写设备可读 buffer；设备**不应**读设备可写 buffer（出于调试或诊断目的时可以读）。设备**不得**写描述符表中的任何条目。

#### 2.7.5.2 Driver Requirements: The Virtqueue Descriptor Table（驱动要求）

驱动**不得**添加总长度超过 **2^32 字节** 的描述符链；这意味着描述符链中**禁止出现循环**。

如果协商了 `VIRTIO_F_IN_ORDER`，并且驱动把表中偏移 `x` 的描述符提供给设备时设置了 `flags` 中的 `VRING_DESC_F_NEXT`，那么：

- 对于表中的最后一个描述符（`x = queue_size - 1`），驱动**必须**把 `next` 设为 0；
- 对于其余描述符，驱动**必须**把 `next` 设为 `x + 1`。

#### 2.7.5.3 Indirect Descriptors（间接描述符）

有些设备需要同时派发大量大请求，`VIRTIO_F_INDIRECT_DESC` 特性允许这样做。为了增加环容量，驱动可以在内存任意位置存放一张间接描述符表，并在主 virtqueue 中插入一个设置 `VIRTQ_DESC_F_INDIRECT` 标志的描述符，指向包含该间接描述符表的内存 buffer；此时 `addr` 和 `len` 分别表示间接表的地址和字节长度。

间接表布局如下（`len` 是引用该表的描述符长度，因此下面的代码无法直接编译）：

```c
struct indirect_descriptor_table {
    /* 实际描述符，每个 16 字节 */
    struct virtq_desc desc[len / 16];
};
```

第一个间接描述符位于间接表起始处（索引 0），后续间接描述符通过 `next` 串联。没有有效 `next` 的间接描述符（即 `flags` 中未设置 `VIRTQ_DESC_F_NEXT`）表示描述符结束。单个间接描述符表可以同时包含设备可读和设备可写描述符。

如果协商了 `VIRTIO_F_IN_ORDER`，间接描述符按顺序使用索引：0、1、2……

##### 2.7.5.3.1 Driver Requirements: Indirect Descriptors（驱动要求）

驱动**不得**在未协商 `VIRTIO_F_INDIRECT_DESC` 特性时设置 `VIRTQ_DESC_F_INDIRECT` 标志。

驱动**不得**在间接描述符内部再设置 `VIRTQ_DESC_F_INDIRECT`（即每个描述符表只允许一层间接）。

驱动**不得**创建长度超过设备 `QueueSize` 的描述符链。

驱动**不得**在 `flags` 中同时设置 `VIRTQ_DESC_F_INDIRECT` 和 `VIRTQ_DESC_F_NEXT`。

如果协商了 `VIRTIO_F_IN_ORDER`，间接描述符**必须**按顺序出现：第 1 个描述符的 `next` 为 1，第 2 个为 2，依此类推。

##### 2.7.5.3.2 Device Requirements: Indirect Descriptors（设备要求）

设备**必须**忽略引用间接表的那个描述符中的 `VIRTQ_DESC_F_WRITE` 标志。

设备**必须**处理“零个或多个普通链式描述符，后面跟着一个 `VIRTQ_DESC_F_INDIRECT` 描述符”的情况。

> 注意：这种布局不常见（大多数实现要么只使用非间接描述符链，要么只使用一个间接元素），但它是合法的。

### 2.7.6 The Virtqueue Available Ring（虚拟队列可用环）

可用环的布局如下：

```c
struct virtq_avail {
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1
    le16 flags;
    le16 idx;
    le16 ring[/* QueueSize */];
    le16 used_event; /* 仅当 VIRTIO_F_EVENT_IDX 时存在 */
};
```

驱动使用可用环向设备提供 buffer：环中的每个条目指向一条描述符链的链头。可用环只由驱动写、设备读。

`idx` 字段表示驱动下一次会把描述符条目放入环中的位置（对队列大小取模）。它从 0 开始并递增。

> 注意：传统接口的 [Virtio PCI Draft] 把该结构称为 `vring_avail`，把常量称为 `VRING_AVAIL_F_NO_INTERRUPT`，但布局和值完全相同。

#### 2.7.6.1 Driver Requirements: The Virtqueue Available Ring（驱动要求）

驱动**不得**递减 virtqueue 的 available `idx`（也就是说，没有办法把已经暴露的 buffer 再“收回”）。

### 2.7.7 Used Buffer Notification Suppression（已用缓冲区通知抑制）

如果没有协商 `VIRTIO_F_EVENT_IDX` 特性，可用环中的 `flags` 字段为驱动提供一种粗略机制，用来告诉设备：当 buffer 被使用时，驱动不需要通知。否则，`used_event` 是性能更好的替代方案：驱动可以指定设备在需要通知之前可以推进到多远。

这两种通知抑制方法都不是完全可靠的，因为它们没有与设备同步，但它们是有用的优化。

#### 2.7.7.1 Driver Requirements: Used Buffer Notification Suppression（驱动要求）

如果没有协商 `VIRTIO_F_EVENT_IDX`：

- 驱动**必须**把 `flags` 设为 0 或 1；
- 驱动**可以**把 `flags` 设为 1，告知设备不需要通知。

如果协商了 `VIRTIO_F_EVENT_IDX`：

- 驱动**必须**把 `flags` 设为 0；
- 驱动**可以**使用 `used_event` 告知设备：在设备把一个索引为由 `used_event` 指定的条目写入 used ring 之前（等价地，在 used ring 中的 `idx` 达到 `used_event + 1` 之前），不需要通知。

驱动**必须**处理来自设备的伪通知（spurious notifications）。

#### 2.7.7.2 Device Requirements: Used Buffer Notification Suppression（设备要求）

如果没有协商 `VIRTIO_F_EVENT_IDX`：

- 设备**必须**忽略 `used_event` 的值；
- 设备把描述符索引写入 used ring 之后：
  - 如果 `flags` 为 1，设备**不应**发送通知；
  - 如果 `flags` 为 0，设备**必须**发送通知。

如果协商了 `VIRTIO_F_EVENT_IDX`：

- 设备**必须**忽略 `flags` 的最低位；
- 设备把描述符索引写入 used ring 之后：
  - 如果 used ring 中的 `idx` 字段（它决定了该描述符索引被放置的位置）等于 `used_event`，设备**必须**发送通知；
  - 否则设备**不应**发送通知。

> 注意：例如，如果 `used_event` 为 0，那么使用 `VIRTIO_F_EVENT_IDX` 的设备会在第一个 buffer 被使用后向驱动发送 used buffer notification（之后会在第 65536 个 buffer 后再次发送，等等）。

### 2.7.8 The Virtqueue Used Ring（虚拟队列已用环）

已用环的布局如下：

```c
struct virtq_used {
#define VIRTQ_USED_F_NO_NOTIFY 1
    le16 flags;
    le16 idx;
    struct virtq_used_elem ring[/* QueueSize */];
    le16 avail_event; /* 仅当 VIRTIO_F_EVENT_IDX 时存在 */
};

/* 出于对齐/填充原因，id 使用 le32 */
struct virtq_used_elem {
    /* 已用描述符链的起始索引 */
    le32 id;
    /*
     * 设备写入描述符链所描述 buffer 的设备可写部分的字节数
     */
    le32 len;
};
```

已用环是设备完成 buffer 后归还它们的地方：它只由设备写、驱动读。

环中的每个条目是一对值：`id` 表示描述该 buffer 的描述符链的链头条目（它与 guest 之前放入 available ring 的条目对应），`len` 表示写入 buffer 的总字节数。

> 注意：`len` 对使用不可信 buffer 的驱动特别有用。如果驱动不知道设备到底写了多少字节，它就必须事先把 buffer 清零，以确保不会泄露数据。例如，网络驱动可能把接收 buffer 直接交给非特权用户空间应用；如果网络设备没有覆盖该 buffer 中原来的字节，就可能把其他进程释放内存的内容泄露给应用。

`idx` 字段表示设备下一次会把描述符条目放入环中的位置（对队列大小取模）。它从 0 开始并递增。

> 注意：传统接口的 [Virtio PCI Draft] 把这些结构称为 `vring_used` 和 `vring_used_elem`，把常量称为 `VRING_USED_F_NO_NOTIFY`，但布局和值完全相同。

#### 2.7.8.1 Legacy Interface: The Virtqueue Used Ring（传统接口说明）

历史上，许多驱动忽略了 `len` 值，因此许多设备把 `len` 设置错了。所以使用传统接口时，如果可能，通常最好忽略 used ring 条目中的 `len` 值。具体已知问题按设备类型列出。

#### 2.7.8.2 Device Requirements: The Virtqueue Used Ring（设备要求）

设备**必须**在更新 used `idx` 之前设置 `len`。

设备**必须**在更新 used `idx` 之前，从第一个设备可写 buffer 开始，向描述符写入至少 `len` 字节。

设备**可以**向描述符写入超过 `len` 字节。

> 注意：某些错误情况下，设备可能不知道 buffer 的哪些部分已经被写入。这就是允许 `len` 偏低的原因：这比让驱动误以为未初始化内存已经被覆盖要好。

#### 2.7.8.3 Driver Requirements: The Virtqueue Used Ring（驱动要求）

驱动**不得**对设备可写 buffer 中前 `len` 字节之后的数据做任何假设，并且**应该**忽略这些数据。

### 2.7.9 In-order use of descriptors（描述符的顺序使用）

有些设备总是按照描述符被提供的顺序使用描述符。这些设备可以提供 `VIRTIO_F_IN_ORDER` 特性。如果协商了该特性，设备可以通过只写入一条 used ring 条目（其 `id` 对应批次中最后一个 buffer 所对应描述符链的链头）来通知驱动一批 buffer 已被使用。

设备随后根据批次大小在环中向前跳过，并相应地把 used `idx` 增加批次大小。

驱动需要查找 used `id` 并计算批次大小，才能推进到设备下一次写入 used ring 条目的位置。

这样，used ring 条目的偏移会与批次中第一个 available ring 条目对应，下一批的 used ring 条目会与下一批的第一个 available ring 条目对应，依此类推。

被跳过的 buffer（没有为它们写 used ring 条目的 buffer）被假定已被设备完整使用（读取或写入）。

### 2.7.10 Available Buffer Notification Suppression（可用缓冲区通知抑制）

设备可以用类似 2.7.7 节中驱动抑制 used buffer notification 的方式，抑制 available buffer notification。设备在 used ring 中操作 `flags` 或 `avail_event`，就像驱动在 available ring 中操作 `flags` 或 `used_event` 一样。

#### 2.7.10.1 Driver Requirements: Available Buffer Notification Suppression（驱动要求）

驱动在分配 used ring 时**必须**把 used ring 中的 `flags` 初始化为 0。

如果没有协商 `VIRTIO_F_EVENT_IDX`：

- 驱动**必须**忽略 `avail_event` 的值；
- 驱动把描述符索引写入 available ring 之后：
  - 如果 `flags` 为 1，驱动**不应**发送通知；
  - 如果 `flags` 为 0，驱动**必须**发送通知。

如果协商了 `VIRTIO_F_EVENT_IDX`：

- 驱动**必须**忽略 `flags` 的最低位；
- 驱动把描述符索引写入 available ring 之后：
  - 如果 available ring 中的 `idx` 字段（它决定了该描述符索引被放置的位置）等于 `avail_event`，驱动**必须**发送通知；
  - 否则驱动**不应**发送通知。

#### 2.7.10.2 Device Requirements: Available Buffer Notification Suppression（设备要求）

如果没有协商 `VIRTIO_F_EVENT_IDX`：

- 设备**必须**把 `flags` 设为 0 或 1；
- 设备**可以**把 `flags` 设为 1，告知驱动不需要通知。

如果协商了 `VIRTIO_F_EVENT_IDX`：

- 设备**必须**把 `flags` 设为 0；
- 设备**可以**使用 `avail_event` 告知驱动：在驱动把一个索引为由 `avail_event` 指定的条目写入 available ring 之前（等价地，在 available ring 中的 `idx` 达到 `avail_event + 1` 之前），不需要通知。

设备**必须**处理来自驱动的伪通知。

### 2.7.11 Helpers for Operating Virtqueues（操作虚拟队列的辅助代码）

Linux 内核源码在 `include/uapi/linux/virtio_ring.h` 中以更可用的形式包含了上述定义和辅助函数。IBM 和 Red Hat 明确以（三条款）BSD 许可证授权，供所有其他项目自由使用，并在 `A virtio_queue.h` 中（略有改动地）复现。

### 2.7.12 Virtqueue Operation（虚拟队列操作）

虚拟队列操作分为两部分：向设备提供新的 available buffer，以及处理来自设备的 used buffer。

> 注意：例如，最简单的 virtio 网络设备有两个 virtqueue：发送 virtqueue 和接收 virtqueue。驱动把出站（设备可读）数据包加入发送 virtqueue，并在它们被使用后释放；类似地，入站（设备可写）buffer 被加入接收 virtqueue，并在使用后处理。

下面是使用 split virtqueue 格式时，这两部分各自的详细要求。

### 2.7.13 Supplying Buffers to The Device（向设备提供缓冲区）

驱动按以下步骤向设备的某个 virtqueue 提供 buffer：

1. 驱动把 buffer 放入描述符表中的空闲描述符，必要时把它们串成链（见 2.7.5 节）；
2. 驱动把描述符链链头的索引放入 available ring 的下一个环条目；
3. 如果可以批处理，步骤 1 和 2 **可以**重复多次；
4. 驱动执行合适的内存屏障，确保设备在下一步之前能看到更新后的描述符表和 available ring；
5. 把 available `idx` 增加为 available ring 新增的描述符链头数量；
6. 驱动执行合适的内存屏障，确保在检查通知抑制之前先更新 `idx` 字段；
7. 如果没有抑制通知，驱动向设备发送 available buffer notification。

注意，上述过程没有专门防止 available ring 回绕：这是不可能的，因为环 buffer 与描述符表大小相同，步骤 1 会阻止这种情况。

此外，最大队列长度是 32768（能放进 16 位的最大 2 的幂），所以 16 位 `idx` 值总能区分满和空 buffer。

下面更详细地说明每个阶段的要求。

#### 2.7.13.1 Placing Buffers Into The Descriptor Table（把缓冲区放入描述符表）

一个 buffer 由零个或多个设备可读的物理连续元素，后跟零个或多个物理连续设备可写元素组成（每类至少有一个元素）。下面的算法把它映射到描述符表，形成描述符链：

对每个 buffer 元素 `b`：

1. 取得下一个空闲的描述符表条目 `d`；
2. 把 `d.addr` 设为 `b` 起始处的物理地址；
3. 把 `d.len` 设为 `b` 的长度；
4. 如果 `b` 是设备可写，把 `d.flags` 设为 `VIRTQ_DESC_F_WRITE`，否则设为 0；
5. 如果后面还有 buffer 元素：
   - (a) 把 `d.next` 设为下一个空闲描述符元素的索引；
   - (b) 在 `d.flags` 中设置 `VIRTQ_DESC_F_NEXT` 位。

实践中，`d.next` 通常用于把空闲描述符串起来，并单独维护计数器，以便在开始映射之前检查是否有足够的空闲描述符。

#### 2.7.13.2 Updating The Available Ring（更新可用环）

描述符链链头就是上述算法中的第一个 `d`，即指向 buffer 第一部分的描述符表条目索引。一个简单的驱动实现可以这样做（假设进行了适当的小端序转换）：

```c
avail->ring[avail->idx % qsz] = head;
```

不过，一般来说，驱动可以在更新 `idx` 之前添加许多描述符链（更新 `idx` 后它们才对设备可见），所以通常会维护一个计数器，记录驱动已经添加了多少条：

```c
avail->ring[(avail->idx + added++) % qsz] = head;
```

#### 2.7.13.3 Updating idx（更新 idx）

`idx` 总是递增，并在 65536 处自然回绕：

```c
avail->idx += added;
```

驱动更新 available `idx` 之后，就会暴露描述符及其内容。设备**可以**立即访问驱动创建的描述符链以及它们引用的内存。

##### 2.7.13.3.1 Driver Requirements: Updating idx（驱动要求）

驱动**必须**在更新 `idx` 之前执行合适的内存屏障，以确保设备看到最新副本。

#### 2.7.13.4 Notifying The Device（通知设备）

设备通知的具体方法与总线相关，但通常开销较大。因此，如果设备不需要通知，它可以抑制这些通知，详见 2.7.10 节。

驱动必须小心：在检查通知是否被抑制之前，先暴露新的 `idx` 值。

##### 2.7.13.4.1 Driver Requirements: Notifying The Device（驱动要求）

驱动**必须**在读取 `flags` 或 `avail_event` 之前执行合适的内存屏障，以避免漏掉通知。

### 2.7.14 Receiving Used Buffers From The Device（接收来自设备的已用缓冲区）

一旦设备使用了描述符引用的 buffer（根据 virtqueue 和设备的性质，可能是从 buffer 读取、向 buffer 写入，或两者都有），设备就会按 2.7.7 节所述向驱动发送 used buffer notification。

> 注意：为了获得最佳性能，驱动可以在处理 used ring 时禁用 used buffer notifications，但要注意“清空环”和“重新启用通知”之间可能漏掉通知的问题。通常通过在重新启用通知之后再次检查是否有更多 used buffer 来处理：

```c
virtq_disable_used_buffer_notifications(vq);

for (;;) {
    if (vq->last_seen_used != le16_to_cpu(virtq->used.idx)) {
        virtq_enable_used_buffer_notifications(vq);
        mb();

        if (vq->last_seen_used != le16_to_cpu(virtq->used.idx)) {
            break;
        }

        virtq_disable_used_buffer_notifications(vq);
    }

    struct virtq_used_elem *e = virtq.used->ring[vq->last_seen_used % vsz];
    process_buffer(e);
    vq->last_seen_used++;
}
```

---

## 与 TinyOS BATCH35 的对应关系

| 协议结构 | TinyOS 驱动中的对象 |
|---|---|
| Descriptor Table | `g_descriptors` |
| Available Ring | `g_available` |
| Used Ring | `g_used` |
| 请求头 | `VirtioBlkReqHeader` |
| 数据区 | 驱动私有 512 字节 buffer |
| 状态字节 | `status_byte` |
| `QueueDesc` / `QueueDriver` / `QueueDevice` 寄存器 | 分别写入上述三块内存的 guest 物理地址 |

BATCH35 只需要实现 split virtqueue，不需要 packed virtqueue；因此 2.8 节暂不翻译。

## 待续章节

- 2.8 Packed Virtqueues（打包式虚拟队列，BATCH35 不需要）
- 3.1 Device Initialization（设备初始化）
- 4.2 Virtio Over MMIO（MMIO 寄存器布局与初始化）
- 5.2 Block Device（块设备请求格式与操作）
