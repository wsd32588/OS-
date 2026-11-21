# VirtIO 1.2 中文翻译（节选）：Virtio 设备的基本设施

> 原文：*Virtual I/O Device (VIRTIO) Version 1.2*，OASIS，2022-07-01。
>
> 本文件翻译第 **2.1–2.5 节**：设备状态字段、特性位、通知、设备重置、设备配置空间。
>
> 翻译约定：MUST = 必须；MUST NOT = 禁止；SHOULD = 应该；SHOULD NOT = 不应；MAY = 可以。

---

## 2. Virtio 设备的基本设施

VirtIO 设备通过总线相关的方式被发现和识别（见第 4.1 节 PCI、4.2 节 MMIO、4.3 节 Channel I/O）。

每个设备由以下部分组成：

- 设备状态字段（Device status field）
- 特性位（Feature bits）
- 通知（Notifications）
- 设备配置空间（Device Configuration space）
- 一个或多个 virtqueue

### 2.1 Device Status Field（设备状态字段）

驱动初始化设备时，会遵循 3.1 节规定的步骤顺序。设备状态字段用简单的底层指示表示这些步骤的完成情况。可以把它想象成控制台上的一组信号灯，表示每个设备的状态。

定义的状态位如下（按通常设置的顺序列出）：

| 位 | 名称 | 值 | 含义 |
|---|---|---:|---|
| 0 | `ACKNOWLEDGE` | 1 | guest OS 已经发现该设备，并确认它是合法的 virtio 设备 |
| 1 | `DRIVER` | 2 | guest OS 知道如何驱动该设备 |
| 7 | `FAILED` | 128 | guest 中发生了错误，已经放弃该设备。可能是内部错误、驱动不喜欢该设备，或设备操作期间的致命错误 |
| 3 | `FEATURES_OK` | 8 | 驱动已经确认它理解的所有特性，特性协商完成 |
| 2 | `DRIVER_OK` | 4 | 驱动已经完成设置，准备好驱动设备 |
| 6 | `DEVICE_NEEDS_RESET` | 64 | 设备遇到了无法恢复的错误 |

设备状态字段初始为 0，设备重置时会重新初始化为 0。

> 注意：设置 `DRIVER` 位之前可能有很长（甚至无限）的延迟。例如在 Linux 下，驱动可以是可加载模块。

#### 2.1.1 Driver Requirements: Device Status Field（驱动要求）

- 驱动**必须**更新设备状态，通过设置位来表示 3.1 节初始化序列中已完成的步骤。
- 驱动**不得**清除设备状态位。
- 如果驱动设置了 `FAILED` 位，之后在尝试重新初始化之前，**必须**重置设备。
- 如果 `DEVICE_NEEDS_RESET` 被设置，驱动**不应**依赖设备操作完成。
- 注意：例如，`DEVICE_NEEDS_RESET` 被设置时，驱动不能假设正在进行的请求会完成，也不能假设它们没有完成。好的实现会尝试通过发出重置来恢复。

#### 2.1.2 Device Requirements: Device Status Field（设备要求）

- 在 `DRIVER_OK` 之前，设备**不得**消费 buffer，也**不得**向驱动发送任何 used buffer notification。
- 当设备进入需要重置的错误状态时，**应该**设置 `DEVICE_NEEDS_RESET`。
- 如果 `DRIVER_OK` 已设置，设备在设置 `DEVICE_NEEDS_RESET` 之后，**必须**向驱动发送设备配置变化通知。

### 2.2 Feature Bits（特性位）

每个 virtio 设备提供它理解的所有特性。设备初始化期间，驱动读取这些特性，并告诉设备自己接受哪个子集。重新协商特性的唯一方式是重置设备。

这允许向前和向后兼容：

- 如果设备增加了新的特性位，旧驱动不会把该特性位写回设备；
- 如果驱动增加了设备不支持的新特性，它会看到设备没有提供该特性。

特性位分配如下：

| 范围 | 用途 |
|---|---|
| 0–23、50–127 | 特定设备类型的特性位 |
| 24–40 | 保留给队列机制和特性协商机制的扩展 |
| 41–49、128 及以上 | 保留给未来扩展 |

> 注意：例如，网络设备（Device ID 1）的特性位 0 表示设备支持数据包校验和。
>
> 特别地，设备配置空间中的新字段通过提供新的特性位来表示。

#### 2.2.1 Driver Requirements: Feature Bits（驱动要求）

- 驱动**不得**接受设备没有提供的特性，也**不得**接受依赖未接受特性的特性。
- 如果设备没有提供驱动理解的某个特性，驱动**应该**进入向后兼容模式；否则**必须**设置 `FAILED` 设备状态位并停止初始化。

#### 2.2.2 Device Requirements: Feature Bits（设备要求）

- 设备**不得**提供依赖未提供特性的特性。
- 设备**应该**接受驱动接受的任何合法特性子集；否则，当驱动写 `FEATURES_OK` 时，设备**必须**让该位设置失败。
- 如果设备至少成功协商过一次某组特性（在设备初始化期间接受 `FEATURES_OK`），那么设备或系统重置后，它**不应**让同一组特性的重新协商失败。否则会妨碍从挂起恢复和错误恢复。

#### 2.2.3 Legacy Interface: A Note on Feature Bits（传统接口说明）

- 过渡期驱动**必须**通过检测设备没有提供 `VIRTIO_F_VERSION_1` 特性位来识别传统设备。
- 过渡期设备**必须**通过检测驱动没有确认 `VIRTIO_F_VERSION_1` 来识别传统驱动。
- 这种情况下，设备通过传统接口使用。
- 传统接口支持是**可选的**。因此，过渡期和非过渡期设备/驱动都符合本规范。
- 与过渡期设备/驱动相关的要求，放在名为 “Legacy Interface” 的章节中，比如本节。
- 当设备通过传统接口使用时，过渡期设备和过渡期驱动**必须**按照这些传统接口章节中记录的要求操作。这些章节中的规范文本通常不适用于非过渡期设备。

### 2.3 Notifications（通知）

发送通知（驱动到设备，或设备到驱动）在本规范中很重要。通知的具体工作方式与传输层相关。

有三种通知：

- **configuration change notification（配置变化通知）**
- **available buffer notification（可用缓冲区通知）**
- **used buffer notification（已用缓冲区通知）**

配置变化通知和 used buffer notification 由设备发送，接收者是驱动：

- 配置变化通知表示设备配置空间已经改变；
- used buffer notification 表示通知指定的 virtqueue 上可能有一个 buffer 已被使用。

Available buffer notification 由驱动发送，接收者是设备，表示通知指定的 virtqueue 上可能有一个 buffer 已变为可用。

这些通知的语义、传输层相关实现以及其他重要方面，在后续章节中详细规定。

大多数传输层使用中断来实现设备到驱动的通知。因此在本规范早期版本中，这些通知常被称为中断。本规范中定义的一些名称仍保留 “interrupt” 术语。偶尔也用 “event” 表示一次通知或收到一次通知。

### 2.4 Device Reset（设备重置）

驱动可能希望在多个时间点发起设备重置；特别是在设备初始化和设备清理期间，驱动需要这样做。

驱动发起重置的机制与传输层相关。

#### 2.4.1 Device Requirements: Device Reset（设备要求）

- 设备收到重置后，**必须**把设备状态重新初始化为 0。
- 设备通过把设备状态重新初始化为 0 来表示重置完成后，**不得**发送通知或与队列交互，直到驱动重新初始化设备。

#### 2.4.2 Driver Requirements: Device Reset（驱动要求）

驱动读取到设备状态为 0 时，**应该**认为驱动发起的重置已经完成。

### 2.5 Device Configuration Space（设备配置空间）

设备配置空间通常用于很少变化或只在初始化时使用的参数。如果配置字段是可选的，它的存在由特性位表示。本规范的未来版本可能会在尾部添加额外字段来扩展设备配置空间。

> 注意：设备配置空间的多字节字段使用小端序格式。

每个传输层还为设备配置空间提供一个**代数计数（generation count）**。只要两次访问设备配置空间之间可能出现不同版本的配置，该计数就会变化。

#### 2.5.1 Driver Requirements: Device Configuration Space（驱动要求）

- 驱动**不得**假设对大于 32 位的字段的读取是原子的，也**不得**假设多个字段的读取是原子的。
- 驱动**应该**这样读取设备配置空间字段：

```c
u32 before, after;
do {
    before = get_config_generation(device);
    // 读取配置字段
    after = get_config_generation(device);
} while (after != before);
```

- 对于可选配置空间字段，驱动在访问该部分配置空间之前，**必须**检查对应特性是否已提供。
- 驱动**不得**限制结构大小和设备配置空间大小。相反，驱动**应该**只检查设备配置空间是否足够大，能包含设备操作所需的字段。
- 注意：例如，如果规范说设备配置空间“包含一个 8 位字段”，驱动应理解设备配置空间也可能包含任意数量的尾部填充，并接受任何大于或等于该 8 位字段大小的设备配置空间。

#### 2.5.2 Device Requirements: Device Configuration Space（设备要求）

设备**必须**允许驱动在设置 `FEATURES_OK` 之前读取任何设备特定配置字段。这包括依赖特性位的字段，只要这些特性位由设备提供。

#### 2.5.3 Legacy Interface: A Note on Device Configuration Space endian-ness（传统接口字节序）

注意，对于传统接口，设备配置空间通常使用 guest 的本机字节序，而不是 PCI 的小端序。正确的字节序按设备类型记录。

#### 2.5.4 Legacy Interface: Device Configuration Space（传统接口）

传统设备没有配置代数（configuration generation）字段，因此如果配置被更新，容易发生竞态条件。这会影响块设备容量（见 5.2.4 节）和网络 MAC 地址（见 5.1.4 节）字段。使用传统接口时，驱动**应该**多次读取这些字段，直到两次读取得到一致结果。

---

## 与后续章节的关系

- 2.6 / 2.7 节（Virtqueue 与 Split Virtqueue）见 [`VIRTIO_1_2_CN_SPLIT_VIRTQUEUE.md`](VIRTIO_1_2_CN_SPLIT_VIRTQUEUE.md)。
- 3.1–3.3、4.2、5.2 节见 [`VIRTIO_1_2_CN_MMIO_BLOCK.md`](VIRTIO_1_2_CN_MMIO_BLOCK.md)。
