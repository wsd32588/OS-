# TinyOS ELF：从构建、解析到装载、运行与回收

更新：2026-10-26。本轮功能、必要验证与理解题已完成。**复习本轮只需阅读这一份文档**：当前执行路径、概念补充、作者回答与修正集中在下面。旧解析阶段资料折叠在文末；[装载任务与验收记录](USER_ELF_LOADING.md)仅供追溯实现要求，不必来回拼接。

## 1. 当前范围与验证结论

当前支持两条非空 LOAD：代码/只读常量一页、数据/BSS一页，另由内核提供一页用户栈。用户与内核共享根页表，还没有每任务独立地址空间、多页通用装载或文件系统读取。

- 正常路径完成 yield → R → sleep → X → Hello from user! → Data OK → exit；退出后内核任务与时钟仍运行。
- 无效入口落入数据 VA 时，在分配/映射前拒绝，页数、输出描述、satp与原映射保持不变。
- 纯BSS场景已通过零初值、睡眠后内容保持和退出五页回收检查。不需要另建 `user/bss_probe.c`。
- P2改变链接文件布局的实验暂缓，不阻塞收尾。上述运行证据沿用本轮已完成验证，本次整理没有重新构建或运行。

## 2. 一条完整执行路径

### 构建与嵌入

```text
user源码 → .o → 链接生成demo.elf → objcopy另外生成demo.bin
                         ↓
kernel/user/user_entry.S通过.incbin嵌入demo.elf
                         ↓
内核被加载时，整份ELF文件字节也进入内存
```

[Makefile](../Makefile)中的链接和objcopy规则是构建步骤；实际嵌入发生在[user_entry.S](../kernel/user/user_entry.S)。当前准备函数使用 `user_elf_start` 作为源，不再从BIN固定偏移推断段。ELF进入内核内存，不代表它的段已经放进用户页。

### 启动与用户内存准备

[kernel_main](../kernel/boot/main.c)的顺序是：

```text
解析ELF，得到UserElfLayout
    → 初始化PMM
    → 建立内核页表并启用Sv39
    → prepare_demo_user_memory检查当前支持的布局
    → PMM分配代码、栈、数据物理页
    → 根据每段offset/filesz/memsz复制并补零
    → fence.i
    → 建立用户VA到PA映射，执行sfence.vma
    → 成功提交UserMemory
    → 创建任务、构造初始现场
    → 启动调度器，恢复现场并通过sret进入U-mode
```

[user_elf_parse](../kernel/user/user_elf.c)负责格式和范围；[prepare_demo_user_memory](../kernel/boot/private/user_prepare.c)负责内核支持的段数、VA、权限、大小和入口策略。先完成选段，再检查两个段指针非NULL，再读取字段校验，最后才分配资源。

分配用户页时PMM已经初始化，MMU也已经启用。准备函数修改正在使用的共享页表，增加用户映射；不是在复制完后才启用MMU。

### 文件位置、用户VA与PA

| 角色 | 决定方式 |
| --- | --- |
| 复制源 | 内核中保存的ELF起点 + 该段p_offset |
| 复制目标 | PMM实际分配的物理页；内核通过当前恒等映射访问 |
| 用户访问VA | ELF规定的段VA与本批固定栈布局 |
| 用户访问实际到达的PA | 当前根页表 + VA，经三级页表翻译得到 |

用户VA是翻译的输入，PA是结果；不能反过来说用户VA由查得的PA决定。改变文件偏移或分配到另一张物理页，都不必改变用户VA。

| 用户VA区间 | 用途 | 权限 |
| --- | --- | --- |
| [0x40000000, 0x40001000) | 代码与只读常量 | U/R/X |
| [0x40001000, 0x40002000) | 用户栈 | U/R/W |
| [0x40002000, 0x40003000) | 留空 | 未映射 |
| [0x40003000, 0x40004000) | 数据与BSS | U/R/W |

这里固定的是用户VA布局。实际PA来自PMM，不是按上述数字直接分配；根页表目前由内核与用户共享。

### 复制、栈与现场

[user_image_load_segment](../kernel/user/user_image.c)对每段从 `image + file_offset` 复制 `file_size` 字节，再将段内 `[file_size, memory_size)` 清零。它不分配内存。当前即使memsz只有64字节，承载它的分配单位仍为整张4096字节物理页。

用户栈不从ELF复制：PMM分配普通物理页作为栈存储，映射到用户栈VA区间，再把栈顶VA `0x40002000` 写入初始TrapFrame.sp。这张栈页不是根页表，sp也不填栈的PA。

`fence.i`处理写入代码后的取指可见性；`sfence.vma`处理页表变化后的地址翻译，二者不能统称一次Cache刷新。

### 归属、执行与退出

准备成功只表示内存资源就绪，还要由 `task_create_user` 构造任务初始现场。`UserMemory`记录借用的root以及拥有的内容页和VA；它不执行地址翻译。任务创建成功后接管描述并清空调用者描述，避免两处都认为自己拥有同一批页。

调度恢复任务后，用户 `_start`调用main；main返回后，[start.S](../user/start.S)调用user_exit，经ecall进入内核。yield、sleep与timer可能在运行过程中调度其他任务；中断使能状态仍需按各执行阶段维护。

正常退出遵循解除用户映射 → sfence.vma → 释放内容页 → 回收空中间页表。本布局回收代码、栈、数据三页，以及共享用户分支的L0/L1两页，共五页。共享根页表、其他内核映射和静态内核栈保留，PMM可在后续分配中复用回收页；部分释放失败的可重试语义继续保留。

## 3. 本轮补充问答

### 为什么魔数指针加0？

```c
const unsigned char *input = image;
const unsigned char *maigc = input + ELF_HEADER_MAGIC_OFFSET;
```

第一行在C中把 `const void *` 隐式转换为字节指针；第二行不负责转换。MAGIC_OFFSET为0，所以第二行等价于 `maigc = input`，去掉 `+ 0` 不会报错。保留名字是为了表达“魔数在ELF头中的偏移”，与其他字段的偏移写法一致。后面的maigc[0..3]才读取字节。maigc是magic的拼写笔误，前后一致不影响执行。

### “另一种链接文件对齐所得p_offset”指哪个文件？

这句话原先表达含糊，并没有另一份现成文件需要寻找。它指在临时副本改变[user/user.ld](../user/user.ld)或相关链接布局，让生成的 `user/build/demo.elf` 中某段文件偏移变化，再检查装载器仍使用新p_offset。

例如代码的文件偏移可能改变，用户VA仍保持0x40000000；具体偏移以生成后的readelf为准，并须满足ELF对齐约束。目的在于证明没有写死文件偏移。这是暂缓的P2实验，不是必要实现或新增文件要求。

## 4. 本轮三题：作者原答、审查与修正版

以下原答保留作者用语；修正版是协作者补充，不算作作者已经独立答出的内容。

### 第一题：文件偏移、复制目标与用户访问地址

题目：数据段p_offset由0x2000变成0x3000，p_vaddr仍为0x40003000，PMM返回另一张物理页。复制源、目标和用户访问地址分别由什么决定？

**作者原答：**

> 复制的源地址是我们给出的地址，目的地址是分配的物理页，是内核为程序决定，用户访问地址根据它的根页表和虚拟地址通过三级页表找到的物理地址决定

**审查：** 目标PA与页表翻译方向基本掌握；源地址需明确加p_offset，用户访问VA不能说成由翻译结果决定。当前也还没有每任务独立root。

**修正版：** 源是ELF内存起点加新p_offset，目标是PMM分配的物理页，用户VA仍为0x40003000。页表把这个VA映射到实际PA；文件位置变化与PA变化不要求VA变化。

### 第二题：纯BSS与用户栈

题目：数据段filesz=0、memsz=64，为什么仍分配？复制和清零多少？用户栈从哪里来？

**作者原答：**

> 因为memsz不是0，这个
>
> ```c
> for (size_t i = 0; i < file_size;++i) {
>     output[i] = input[file_offset + i];
> }
>
> for (size_t i = file_size; i < memory_size; ++i) {
>     output[i] = 0;
> }
> ```
>
> 我们其实只管对memsz指示分配的内存多大。0字节，64字节，先选定内存地址区间，用户栈先pmm_alloc_page出物理根页表，然后map之后开始绑定TrapFrame的sp位置

**审查：** 复制0字节、清零64字节正确；循环不负责分配，memsz不是实际按页申请的大小。栈物理页与根页表混淆。

**修正版：** memsz非零说明运行时需要存储，当前为它申请一整页，复制0字节，段补零循环清零64字节。PMM还会清零整张新分配页，但这与段复制函数的职责不同。用户栈另外分配普通物理页并映射，初始sp保存栈顶VA，不保存PA，也没有为栈另分配根页表。

### 第三题：拒绝、归属与回收

题目：为什么无效入口在分配前拒绝？成功后为什么交给任务管理？退出回收五页时为什么保留根页表？

**作者原答：**

> 无效入口就代表没办法正确分配使用，装载完成后就是一个完整的任务了，需要开始工作，而每个任务不是自己能完全调节行动的，所以任务管理接管了任务的运行秩序，回收页的时候，还需要选sfence.vma刷新TLB

**审查：** 刷新与运行调度的认识正确；无效入口不等于无法分配，装载资源也不等于已创建完整任务。还需说明所有权与共享root。

**修正版：** 即使入口错误，分配仍可能成功；提前拒绝是为了不改变页数、输出描述和映射。准备成功后，创建任务才构造初始现场并接管UserMemory，明确由谁在退出时释放，避免重复释放。正常解除映射后先sfence.vma，再归还物理页；只回收属于用户的资源与空分支，内核和其他任务仍使用的共享根页表必须保留。

## 5. 后续阅读方式

本轮ELF解析、装载与理解题在此收尾；下一批由作者选择具体TinyOS功能。本次没有启动C++迁移或增加新的测试要求。旧阶段的解析字段、接口骨架和三题修正版保留在下面，属于历史材料，其“仍走BIN”“下一批”等状态不代表当前实现。

<details>
<summary>历史资料：2026-10-11 ELF解析阶段的任务与答题</summary>

# 下一批：读取 ELF，理解程序怎样描述自己的布局

状态：2026-10-11 本批可见功能完成。解析器与完整启动打印已实现，make 无警告通过；QEMU 入口、程序头总数及两条 LOAD 的六字段与 readelf 全部一致，原 Hello/Data OK/EXIT 继续运行。临时宿主核对真实镜像布局及坏 magic 返回 -1/输出不变通过，未增加持久测试框架。当前实际装载仍沿用 bin；下一批任务在 USER_ELF_LOADING.md。本批收尾为整体梳理和三个理解小测。

## 目标与完成点

让内核读取实际 `user/build/demo.elf`，在启动时打印入口及每个 `PT_LOAD` 的文件偏移、用户 VA、文件大小、内存大小、权限和对齐。打印与同一次构建产物的 `readelf` 一致，原用户演示继续运行，即完成本批。

本批实现一个纯 C ELF 读取模块。现有 flat bin 装载继续工作；下一批再把解析结果接到 `user_image_load_segment` 和任务入口。

## 为什么接着做这个

当前构建是：用户 C/汇编 → demo.elf → objcopy → demo.bin → incbin → 分配三页 → 复制/补零 → 映射 → U-mode。

内核目前从 bin 的长度推断是否有数据，并用固定 0x3000 切分。ELF 自己保存了入口和段描述，读取它是后续按文件描述装载程序的基础。

ELF header 给出入口、程序头表的位置和数量；程序头里的 PT_LOAD 描述可加载段。同一个段可以包含多个 section，装载时可以沿程序头找到内容，无需按 `.text`、`.data` 等名字遍历节表。[ELF header](https://gabi.xinuos.com/elf/02-eheader.html)、[Program header](https://gabi.xinuos.com/elf/07-pheader.html)。

2026-10-10 只读查看已有磁盘产物得到：入口 0x40000000；程序头共 3 项，两个 LOAD，另一个为 RISCV_ATTRIBUTES。

| LOAD | ELF 文件偏移 | 用户 VA | 文件字节数 | 内存字节数 | flags |
| --- | --- | --- | --- | --- | --- |
| 代码/只读常量 | 0x1000 | 0x40000000 | 0x27a | 0x27a | 5（RX） |
| 数据/BSS | 0x2000 | 0x40003000 | 0x11 | 0x38 | 6（RW） |

这是已有产物的观察值；源码改变和重新构建后，以实际 readelf 输出为准，解析器中不要写死这些偏移和长度。

## 作者实现：一个解析函数

新建 `include/user_elf.h`，使用 include guard，接口如下：

```c
#include <stddef.h>
#include <stdint.h>

#define USER_ELF_MAX_LOAD_SEGMENTS 2U

enum {
    USER_ELF_OK = 0,
    USER_ELF_INVALID_ARGUMENT = -1
};

typedef struct {
    uint64_t file_offset;
    uint64_t virtual_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
    uint32_t flags;
} UserElfSegment;

typedef struct {
    uint64_t entry_address;
    uint16_t program_header_count;
    size_t load_segment_count;
    UserElfSegment segments[USER_ELF_MAX_LOAD_SEGMENTS];
} UserElfLayout;

int user_elf_parse(const void *image, size_t image_size,
                   UserElfLayout *layout_out);
```

两条 LOAD 是本项目这一阶段的容量限制，ELF 格式允许更多。超出容量返回错误。遍历所有程序头，跳过非 LOAD；至少找到一个 LOAD 才返回成功。

在 `kernel/user_elf.c` 中实现；只包含自己的头文件和所需标准整数定义。所有 helper 用 static，解析器只读输入并生成布局，打印由 main.c 负责。

### 读整数的关键骨架

按字节读小端整数，可以直接处理任意对齐的输入：

```c
/* 调用前已确认这一字段的全部字节都在镜像范围内。
   width 在本模块内固定取 2、4 或 8。 */
static uint64_t read_little_endian(const unsigned char *bytes,
                                 size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i) {
        value |= (uint64_t)bytes[i] << (8U * i);
    }
    return value;
}
```

先检查镜像和输出指针非空、镜像至少 64 字节，再访问 header。

ELF64 header 中本批需要的字段位置如下；这是格式规定的字节偏移：

| 字段 | 字节偏移 | 宽度 | 本批用途 |
| --- | --- | --- | --- |
| magic | 0 | 4 | 0x7f、'E'、'L'、'F' |
| EI_CLASS | 4 | 1 | 等于 2：ELF64 |
| EI_DATA | 5 | 1 | 等于 1：小端 |
| EI_VERSION | 6 | 1 | 等于 1 |
| e_type | 16 | 2 | 等于 2：ET_EXEC |
| e_machine | 18 | 2 | 等于 243：RISC-V |
| e_version | 20 | 4 | 等于 1 |
| e_entry | 24 | 8 | 保存入口 VA |
| e_phoff | 32 | 8 | 程序头表的文件偏移 |
| e_ehsize | 52 | 2 | 等于 64 |
| e_phentsize | 54 | 2 | 等于 56 |
| e_phnum | 56 | 2 | 程序头数量；本批拒绝 0 和扩展编号 0xffff |

RISC-V 的 e_machine 值来自 [RISC-V psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/#_file_header)。已有 ELF 的 e_flags 为 1，含 RVC 标志，本批保留其原样，不要求它等于零。

确认表完整存在之后才能遍历：

```c
if (program_header_offset > image_size ||
    program_header_count >
        (image_size - program_header_offset) / 56U) {
    return USER_ELF_INVALID_ARGUMENT;
}
```

第 i 项的位置为 `program_header_offset + i * 56U`。先完成上面的范围检查，然后将偏移转为 size_t 并做指针运算。

每条 ELF64 program header 的字段为：

| 字段 | 项内偏移 | 宽度 |
| --- | --- | --- |
| p_type | 0 | 4 |
| p_flags | 4 | 4 |
| p_offset | 8 | 8 |
| p_vaddr | 16 | 8 |
| p_paddr | 24 | 8 |
| p_filesz | 32 | 8 |
| p_memsz | 40 | 8 |
| p_align | 48 | 8 |

`p_type == 1` 才是 LOAD。对每个 LOAD 检查：

- `file_size <= memory_size`。
- `file_offset <= image_size` 且 `file_size <= image_size - file_offset`。
- `memory_size <= UINT64_MAX - virtual_address`，防止地址范围溢出。
- alignment 为 0/1，或者是二次幂；大于 1 时要求 `virtual_address % alignment == file_offset % alignment`。
- 收集数量没有超出上述两条容量。

用 `UserElfLayout layout = {0};` 保存局部结果，全部成功后再写 `*layout_out = layout;`。错误返回时输出不变。本批结果是读取到的元数据，实际装载时还会检查地址区域、入口和支持的权限策略。

## 接线：由协作者在解析函数完成后补齐

接线集中在三处，作者可以先只实现上述两个文件：

1. `kernel/user_entry.S` 在已有 `user_image_end` 后另加一份 `.balign 8` 的 ELF 嵌入，用 `user_elf_start/user_elf_end` 独立标签和 `.incbin "user/build/demo.elf"`。两组 start/end 各自只包围对应文件内容。
2. Makefile 的 OBJS 增加 `kernel/user_elf.o`，嵌入对象明确依赖 demo.elf 和原 demo.bin。
3. main.c 在准备用户物理页前调用解析器，通过现有 uart_puts/uart_put_hex 打印 entry、程序头数、LOAD 数，以及各段字段。解析失败打印错误后停止启动；成功后继续原有用户准备和运行。

解析函数完成后发给协作者集中审查，再补这三处接线，作者无需重新写一套测试框架。

## 只保留一个可见运行目标

接线完成后：

```bash
make
riscv64-linux-gnu-readelf -h -lW user/build/demo.elf
make qemu
```

内核打印的段布局与 readelf 的 LOAD 行对上；原演示仍能输出 Hello、Data OK 并 EXIT。手动退出 QEMU：Ctrl+A，再按 X。必要回归由协作者执行。

## 完成后的整体梳理与三个小测

梳理“源代码 → 链接脚本 → ELF header/程序头 → 文件内容 → 物理页 → 用户虚拟地址 → 入口”的关系；标出当前仍由 bin 完成的装载步骤，以及下一批将复用的段复制/清零函数。

1. 数据段的 ELF 文件偏移是 0x2000，代码/数据 VA 间距是 0x3000，为什么它们可以不同？

```
Answer 1:
    p_offset 描述段内容在 ELF 文件中的位置，p_vaddr 描述段在用户虚拟地址空间中的位置，两者由链接器分别安排。
    当前数据的文件内容从 0x2000 开始，运行时却位于 VA 0x40003000；装载器读取文件内容，复制到 PMM 分配的物理页，再建立对应 VA 到该物理页的映射。
    所以文件不需要为代码与数据之间的每个虚拟地址保留字节。0x2000 不是物理地址，也不是为了抵消页表开销而决定的；文件偏移与 VA 仍须满足 ELF 的对齐约束。
    当前代码、栈和数据映射共用同一条 L1/L0 页表路径，不是每个内容页各分配两张页表。代码与数据起始 VA 相差 0x3000，中间是栈页 0x40001000 和未映射页 0x40002000。
    栈顶固定为 0x40002000，它是栈页上边界，不是整个虚拟地址空间的顶层。
```

2. 一个 LOAD 的 file_size=17、memory_size=56，装载时复制多少、清零多少？p_paddr 能直接作为 PMM 分配结果使用吗？

```
Answer 2:
    从文件 p_offset 处复制 17 字节，再把段内偏移 [17, 56) 的 39 字节清零。
    当前实现先写入 PMM 分配的物理页，再建立 p_vaddr 到该页的映射；用户看到的零区域是 [p_vaddr + 17, p_vaddr + 56)。
    这段尾部零区域满足 BSS 的零初始化要求，也可能包含对齐填充，不必逐个寻找变量或节名。
    p_paddr 不能直接作为我们内核的 PMM 分配结果。实际 PA 来自 pmm_alloc_page，资源归属由 UserMemory 记录。
```

3. 程序头共三项，为什么只输出两个 LOAD？寻找 `.bss` 的节名是不是这次读取段布局的必要步骤？

```
Answer 3:
    当前三个程序头分别是 RISCV_ATTRIBUTES、代码 LOAD、数据 LOAD。解析器只收集 PT_LOAD，所以输出两项；架构属性项不属于这次装载的内容段。
    .bss 是节名，当前与 .data 同属数据 LOAD，并不是第三条 LOAD。
    装载器根据 LOAD 的 p_offset、p_filesz、p_memsz 复制文件字节并补零，无需查询 .bss 的节名。
    BSS 不在文件中保存变量的零字节初值，但需要运行时内存。当前 buffer[32] 就使用这块空间，不能因为文件没有对应初值而丢弃它。
    若程序没有这类静态数据，BSS 可以为空；是否需要 BSS 内存与装载器是否需要寻找节名是两个问题。
```

下一批再把布局接入实际装载：复制位置/大小取自段记录，任务入口取自 ELF header。本批到上述可见结果和理解梳理即可收尾。

</details>
