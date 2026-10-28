# ELF实际装载：已完成的任务与验收记录

状态：2026-10-26，rCore第四章本轮收尾后已回到本批。前一批ELF读取与布局打印曾运行通过，
作者已修正选段/校验顺序，正常ELF装载及必要拒绝验证通过；纯BSS选做也已复验，P2按作者安排暂缓。
协作者一次timeout 8s make qemu观察到yield/R/sleep/X/Hello/Data OK/exit，退出后S timer继续，未代改实现。
日志与检查在`.cache/elf-normal-review-foikiwre/`；124是常驻内核task_a运行至限时结束，不是用户失败。
随后在唯一临时拒绝副本中，将layout副本entry改成数据VA，返回INVALID_ARGUMENT，
实际分配/映射调用均0，页数、satp、输出七字段、三用户映射及root分支保持不变；之后正常程序仍完成。
证据`.cache/elf-reject-review-4xmqitrw/`，未改真实源码或正式脚本。
现有`python3 -u test/verify_user_data.py pure_bss`单例已PASS，证据`.cache/elf-pure-bss-review-dy6rn1gc/`。
该脚本在临时副本提供纯BSS用户程序，并检查初值/睡眠后保持及退出五页回收；不需另建user/bss_probe.c。
如果希望将样例独立保存为该文件，需要另外明确测试构建入口，当前Makefile不会自动链接它。
本批功能、必要验证、功能梳理与三题审查均已完成；P2不阻塞收尾。
**统一复习入口：[ELF构建、解析、装载与答题总结](USER_ELF_LEARNING.md)。**
以下保留实现要求与历史审查，不必与总结反复对照阅读。
本批继续复用三页分配、映射及回收，实际位置为`kernel/boot/private/user_prepare.c`与`boot_stages.c`。

## 恢复时先处理的完整功能

由作者主导完成装载流程和一条有学习价值的测试，协作者帮助解释、审查与例行复验，
不新建框架或把实现拆成预先铺好的几个TODO。本批继续现有语言与固定三页布局，不同时迁移C++或新增独立页表。

2026-10-26作者修改后的集中审查：

最新修正后，入口判断、映射前查询与数据段长度问题已修；但只将非NULL检查移到了for后，
完整字段/入口校验仍在循环内。第一段之后data_segment为NULL，读取其memory_size触发地址0x18的读缺页。
现有ELF定位sepc=0x8020038e为user_prepare.c:57的`ld a5,24(a5)`。
随后作者已将整个校验移到遍历与非NULL检查之后，正常路径通过；以上NULL问题已解决。
正确顺序是完成整个选段循环，再检查两个指针均非NULL，最后执行完整字段与入口校验。下面保留此前集中审查记录。

- 五参数接口、按VA选段和ELF字段复制已写入；但“两段都找到”检查仍在for内部，第一段后另一指针必NULL，这是当前首个返回点。
- 两段的完整字段/入口校验应放在选段遍历结束之后。现入口条件把合法范围当作错误，还需拒绝非两字节对齐入口。
- 分配前仍要求用户VA已经能查询成功，应移除该旧检查；此时正准备建立用户映射。
- 数据复制最后一个参数仍误用code_segment->memory_size，必须取data_segment自己的memory_size。
- boot_start_user已传user_elf_start及layout->entry_address，保持现有接线，不重复改回旧BIN或固定入口。
- prepare_user_image已在此前目录整理中删除，不再重复安排删除或搬文件。

正常功能恢复后，作者可先写一条“入口落在数据段”的拒绝测试：必须在分配/映射前拒绝，
可用页数、原输出描述和用户映射不变。这是一条完整学习测试，不扩成故障矩阵。
协作者继续保证正常路径、内容/BSS/权限与必要拒绝验证可运行；不要求作者自行搭建全套测试框架。

旧测试入口需要区分覆盖范围：make test尚未接入user_elf.c，只覆盖已有复制助手等模块；
verify_prepare_rollback.py仍用旧两参数签名/两内容页假设，verify_task_creation_rollback.py仍预期旧四页，
部分WRITE探针缺ELF标签且仍寻找旧main.c锚点。它们的适配失败不能直接算作新装载逻辑失败。
verify_user_data.py的baseline最接近现有三页正常路径，可在实现后复用；历史矩阵适配仍按需维护。
现有user_memory_release的部分成功后可重试语义应保留，不能失败时直接抹掉尚未归还的拥有者。

## 目标与完成点

让正常用户程序从嵌入的 demo.elf 装载。代码与数据的文件偏移、复制长度、补零长度来自 UserElfLayout；用户任务入口来自 layout.entry_address。

完成时仍看到 yield/R/sleep/X/Hello/Data OK/EXIT，原全局变量初值、BSS 零值和 sleep 后状态保持检查继续成功。
作者主导实现和选定的学习测试，协作者负责例行构建、实际运行及必要复验。

本批支持现有链接脚本生成的静态演示：代码与只读常量一页、数据/BSS 一页、独立栈一页，共享 root。入口由 ELF 读取，允许落在代码段文件内容中的其他位置。本批地址空间仍使用代码 0x40000000、数据 0x40003000、栈 0x40001000 与栈顶 0x40002000。

## 先理解各部分的职责

user_elf_parse 读取并校验文件格式，生成布局。

prepare_demo_user_memory 根据当前内核支持的地址/权限策略检查布局，再分配物理页，复制文件内容、补零、映射和刷新。

task_create_user 用入口和栈顶建立初始任务上下文，接收 UserMemory 的资源归属。调度、退出和资源回收继续复用。

同一 ELF 数据段可以位于文件偏移 0x2000，却要求映射到 VA 0x40003000。内核将文件字节复制到自己分配的 PA，再建立 VA → PA 映射，不能把文件位置或 p_paddr 当成 PMM 的分配结果。

## 本批实现顺序

作者集中完成下面四步后交给协作者检查；中途不用逐项交作业。

1. 保留已有五参数接口，完成首次分配前的布局校验，并选出代码/数据段。
2. 用两段记录替换旧 bin 切割与复制，保留现有分配、映射、刷新和失败清理。
3. 修改boot_stages.c的boot_start_user中准备函数和task_create_user调用，传入同一ELF镜像/布局及ELF入口。
4. 保留现有失败清理，编译并完成选定的学习测试；将代码和结果一起交给协作者集中审查、运行及梳理。

下面各节给齐这四步的要求和关键骨架。本批不新增调度、回收或测试框架模块。

## 修改准备函数的参数

当前私有模块已有以下入口，不需要重复修改参数：

```c
int prepare_demo_user_memory(
    Sv39PageTable *root,
    const void *image,
    size_t image_size,
    const UserElfLayout *layout,
    UserMemory *memory_out
);
```

调用者传入刚刚成功解析的同一 ELF 镜像及布局。先检查指针，再检查布局，全部通过后才开始 pmm_alloc_page。

布局检查集中做一次：
- load_segment_count 恰好为 2；代码和数据的 memory_size 都在 1..4096 范围内。
- 按 virtual_address 找到代码段和数据段，各恰好一条；不依赖它们在数组中的位置。
- 代码 VA 等于 DEMO_USER_CODE_ADDRESS，flags 等于 5（RX）；数据 VA 等于 DEMO_USER_DATA_ADDRESS，flags 等于 6（RW）。其余地址、重复段或权限不符合则拒绝。
- 两段 file_size <= memory_size；file_offset <= image_size，且 file_size <= image_size - file_offset。纯 BSS 的 file_size 可以为零。
- entry_address 位于代码段文件内容范围内：先检查 entry >= code VA，再检查 entry - code VA < code file_size；入口满足两字节对齐。

这是当前三页演示的装载策略，ELF 文件格式本身允许更多段和其他布局。解析器已经处理格式、表范围和对齐；装载器继续检查自己支持的映射策略和复制范围。

通过检查后，可以用两个指针保存选中的段：

```c
const UserElfSegment *code_segment = NULL;
const UserElfSegment *data_segment = NULL;
```

遍历布局选段和校验由作者完成；任意拒绝都返回 SV39_ERR_INVALID_ARGUMENT，输出描述不变，尚未分配或修改页表。

## 替换原来的文件切割与复制

旧BIN切分块现已由作者替换。继续核对每段分别使用自己的三个字段，保留代码/栈/数据页分配和原来的失败清理。

代码页的装载骨架：

```c
int load_result = user_image_load_segment(
    code_page,
    SV39_PAGE_SIZE,
    image,
    image_size,
    (size_t)code_segment->file_offset,
    (size_t)code_segment->file_size,
    (size_t)code_segment->memory_size
);
```

数据页同样使用 data_segment 的三个大小/偏移字段。检查每次返回值，失败进入已有清理路径。

user_image_load_segment 会复制 file_size 字节并把 memory_size - file_size 的尾部清零。页内其余字节已经由 pmm_alloc_page 初始化为零。栈继续用 PMM 清零页。

之后保留原顺序：两段装载成功 → fence.i → 三页 map → 三处 flush → 提交 UserMemory。ELF flags 与 Sv39 PTE 位编码不同；继续使用现有代码 RX 和数据/栈 RW 的 PTE 常量，U/A/D 的处理也保持原样。

## 两处调用的目标形式（作者已接通）

boot_start_user已经收到elf_size和layout指针，准备调用改为：

```c
prepare_demo_user_memory(
    kernel_root, user_elf_start, elf_size, layout, &user_memory
);
```

继续检查返回值。创建用户任务时：

```c
task_create_user(
    (uintptr_t)layout->entry_address,
    DEMO_USER_STACK_TOP,
    &user_memory
);
```

继续保留任务创建失败的原有资源清理。固定地址常量仍用于本批布局策略和映射，入口参数已经来自 ELF。

旧prepare_user_image已移除。暂时可以保留bin构建/嵌入产物，后续按需整理接线；
正常复制源必须全部来自传入的ELF image，不要求先清理旧产物才完成本批。

本批两条非空 LOAD 的范围会拒绝完全没有 data/BSS 的旧短程序；纯 BSS 仍支持。旧 WRITE 探针会替换 user_entry.S 并只定义 bin 标签，部分旧 QEMU 探针也按 bin 偏移/长度设计，不能直接用来验收新的 ELF 装载。协作者记录这些覆盖限制，历史脚本适配为 P2 选做维护，本批先运行正常含数据/BSS的演示。

## 收尾与选做练习

作者完成上述改动后集中交给协作者审查和运行。正常演示继续成功，且实际复制位置/长度与程序入口都来自 ELF，即完成本批核心功能。

必要验证由协作者承担：正常运行与初值/BSS检查、一条无效布局在分配前被拒绝的验证，以及实际装载字节和入口参数的核对。原有回归按变更涉及范围运行。

选做练习随时间挑选，均不阻塞继续学习：

| 优先级 | 练习 | 预期 | 学习点/新增工作 |
| --- | --- | --- | --- |
| P1 选做 | 将 entry 改为数据 VA | 准备失败，页数与输出描述不变 | 理解可执行入口；一条临时布局修改 |
| P1 选做 | 数据只保留 BSS | file_size=0 仍能装载，BSS 初值为零 | 理解文件与内存大小；简短用户程序 |
| P2 选做 | 临时改变链接布局，比较生成ELF中的p_offset | 装载仍按新偏移工作 | 理解文件位置独立于 VA；临时链接布局实验 |

完成后梳理“解析布局 → 支持策略校验 → 分配 PA → 复制/补零 → fence.i → VA 映射/flush → 创建任务 → exit 回收”，再用约三个理解小测检查入口、地址转换和资源生命周期。
