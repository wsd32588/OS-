# VirtIO 1.2 中文翻译（节选）：Packed Virtqueue 与杂项设施

> 原文：*Virtual I/O Device (VIRTIO) Version 1.2*，OASIS，2022-07-01。
>
> 本文件翻译第 **2.8–2.11 节**：Packed Virtqueues、Driver Notifications、Shared Memory Regions、Exporting Objects。
>
> 翻译约定：MUST = 必须；MUST NOT = 禁止；SHOULD = 应该；SHOULD NOT = 不应；MAY = 可以。

---

## 2.8 Packed Virtqueues（打包式虚拟队列）

Packed virtqueue 是一种紧凑的替代布局，它使用**读写内存**，也就是宿主和 guest 都会读写的内存。

是否使用 packed virtqueue 由 `VIRTIO_F_RING_PACKED` 特性位协商。

Packed virtqueue 每个最多支持 **2^15** 个条目。

在当前传输层下，virtqueue 位于驱动分配的 guest 内存中。每个 packed virtqueue 由三部分组成：

- **Descriptor Ring（描述符环）**：占据 Descriptor Area；
- **Driver Event Suppression（驱动事件抑制）**：占据 Driver Area；
- **Device Event Suppression（设备事件抑制）**：占据 Device Area。

Descriptor Ring 又由描述符组成，每个描述符包含：

- Buffer ID
- Element Address
- Element Length
- Flags

一个 buffer 由零个或多个设备可读的物理连续元素，后跟零个或多个物理连续设备可写元素组成（每个 buffer 至少有一个元素）。

驱动想向设备发送这样的 buffer 时，会把至少一个描述 buffer 元素的 available descriptor 写入 Descriptor Ring。描述符通过描述符内的 Buffer ID 与 buffer 关联。

驱动随后通知设备。设备处理完 buffer 后，把一个包含该 Buffer ID 的 used 设备描述符写回 Descriptor Ring（覆盖之前由驱动提供、现在已使用的描述符），并发送 used event notification。

Descriptor Ring 以环形方式使用：驱动按顺序写入描述符；到达环尾后，下一个描述符放在环头。环被驱动描述符填满后，驱动停止发送新请求，等待设备开始处理并写出一些 used 描述符，然后才能继续提供新的驱动描述符。

类似地，设备按顺序读取描述符，发现某个驱动描述符已经 available；处理完成后，把 used 描述符写回环中。

> 注意：设备按顺序读取驱动描述符并开始处理后，**可以乱序完成**。Used 设备描述符按处理完成的顺序写入。

**DeviceEventSuppression** 结构由设备只写。它包含减少设备事件（即减少向设备发送 available buffer notification）的信息。

**DriverEventSuppression** 结构由设备只读。它包含减少驱动事件（即减少向驱动发送 used buffer notification）的信息。

### 2.8.1 Driver and Device Ring Wrap Counters（驱动和设备的环回绕计数器）

驱动和设备各自在内部维护一个单比特的**环回绕计数器**，初始值为 1。

- 驱动维护的计数器称为 **Driver Ring Wrap Counter**。驱动每次把环中最后一个描述符变为 available 后，翻转该计数器。
- 设备维护的计数器称为 **Device Ring Wrap Counter**。设备每次使用环中最后一个描述符（把它标记为 used）后，翻转该计数器。

当驱动和设备在处理同一个描述符，或者所有 available 描述符都已被使用时，Driver Ring Wrap Counter 与 Device Ring Wrap Counter 是匹配的。

为了把描述符标记为 available 和 used，驱动和设备使用以下两个标志：

```c
#define VIRTQ_DESC_F_AVAIL  (1 << 7)
#define VIRTQ_DESC_F_USED   (1 << 15)
```

驱动把描述符标记为 available 时：

- 在 `Flags` 中设置 `VIRTQ_DESC_F_AVAIL`，使其匹配当前 Driver Ring Wrap Counter；
- 设置 `VIRTQ_DESC_F_USED` 为**相反值**（即不匹配 Driver Ring Wrap Counter）。

设备把描述符标记为 used 时：

- 在 `Flags` 中设置 `VIRTQ_DESC_F_USED`，使其匹配当前 Device Ring Wrap Counter；
- 同时设置 `VIRTQ_DESC_F_AVAIL` 为**相同值**。

因此：

- available 描述符的 `AVAIL` 和 `USED` 位不同；
- used 描述符的 `AVAIL` 和 `USED` 位相同。

> 注意：这个观察主要用于健全性检查；它们是必要条件但不是充分条件（例如所有描述符初始为 0）。驱动/设备可以跟踪上次观察到的 `AVAIL/USED` 值来检测状态变化。

### 2.8.2 Polling of available and used descriptors（轮询 available/used 描述符）

设备描述符和驱动描述符的写入通常可以被重排，但每一侧（驱动/设备）只需要轮询（或测试）内存中的**一个位置**：按环形顺序，它上次处理过的描述符之后的**下一个**设备描述符。

有时设备在处理一批多个 available 描述符后，只需要写出一个 used 描述符。使用描述符链或 in-order 使用描述符时会发生这种情况。此时设备写出一个 used 描述符，其 Buffer ID 是组中最后一个描述符的 ID。处理完这个 used 描述符后，设备和驱动会按组中剩余描述符数量向前跳过，直到处理下一个 used 描述符。

### 2.8.3 Write Flag（写标志）

在 available 描述符中，`Flags` 里的 `VIRTQ_DESC_F_WRITE` 位用于把描述符标记为对应 buffer 的只写元素或只读元素。

```c
/* 表示描述符是设备只写的（否则为设备只读） */
#define VIRTQ_DESC_F_WRITE 2
```

在 used 描述符中，该位用于说明设备是否向 buffer 的任意部分写入了数据。

### 2.8.4 Element Address and Length（元素地址和长度）

在 available 描述符中：

- `ElementAddress` 是 buffer 元素的物理地址；
- 假定物理连续的该元素长度存放在 `ElementLength` 中。

在 used 描述符中：

- `ElementAddress` 不使用；
- `ElementLength` 表示设备已经初始化（写入）的 buffer 长度。

对于没有 `VIRTQ_DESC_F_WRITE` 标志的 used 描述符，`ElementLength` 是保留的，驱动应忽略。

### 2.8.5 Scatter-Gather Support（分散/聚集支持）

有些驱动需要在一个请求中提供多个 buffer 元素（scatter/gather list）。有两种特性支持：

- 描述符链（descriptor chaining）；
- 间接描述符（indirect descriptors）。

如果两者都不使用，每个 buffer 都是物理连续的，要么只读、要么只写，并完全由一个描述符描述。

虽然不常见，但如果两个特性都协商了，在一个环中混合间接和非间接描述符是合法的，只要每个 list 只包含一种类型的描述符。

Scatter/gather list 只适用于 available 描述符。一个 used 描述符对应整个 list。

设备通过传输层相关和/或设备相关的值限制一个 list 中的描述符数量。如果没有限制，最大数量就是 virtqueue size。

### 2.8.6 Next Flag: Descriptor Chaining（Next 标志：描述符链）

Packed ring 允许驱动使用多个描述符向设备提供 scatter/gather list：除最后一个 available 描述符外，所有描述符都设置 `VIRTQ_DESC_F_NEXT`。

```c
/* 表示 buffer 继续到下一个描述符 */
#define VIRTQ_DESC_F_NEXT 1
```

Buffer ID 包含在 list 的**最后一个**描述符中。

驱动总是在 list 其余描述符写入环之后，才把 list 的第一个描述符变为 available。这保证设备永远不会看到不完整的 scatter/gather list。

> 注意：list 中所有描述符的所有 flags（包括 `AVAIL`、`USED`、`WRITE`）都必须正确设置/清除，不能只设置第一个描述符。

设备只为整个 list 写出一个 used 描述符，然后按 list 中的描述符数量向前跳过。驱动需要记录每个 Buffer ID 对应的 list 大小，才能跳到设备下一次写 used 描述符的位置。

例如，如果描述符按 available 顺序使用，used 描述符会覆盖 list 的第一个 available 描述符，下一批的 used 描述符会覆盖下一批的第一个 available 描述符，依此类推。

`VIRTQ_DESC_F_NEXT` 在 used 描述符中是保留的，驱动应忽略。

### 2.8.7 Indirect Flag: Scatter-Gather Support（Indirect 标志）

有些设备需要同时派发大量大请求，`VIRTIO_F_INDIRECT_DESC` 特性允许这样做。为了增加环容量，驱动可以在内存任意位置存放一张（设备只读的）间接描述符表，并在主 virtqueue 中插入一个设置 `VIRTQ_DESC_F_INDIRECT` 的描述符，指向包含该间接表的内存元素；`addr` 和 `len` 分别表示间接表地址和字节长度。

```c
/* 表示该元素包含一张描述符表 */
#define VIRTQ_DESC_F_INDIRECT 4
```

间接表布局：

```c
struct pvirtq_indirect_descriptor_table {
    /* 实际描述符结构（每个 struct pvirtq_desc） */
    struct pvirtq_desc desc[len / sizeof(struct pvirtq_desc)];
};
```

第一个描述符位于间接表起始处，后续间接描述符紧接在后面。间接表中的描述符**只允许** `VIRTQ_DESC_F_WRITE` 标志；其他标志保留，设备忽略。Buffer ID 也是保留的，设备忽略。

对于设置了 `VIRTQ_DESC_F_INDIRECT` 的描述符，`VIRTQ_DESC_F_WRITE` 是保留的，设备忽略。

### 2.8.8 In-order use of descriptors（按序使用描述符）

有些设备总是按描述符 available 的顺序使用描述符。这些设备可以提供 `VIRTIO_F_IN_ORDER`。如果协商了该特性，设备可以只写一个 used 描述符（其 Buffer ID 对应批次中最后一个描述符）来通知驱动一批 buffer 已被使用。

设备随后按批次大小向前跳过。驱动需要查找 used Buffer ID 并计算批次大小，才能推进到设备下一次写 used 描述符的位置。

这样 used 描述符会覆盖批次中的第一个 available 描述符，下一批的 used 描述符会覆盖下一批的第一个 available 描述符，依此类推。

被跳过的 buffer（没有写出 used 描述符的 buffer）被假定已被设备完整使用。

### 2.8.9 Multi-buffer requests（多 buffer 请求）

有些设备在处理单个请求时组合多个 buffer。这些设备总是在请求中其余描述符（按环顺序跟在第一个描述符后面的那些）都被标记为 used 并写入环之后，才把请求中第一个 buffer 对应的描述符标记为 used。这保证驱动永远不会在环中看到不完整的请求。

### 2.8.10 Driver and Device Event Suppression（驱动/设备事件抑制）

在许多系统中，used/available buffer notification 开销较大。为了缓解，每个 virtqueue 包含两个相同的结构，用于控制设备与驱动之间的通知。

- **DriverEventSuppression**：设备只读，控制设备发给驱动的 used buffer notification；
- **DeviceEventSuppression**：驱动只读，控制驱动发给设备的 available buffer notification。

每个 Event Suppression 结构包含以下字段：

- **Descriptor Ring Change Event Flags**：
  ```c
  #define RING_EVENT_FLAGS_ENABLE  0x0  /* 启用事件 */
  #define RING_EVENT_FLAGS_DISABLE 0x1  /* 禁用事件 */
  #define RING_EVENT_FLAGS_DESC    0x2  /* 对特定描述符启用事件；仅当协商了 VIRTIO_F_EVENT_IDX */
  /* 0x3 保留 */
  ```
- **Descriptor Ring Change Event Offset**：如果 flags 为 DESC，表示环内偏移（以描述符大小为单位）；仅当该描述符变为 available/used 时触发事件。
- **Descriptor Ring Change Event Wrap Counter**：如果 flags 为 DESC，表示环回绕计数器；仅当 Wrap Counter 匹配且描述符变为 available/used 时触发。

设备/驱动写出一些描述符后，应查询对应结构，判断是否应发送 used/available buffer notification。

#### 2.8.10.1 Structure Size and Alignment（结构大小与对齐）

| Virtqueue 部分 | 对齐 | 大小 |
|---|---:|---:|
| Descriptor Ring | 16 | `16 * QueueSize` |
| Device Event Suppression | 4 | 4 |
| Driver Event Suppression | 4 | 4 |

`QueueSize` 对应 virtqueue 中描述符的最大数量。packed virtqueue 的 `QueueSize` **不要求**是 2 的幂。

### 2.8.11 Driver Requirements: Virtqueues（驱动要求）

驱动**必须**保证每个 virtqueue 部分第一个字节的物理地址是指定对齐值的整数倍。

### 2.8.12 Device Requirements: Virtqueues（设备要求）

- 设备**必须**按描述符在环中出现的顺序开始处理驱动描述符；
- 设备**必须**按完成顺序开始把设备描述符写入环；
- 设备**可以**在开始写入后重排描述符写入。

### 2.8.13 The Virtqueue Descriptor Format（虚拟队列描述符格式）

Available 描述符描述驱动发送给设备的 buffer。`addr` 是物理地址，描述符通过 `id` 字段与 buffer 关联。

```c
struct pvirtq_desc {
    /* Buffer 地址 */
    le64 addr;
    /* Buffer 长度 */
    le32 len;
    /* Buffer ID */
    le16 id;
    /* 取决于描述符类型的 flags */
    le16 flags;
};
```

Descriptor Ring 初始化为 0。

### 2.8.14 Event Suppression Structure Format（事件抑制结构格式）

```c
struct pvirtq_event_suppress {
    le16 {
        desc_event_off : 15;   /* Descriptor Ring Change Event Offset */
        desc_event_wrap : 1;   /* Descriptor Ring Change Event Wrap Counter */
    } desc;  /* 当 desc_event_flags 为 RING_EVENT_FLAGS_DESC 时使用 */

    le16 {
        desc_event_flags : 2;  /* Descriptor Ring Change Event Flags */
        reserved : 14;         /* 保留，设为 0 */
    } flags;
};
```

### 2.8.15 Device Requirements: The Virtqueue Descriptor Table（设备要求）

- 设备**不得**写设备可读 buffer；
- 设备**不应**读设备可写 buffer；
- 设备**不得**在观察到描述符 `flags` 中的 `VIRTQ_DESC_F_AVAIL` 位发生变化（相对于初始 0 值）之前使用该描述符；
- 设备在改变描述符的 `VIRTQ_DESC_F_USED` 位之后，**不得**再改变该描述符。

### 2.8.16 Driver Requirements: The Virtqueue Descriptor Table（驱动要求）

- 驱动**不得**在观察到描述符 `flags` 中的 `VIRTQ_DESC_F_USED` 位发生变化之前改变该描述符；
- 驱动在改变描述符的 `VIRTQ_DESC_F_AVAIL` 位之后，**不得**再改变该描述符；
- 通知设备时，驱动**必须**把 `next_off` 和 `next_wrap` 设置为下一个尚未变为 available 的描述符；
- 驱动**可以**在没有提供任何新描述符的情况下发送多次 available buffer notification。

### 2.8.17 Driver Requirements: Scatter-Gather Support（驱动要求）

- 驱动**不得**创建超过设备允许长度的描述符 list；
- 驱动**不得**创建超过 `QueueSize` 的描述符 list；这意味着 list 中禁止出现循环；
- 驱动**必须**把所有设备可写描述符元素放在所有设备可读元素之后；
- 驱动**不得**依赖设备使用更多描述符才能写出 list 中所有描述符；驱动在把 list 第一个描述符变为 available 之前，**必须**确保环中有足够空间容纳整个 list；
- 驱动**不得**在 list 中所有后续描述符都 available 之前，把第一个描述符变为 available。

### 2.8.18 Device Requirements: Scatter-Gather Support（设备要求）

- 设备**必须**按照驱动提供 list 的顺序，使用通过 `VIRTQ_DESC_F_NEXT` 链起来的描述符；
- 设备**可以**限制一个 list 中允许的 buffer 数量。

### 2.8.19 Driver Requirements: Indirect Descriptors（驱动要求）

- 驱动**不得**在未协商 `VIRTIO_F_INDIRECT_DESC` 时设置 `VIRTQ_DESC_F_INDIRECT`；
- 驱动在间接描述符内**不得**设置除 `VIRTQ_DESC_F_WRITE` 之外的任何标志；
- 驱动**不得**创建超过设备允许长度的描述符链；
- 驱动**不得**把设置了 `VIRTQ_DESC_F_INDIRECT` 的直接描述符放进通过 `VIRTQ_DESC_F_NEXT` 链接的 scatter/gather list。

### 2.8.20 Virtqueue Operation（虚拟队列操作）

虚拟队列操作分两部分：向设备提供新的 available buffer，以及处理来自设备的 used buffer。

### 2.8.21 Supplying Buffers to The Device（向设备提供缓冲区）

驱动按以下步骤向设备的某个 virtqueue 提供 buffer：

1. 驱动把 buffer 放入 Descriptor Ring 中的空闲描述符；
2. 驱动执行合适的内存屏障，确保在检查通知抑制之前先更新描述符；
3. 如果没有抑制通知，驱动通知设备有新的 available buffer。

#### 2.8.21.1 Placing Available Buffers Into The Descriptor Ring（放入描述符环）

对每个 buffer 元素 `b`：

1. 取得下一个描述符表条目 `d`；
2. 取得下一个空闲 Buffer ID；
3. 把 `d.addr` 设为 `b` 起始处的物理地址；
4. 把 `d.len` 设为 `b` 的长度；
5. 把 `d.id` 设为 Buffer ID；
6. 计算 flags：
   - (a) 如果 `b` 是设备可写，设置 `VIRTQ_DESC_F_WRITE` 为 1，否则为 0；
   - (b) 把 `VIRTQ_DESC_F_AVAIL` 设为当前 Driver Ring Wrap Counter；
   - (c) 把 `VIRTQ_DESC_F_USED` 设为相反值；
7. 执行内存屏障，确保描述符已初始化；
8. 把 `d.flags` 设为计算出的 flags；
9. 如果 `d` 是环中最后一个描述符，翻转 Driver Ring Wrap Counter；
10. 否则，把 `d` 推进到下一个描述符。

这使单个描述符 buffer 变为 available。一般来说，驱动可以在一个请求中使用一批描述符；此时它会推迟更新第一个描述符的 flags（以及之前的内存屏障），直到其余描述符都初始化完成。

一旦驱动更新了描述符的 flags 字段，该描述符及其内容就暴露给设备。设备**可以**立即访问该描述符、驱动创建的后续描述符以及它们引用的内存。

##### 2.8.21.1.1 Driver Requirements: Updating flags（驱动要求）

驱动**必须**在更新 flags 之前执行合适的内存屏障，确保设备看到最新副本。

#### 2.8.21.2 Sending Available Buffer Notifications（发送可用缓冲区通知）

设备通知的具体方法与总线相关，但通常开销较大。设备可以使用 Device Area 中的 Event Suppression 结构抑制通知（见 2.8.14 节）。

驱动必须小心：在检查通知是否被抑制之前，先暴露新的 flags 值。

#### 2.8.21.3 Implementation Example（实现示例）

原文给出了一段驱动示例代码，核心逻辑与上面的步骤一致：

- `vq->avail_wrap_count` 初始为 1；
- 为每个 buffer 元素填写 `address/len/id/flags`；
- 第一个描述符的 flags 暂存，等所有后续描述符写完后再写；
- 最后一个描述符写 Buffer ID；
- 写内存屏障；
- 如果 Device Event Suppression 没有禁用通知，则通知设备。

##### 2.8.21.3.1 Driver Requirements: Sending Available Buffer Notifications（驱动要求）

驱动**必须**在读取 Device Area 中的 Event Suppression 结构之前执行合适的内存屏障。否则可能漏发必须发送的 available buffer notification。

### 2.8.22 Receiving Used Buffers From The Device（接收已用缓冲区）

设备使用完描述符引用的 buffer 后，会按 2.8.14 节向驱动发送 used buffer notification。

> 注意：为了性能，驱动可以在处理 used buffer 时禁用 used buffer notification，但要注意“清空环”和“重新启用通知”之间漏通知的问题。通常做法是：重新启用通知后再次检查是否有更多 used buffer。

原文示例代码的核心逻辑：

- 把 `driver_event.flags` 设为 `RING_EVENT_FLAGS_DISABLE`；
- 检查 `next_used` 描述符的 `AVAIL/USED` 位是否与 `used_wrap_count` 匹配；
- 如果不匹配，先启用事件并重试；仍不匹配则退出；
- 读取 used 描述符，按 Buffer ID 查 list 大小，推进 `next_used`；
- 释放 ID，处理 buffer。

---

## 2.9 Driver Notifications（驱动通知）

有时驱动需要向设备发送 available buffer notification。

未协商 `VIRTIO_F_NOTIFICATION_DATA` 时，这种通知只是把 virtqueue 编号发送给设备（具体方法取决于传输层）。

但有些设备希望在不访问内存中的 virtqueue 的情况下，就能知道队列中有多少可用数据（为了效率或调试）。为了支持这种优化，协商 `VIRTIO_F_NOTIFICATION_DATA` 后，驱动发给设备的通知包含：

- `vqn`：要通知的 virtqueue 编号；
- `next_off`：环中下一个 available ring 条目将被写入的偏移。
  - 未协商 `VIRTIO_F_RING_PACKED` 时，它指 available index 的最低 15 位；
  - 协商了 `VIRTIO_F_RING_PACKED` 时，它指描述符环中下一个 available 描述符的偏移（以描述符条目为单位）。
- `next_wrap`：回绕计数器。
  - 协商了 `VIRTIO_F_RING_PACKED` 时，它指下一个 available 描述符对应的 wrap counter；
  - 未协商 `VIRTIO_F_RING_PACKED` 时，它是 available index 的最高位（bit 15）。

注意：驱动可以在没有提供任何新 buffer 的情况下发送多次通知。协商 `VIRTIO_F_NOTIFICATION_DATA` 后，这些通知的 `next_off` 和 `next_wrap` 值会相同。

---

## 2.10 Shared Memory Regions（共享内存区域）

共享内存区域是给设备的一种额外设施：设备需要一块持续与驱动共享的内存，而不是像 virtqueue 元素那样在两者之间传递。

示例用途包括共享缓存，以及用于版本化数据结构的版本池。

内存区域由设备分配并提供给驱动。如果设备由宿主上的软件实现，这种安排允许内存区域由宿主上的库分配，而设备本身可能无法完全控制这块内存。

一个设备可以有多个共享内存区域。每个区域有一个 `shmid` 来标识，含义由设备定义。

共享内存区域的枚举和位置由传输层相关方式完成。

内存一致性规则因区域和设备而异，由各设备类型规定。

### 2.10.1 Addressing within regions（区域内的寻址）

共享内存区域中的引用使用**从区域起始处开始的偏移**表示，而不是绝对内存地址。偏移既用于共享内存内部结构之间的引用，也用于 virtqueue 中引用共享内存的请求。`shmid` 可以显式给出，也可以从引用上下文中推断。

### 2.10.2 Device Requirements: Shared Memory Regions（设备要求）

设备**不得**暴露用于控制设备操作或传输数据的共享内存区域。

---

## 2.11 Exporting Objects（导出对象）

当一个 virtio 设备创建的对象需要与另一个独立的 virtio 设备共享时，第一个设备可以通过生成一个 UUID 来**导出**该对象，然后把这个 UUID 传给第二个设备以标识该对象。

什么构成对象、如何导出/导入对象，由各设备类型定义。建议设备按照 [RFC4122] 生成 **version 4 UUID**。

---

## 与 TinyOS BATCH35 的关系

BATCH35 使用的是 **Split Virtqueue**，不是本文件的 Packed Virtqueue。因此：

- 2.8 节主要用于理解 QEMU/VirtIO 的另一种队列格式；
- 2.9 节的 `VIRTIO_F_NOTIFICATION_DATA` 在 BATCH35 中未协商；
- 2.10/2.11 与 BATCH35 无直接关系。

BATCH35 的核心仍是 2.7 节的 split virtqueue、4.2 节的 MMIO、5.2 节的块设备。
