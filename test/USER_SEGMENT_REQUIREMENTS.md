# 用户程序装载：段复制与清零

状态：2026-10-07 本批验收完成。kernel/user_image.c 已修正空指针加法，参数校验、初值复制和零尾部正确；S1-S10 的 16 个表驱动场景及原宿主测试全部通过，RV64 内核编译链接成功且无新增警告，模块无未定义符号。构建/C ABI/测试入口均接好，已清除一处行尾空格。本批只验收纯算法，尚未接入用户数据页或 QEMU 运行路径，尚未提交。

## 为什么做这一步

用户程序接下来要能使用这样的变量：

```c
static int counter = 7;  /* 有初值，通常进入 .data。 */
static unsigned char buffer[32];  /* 零初值，通常进入 .bss。 */
```

有初值的内容需要从镜像复制到运行时内存；零初值区域需要清零。BSS 占运行时内存，通常不在文件中保存对应数量的零字节。这是程序装载区别于直接复制一块代码的一个关键点。

本批先做这个操作，后续再给用户程序分配独立 U/R/W 数据页并接入它。目前 user/user.ld 仍拒绝可写全局，正常 C 演示仍保持原状。完成本批算法不等于已经支持用户全局变量。

## 范围与文件

作者新增：

- include/user_image.h：接口和返回码。
- kernel/user_image.c：段复制与清零，实现不依赖 PMM、页表或 CSR。
- test/test_user_image.hpp：声明 int test_user_image(void)。
- test/test_user_image.cpp：下面整批场景。

作者接入口：

- 根 Makefile 的 OBJS 加 kernel/user_image.o。
- test/CMakeLists.txt 的 tinyos_test_kernel 源码加 kernel/user_image.c。
- test/c_api.hpp 的 extern "C" 内包含 user_image.h。
- test/test_main.cpp 包含测试头，并在最终 PASS 前调用 test_user_image，非零返回 EXIT_FAILURE。

C++ 测试文件由现有 GLOB 自动发现，但被测 C 文件和测试调用入口仍需手动添加。

本批不改 UserMemory、prepare_demo_user_memory、调度器、用户链接脚本和三个 QEMU 脚本，也不解析 ELF。原 QEMU 流程不会调用这个新函数，所以本批正确性主要由宿主测试证明。

## 接口

include/user_image.h 骨架：

```c
#ifndef TINYOS_USER_IMAGE_H
#define TINYOS_USER_IMAGE_H

#include <stddef.h>

enum {
    USER_IMAGE_OK = 0,
    USER_IMAGE_INVALID_ARGUMENT = -1
};

int user_image_load_segment(
    void* destination,
    size_t destination_capacity,
    const void* image,
    size_t image_size,
    size_t file_offset,
    size_t file_size,
    size_t memory_size
);

#endif
```

| 参数 | 含义 |
| --- | --- |
| destination | 已经可访问的目标缓冲区起点 |
| destination_capacity | 目标实际可用字节数 |
| image | 完整镜像缓冲区起点 |
| image_size | 镜像实际可用字节数 |
| file_offset | 本段有初值内容在镜像中的起始偏移 |
| file_size | 需要从文件复制的字节数 |
| memory_size | 本段在内存中总共占用的字节数 |

成功时必须得到：

```text
destination[0 .. file_size)           = image[file_offset .. file_offset + file_size)
destination[file_size .. memory_size) = 0
destination[memory_size .. capacity)  = 保持原值
```

例如 image 为 {0x99, 'A', 0, 0xff, 0x88}，offset=1、file_size=3、memory_size=6：目标前六字节应为 {'A', 0, 0xff, 0, 0, 0}。

## 参数规则：一次校验完，再写目标

以下任意一项成立就返回 USER_IMAGE_INVALID_ARGUMENT，且目标和镜像都必须保持原样：

1. file_size > memory_size。
2. memory_size > destination_capacity。
3. file_offset > image_size。
4. file_size > image_size - file_offset；必须先排除第 3 项，避免减法下溢。
5. memory_size 非零而 destination == NULL。
6. image_size 非零而 image == NULL。

不要用 file_offset + file_size > image_size：加法可能先溢出。

零长度也需要遵守上述元数据规则，不要在入口直接 return 成功。具体边界：

- 全部指针 NULL、全部长度/偏移 0：成功，不读写内存。
- file_size=0、memory_size>0、image=NULL、image_size=0、offset=0：合法纯 BSS，只清零。
- file_size=memory_size=0、offset=image_size：合法空段。
- 即使 memory_size=0，offset>image_size 或 NULL 镜像配非零 image_size 仍是错误。

调用者保证非空缓冲区和所报容量对应真实可访问内存，源与目标不重叠；本批不检查任意指针是否映射，也不实现 memmove。文件偏移和段大小则必须按上面的规则检查。

## 实现骨架

kernel/user_image.c：

```c
#include "user_image.h"

int user_image_load_segment(
    void* destination,
    size_t destination_capacity,
    const void* image,
    size_t image_size,
    size_t file_offset,
    size_t file_size,
    size_t memory_size
) {
    /* TODO: 所有参数检查都在任何目标写入之前。 */

    unsigned char* output = destination;
    const unsigned char* input = image;

    /* TODO: 两个明确的循环，先复制初值，再清零其余区域。 */
    /* file_size 为 0 时不能对 NULL input 做指针运算。 */

    return USER_IMAGE_OK;
}
```

这里处理二进制字节，不是字符串；遇到 NUL 不能停。使用 size_t 循环变量。不要引入 memcpy/memset 的内核库依赖，两个简单循环即可。不要求页对齐，不限制一页；具体用户地址布局由以后调用它的装载器负责。

## 整批测试

只需普通字节缓冲区，无需 PagePool、UserMemoryFixture 或 PMM。当前实现使用 Scenario 表和 std::vector<unsigned char> 统一驱动 16 个场景；下面的 std::array 示例用于说明单例断言。目标先填 0xcc，避免默认零初始化掩盖漏清零。

传 destination.data() + 1 时，将 capacity 设为数组长度减 2，留前后哨兵。成功检查整个目标，包括段末之后的哨兵；每次保存 image 原值，确认它不变。失败用 expect_no_change 捕获完整目标与镜像。

| 场景 | 参数/观察结果 |
| --- | --- |
| S1 初值与零尾部 | 上面的 offset=1、3/6 字节例子；NUL、高位字节必须正确，段外保持 0xcc |
| S2 全部有初值 | file_size == memory_size，检查无额外写入；使用能恰好填满容量的例子 |
| S3 纯 BSS | NULL 镜像、大小/偏移/file_size 均 0，memory_size>0；整个段清零，哨兵不变 |
| S4 合法空段 | 全 NULL/零；另用非空镜像且 offset==image_size、file_size==memory_size==0；均成功且不改变缓冲区 |
| S5 文件比内存长 | file_size > memory_size；其他参数合法，失败且不写任何字节 |
| S6 目标容量不足 | memory_size > capacity；其他参数合法，失败且不写任何字节 |
| S7 镜像边界 | offset>image_size；offset==image_size 且 file_size=1；offset在内部但文件尾越界；三种独立调用都失败且无写入 |
| S8 极端大小 | 小的真实缓冲区，offset=SIZE_MAX/file_size=2/memory_size=2；另测 file_size==memory_size==SIZE_MAX；均失败，不能读取或写入假冒的巨大区域 |
| S9 NULL 参数 | destination=NULL/memory_size>0；image=NULL/image_size>0；后一种另测 file_size==memory_size==0，仍失败 |
| S10 零长度非法偏移 | memory_size=file_size=0，但 offset>image_size；失败，不能被零长度特判放过 |

S5/S6/S7/S9/S10 的其他参数尽量保持合法，确保每例真的测到指定校验，而不是被另一条规则提前拦下。

S1 示例（其余由作者完成）：

```cpp
void test_initialized_bytes_and_zero_tail() {
    const std::array<unsigned char, 5> image{0x99, 'A', 0, 0xff, 0x88};
    const auto original_image = image;
    std::array<unsigned char, 8> destination;
    destination.fill(0xcc);

    require_equal(user_image_load_segment(
        destination.data() + 1, 6,
        image.data(), image.size(), 1, 3, 6),
        USER_IMAGE_OK, "load initialized bytes and zero tail");

    const std::array<unsigned char, 8> expected{
        0xcc, 'A', 0, 0xff, 0, 0, 0, 0xcc
    };
    require_equal(destination, expected, "segment contents and guards");
    require_equal(image, original_image, "source image preserved");
}
```

在 test/test_user_image.cpp 包含 c_api.hpp、test_support.hpp、array、cstdint 等实际需要的头，使用 tinyos_test 的助手。测试函数放匿名 namespace；外部入口 test_user_image 用 run_case 注册所有场景，失败返回非零。按现有 test_user_access 的入口方式，先执行 run_case 再汇总结果，避免短路跳过后续场景。

## 验收命令和完成条件

```sh
cd /home/asus/tinyos
make test CTEST='ctest --verbose'
make -j4
git diff --check
```

必须满足：

- S1-S10 及原宿主测试全部 PASS，新增组有清楚的 PASS 名称。
- 内核链接新 C 模块成功，没有新增编译警告或 libc 未定义引用。
- 非法参数不写目标；合法复制仅修改 memory_size 字节；源保持原值。
- 源码清楚地区分 file_size 和 memory_size，校验在写入前，范围检查不会溢出。

这个纯算法尚未接到运行路径，本批无需重复完整 QEMU 回归；数据页集成时再验证映射权限、退出回收和故障回滚。

作者整批完成后，协作者一次集中检查功能、边界测试与入口，再修复复验。下一批才将此函数接入用户数据页，并让真正的 counter/buffer 在 U-mode 运行。
