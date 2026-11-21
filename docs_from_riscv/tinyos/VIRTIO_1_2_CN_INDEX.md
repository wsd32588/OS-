# VirtIO 1.2 中文翻译总目录

> 原文：*Virtual I/O Device (VIRTIO) Version 1.2*，OASIS，2022-07-01，共 282 页。
>
> 本目录跟踪中文翻译进度。译文用于学习和代码对照，不是 OASIS 官方译本；规范性内容以英文原文为准。

## 翻译文件

| 文件 | 对应章节 | 状态 |
|---|---|---|
| [`VIRTIO_1_2_CN_BASIC_FACILITIES.md`](VIRTIO_1_2_CN_BASIC_FACILITIES.md) | 2.1–2.5 设备状态、特性位、通知、重置、配置空间 | 已完成 |
| [`VIRTIO_1_2_CN_SPLIT_VIRTQUEUE.md`](VIRTIO_1_2_CN_SPLIT_VIRTQUEUE.md) | 2.6–2.7 Virtqueue、Split Virtqueue | 已完成 |
| [`VIRTIO_1_2_CN_MMIO_BLOCK.md`](VIRTIO_1_2_CN_MMIO_BLOCK.md) | 3.1–3.3 设备初始化/操作/清理；4.2 MMIO；5.2 块设备 | 已完成 |
| [`VIRTIO_1_2_CN_INTRO.md`](VIRTIO_1_2_CN_INTRO.md) | 1 引言、术语、规范结构 | 已完成 |
| [`VIRTIO_1_2_CN_PACKED_AND_MISC.md`](VIRTIO_1_2_CN_PACKED_AND_MISC.md) | 2.8 Packed Virtqueues、2.9 Driver Notifications、2.10 Shared Memory Regions、2.11 Exporting Objects | 已完成 |
| `VIRTIO_1_2_CN_PCI.md` | 4.1 Virtio Over PCI Bus | 待翻译 |
| `VIRTIO_1_2_CN_CHANNEL_IO.md` | 4.3 Virtio Over Channel I/O | 待翻译 |
| `VIRTIO_1_2_CN_NETWORK.md` | 5.1 Network Device | 待翻译 |
| `VIRTIO_1_2_CN_OTHER_DEVICES.md` | 5.3 以后：Console、Entropy、Balloon、SCSI、GPU、Input 等 | 待翻译 |
| `VIRTIO_1_2_CN_APPENDIX.md` | 附录、参考文献、错误码/预留值 | 待翻译 |

## 主要章节进度

| 原文章节 | 内容 | 状态 |
|---|---|---|
| 1 | Introduction / Terminology / Structure Specifications | 已完成 |
| 2.1 | Device Status Field | 已完成 |
| 2.2 | Feature Bits | 已完成 |
| 2.3 | Notifications | 已完成 |
| 2.4 | Device Reset | 已完成 |
| 2.5 | Device Configuration Space | 已完成 |
| 2.6 | Virtqueues | 已完成 |
| 2.7 | Split Virtqueues | 已完成 |
| 2.8 | Packed Virtqueues | 已完成 |
| 2.9 | Driver Notifications | 已完成 |
| 2.10 | Shared Memory Regions | 已完成 |
| 2.11 | Exporting Objects | 已完成 |
| 3.1 | Device Initialization | 已完成 |
| 3.2 | Device Operation | 已完成 |
| 3.3 | Device Cleanup | 已完成 |
| 4.1 | Virtio Over PCI Bus | 待翻译 |
| 4.2 | Virtio Over MMIO | 已完成 |
| 4.3 | Virtio Over Channel I/O | 待翻译 |
| 5.1 | Network Device | 待翻译 |
| 5.2 | Block Device | 已完成 |
| 5.3 | Console Device | 待翻译 |
| 5.4 | Entropy Device | 待翻译 |
| 5.5 | Traditional Memory Balloon Device | 待翻译 |
| 5.6 | SCSI Host Device | 待翻译 |
| 5.7 | GPU Device | 待翻译 |
| 5.8 | Input Device | 待翻译 |
| 5.9+ | 其他设备类型 | 待翻译 |
| 附录 | 参考文献、错误码、预留值等 | 待翻译 |

## 当前覆盖

- 第 1 章：已完成
- 第 2 章（2.1–2.11）：已完成
- 第 3.1–3.3 节：已完成
- 第 4.2 节：已完成
- 第 5.2 节：已完成
- 第 4.1、4.3、5.1、5.3 及以后：待翻译

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
| available ring | 可用环 |
| used ring | 已用环 |
| device-readable | 设备可读 |
| device-writable | 设备可写 |
| guest physical address | 客户机物理地址（guest PA） |

## 说明

- 全文 282 页，按主题分批翻译，避免单文件过长。
- 每个文件末尾会标注与 TinyOS 代码的对应关系（如果该章与当前实现相关）。
- 原文中的 Legacy Interface 章节会保留，但会明确标注“传统接口”，避免和现代 MMIO/PCI 路径混淆。
