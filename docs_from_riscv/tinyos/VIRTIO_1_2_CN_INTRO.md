# VirtIO 1.2 中文翻译（节选）：引言、术语与规范结构

> 原文：*Virtual I/O Device (VIRTIO) Version 1.2*，OASIS，2022-07-01。
>
> 本文件翻译第 **1 章**：引言、规范性引用、术语、结构规范和常量规范。
>
> 翻译约定：MUST = 必须；MUST NOT = 禁止；SHOULD = 应该；SHOULD NOT = 不应；MAY = 可以。

---

## 1. Introduction（引言）

本文档描述 “virtio” 系列设备的规范。这些设备出现在虚拟环境中，但在设计上对虚拟机里的 guest 来说，它们看起来就像物理设备；本文档也按物理设备的方式来对待它们。这种相似性使 guest 可以使用标准驱动和发现机制。

virtio 和本规范的目标，是让虚拟环境和 guest 拥有一种简单、高效、标准且可扩展的虚拟设备机制，而不是为每个环境或每个 OS 单独定制的机制。

**Straightforward（简单直接）**：  
Virtio 设备使用普通的 bus 机制，即中断和 DMA，任何设备驱动作者都应该熟悉。它没有奇特的 page-flipping 或 COW 机制，它就是普通设备。

> 脚注：不共享页意味着设备实现（例如 hypervisor 或 host）需要对 guest 内存有完整访问权限。与不受信任方通信（例如 guest 之间通信）需要复制。

**Efficient（高效）**：  
Virtio 设备由用于输入和输出的描述符环组成；这些环的布局经过安排，避免驱动和设备写同一 cache line 造成的 cache 效应。

**Standard（标准）**：  
除了支持设备所连接的总线之外，virtio 不对运行环境做任何假设。本规范中，virtio 设备可以实现在 MMIO、Channel I/O 和 PCI 总线传输层之上；早期草案还曾在其他总线上实现过，但本文档不包含那些总线。

**Extensible（可扩展）**：  
Virtio 设备包含特性位（feature bits），guest OS 在设备设置期间确认这些特性位。这允许向前和向后兼容：设备提供它知道的所有特性，驱动确认它理解并希望使用的特性。

> 脚注：Linux 实现进一步把 virtio 传输层代码与具体 virtio 驱动分离；这些驱动在不同传输层之间共享。

### 1.1 Normative References（规范性引用）

本规范规范性引用以下文档：

- **[RFC2119]** Bradner, S., “Keywords for use in RFCs to Indicate Requirement Levels”, BCP 14, RFC 2119, March 1997.
- **[RFC4122]** Leach, P., Mealling, M., and R. Salz, “A Universally Unique IDentifier (UUID) URN Namespace”, RFC 4122, July 2005.
- **[S390 PoP]** *z/Architecture Principles of Operation*, IBM Publication SA22-7832.
- **[S390 Common I/O]** *ESA/390 Common I/O-Device and Self-Description*, IBM Publication SA22-7204.
- **[PCI]** *Conventional PCI Specifications*, PCI-SIG.
- **[PCIe]** *PCI Express Specifications*, PCI-SIG.
- **[IEEE 802]** *IEEE Standard for Local and Metropolitan Area Networks: Overview and Architecture*, IEEE.
- **[SAM]** *SCSI Architectural Model*.
- **[SCSI MMC]** *SCSI Multimedia Commands*.
- **[FUSE]** Linux FUSE interface.
- **[eMMC]** *eMMC Electrical Standard (5.1)*, JESD84-B51.
- **[HDA]** *High Definition Audio Specification*.
- **[I2C]** *I2C-bus specification and user manual*.
- **[SCMI]** *Arm System Control and Management Interface*, DEN0056.

### 1.2 Non-Normative References（非规范性引用）

- **[Virtio PCI Draft]** *Virtio PCI Draft Specification*（早期草案，用于理解传统接口）。

### 1.3 Terminology（术语）

本文档中的关键词 “MUST”、“MUST NOT”、“REQUIRED”、“SHALL”、“SHALL NOT”、“SHOULD”、“SHOULD NOT”、“RECOMMENDED”、“MAY”、“OPTIONAL” 按 [RFC2119] 解释。

#### 1.3.1 Legacy Interface: Terminology（传统接口术语）

本规范 1.0 之前的草案（例如 [Virtio PCI Draft]）定义了驱动与设备之间类似但不同的接口。由于这些接口被广泛部署，本规范提供了**可选**特性，以简化从早期草案接口的过渡。

设备和驱动**可以**支持：

- **Legacy Interface（传统接口）**：由本规范 1.0 之前的早期草案规定的接口；
- **Legacy Device（传统设备）**：在本规范发布之前实现、并在宿主侧实现传统接口的设备；
- **Legacy Driver（传统驱动）**：在本规范发布之前实现、并在 guest 侧实现传统接口的驱动。

传统设备和传统驱动**不符合**本规范。

为了简化从早期草案接口的过渡，设备**可以**实现：

- **Transitional Device（过渡期设备）**：既支持符合本规范的驱动，也允许传统驱动。

类似地，驱动**可以**实现：

- **Transitional Driver（过渡期驱动）**：既支持符合本规范的设备，也支持传统设备。

> 注意：传统接口不是必需的；除非你需要向后兼容，否则不要实现它们！

没有传统兼容性的设备或驱动分别称为 **non-transitional device（非过渡期设备）** 和 **non-transitional driver（非过渡期驱动）**。

#### 1.3.2 Transition from earlier specification drafts（从早期草案过渡）

对于已经实现传统接口的设备和驱动，要支持本规范需要做一些修改。

这种情况下，读者可以重点关注标题中带有 “Legacy Interface” 的章节；这些章节标出了自早期草案以来发生的变化。

### 1.4 Structure Specifications（结构规范）

本文档使用 C 结构体语法描述许多设备和驱动的内存结构布局。所有结构都假定**没有额外填充**。为了强调这一点，如果已知常见 C 编译器会在结构内插入额外填充，则用 GNU `__attribute__((packed))` 语法标注。

结构定义中使用的整数类型约定如下：

| 类型 | 含义 |
|---|---|
| `u8, u16, u32, u64` | 指定位数的无符号整数 |
| `le16, le32, le64` | 指定位数的无符号整数，小端序 |
| `be16, be32, be64` | 指定位数的无符号整数，大端序 |

本规范中有些字段不是从字节边界开始或结束，这些字段称为**位域（bit-fields）**。一组位域总是某个整数类型字段的细分。

整数字段中的位域总是按从最低有效位到最高有效位的顺序列出。位域被视为指定宽度的无符号整数，并保留各位之间的相邻关系。

例如：

```c
struct S {
    be16 {
        A : 15;
        B : 1;
    } x;
    be16 y;
};
```

这表示：

- 值 `A` 存放在 `x` 的低 15 位；
- 值 `B` 存放在 `x` 的高 1 位；
- 16 位整数 `x` 使用大端序，位于结构体 `S` 的起始处；
- 紧接着是使用大端序的无符号整数 `y`，位于距结构体起始处 2 字节（16 位）的偏移处。

> 注意：这种写法看起来有点像 C 位域语法，但不要直接转换成可移植代码中的位域写法：它匹配 C 编译器在小端架构上打包位域的方式，但不匹配 C 编译器在大端架构上的打包方式。

假设 `CPU_TO_BE16` 把 16 位整数从本机 CPU 字节序转换为大端序，那么生成要存入 `x` 的值的可移植 C 代码等价于：

```c
CPU_TO_BE16(B << 15 | A)
```

### 1.5 Constant Specifications（常量规范）

许多情况下，设备与驱动接口中的数值使用 C 的 `#define` 和 `/**/` 注释语法记录。多个相关值用共同前缀分组，并用 `_` 分隔。使用 `_XXX` 作为后缀表示该组中的所有值。

例如：

```c
/* 字段 Fld 取值 A 的描述 */
#define VIRTIO_FLD_A    (1 << 0)
/* 字段 Fld 取值 B 的描述 */
#define VIRTIO_FLD_B    (1 << 1)
```

这表示字段 `Fld` 的两个数值：`Fld` 取值 1 表示 A，取值 2 表示 B。注意 `<<` 表示左移操作。

进一步地，在这个例子中：

- `VIRTIO_FLD_A` 和 `VIRTIO_FLD_B` 分别表示 `Fld` 的值 1 和值 2；
- `VIRTIO_FLD_XXX` 表示 `VIRTIO_FLD_A` 或 `VIRTIO_FLD_B` 中的任意一个。

---

## 相关文件

- 第 2.1–2.5 节：[`VIRTIO_1_2_CN_BASIC_FACILITIES.md`](VIRTIO_1_2_CN_BASIC_FACILITIES.md)
- 第 2.6–2.7 节：[`VIRTIO_1_2_CN_SPLIT_VIRTQUEUE.md`](VIRTIO_1_2_CN_SPLIT_VIRTQUEUE.md)
- 第 2.8–2.11 节：[`VIRTIO_1_2_CN_PACKED_AND_MISC.md`](VIRTIO_1_2_CN_PACKED_AND_MISC.md)
- 第 3.1–3.3、4.2、5.2 节：[`VIRTIO_1_2_CN_MMIO_BLOCK.md`](VIRTIO_1_2_CN_MMIO_BLOCK.md)
