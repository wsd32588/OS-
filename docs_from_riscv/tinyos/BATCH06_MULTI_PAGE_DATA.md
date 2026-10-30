# BATCH06：多页数据/BSS 与有界用户页集合

状态：功能、P0验证及理解题原答/审查/修正版均已归档，本批完成。日期：2026-10-30。

导航：[批次索引](README.md) · [上一批](BATCH05_ELF_LOADER.md) · [总路线图](../TINYOS_EVOLUTION.md)

## 最终修正与验收（2026-10-30）

作者明确授权“剩下一些你来改吧”后，协作者完成剩余局部修正并适配原宿主测试。保留作者的PreparedPage阶段循环、页范围迭代、逐页复制和C接口，没有增加堆、独立根页表或自动析构。

- UserMemory使用root/page_count/pages[16]。root借用、内容页拥有，未使用尾部不参与操作；全部释放成功才清空描述，部分失败保留count与全部VA，成功free立即清实际PA。
- 保留BasicPageRange<Tag>泛化、VirtPageRange/PhysPageRange及PageIter；PageRange为虚拟范围别名。仅VA检查canonical半区，PA不套用该限制。释放时分别检查全部有效VA范围，以及跳过null PA后的物理范围。
- loader先限制数据页最多14张，再构造完整区域；逐页分配/复制，纯BSS页源偏移停在文件内容末尾。成功逐条填写VA/PA后一次提交；失败按有效rollback前缀处理栈、代码、全部数据页，修正了索引恒为1及提交总取首条记录的问题。
- find_page仅在有效前缀内匹配合法页VA，不查询MMU或按PA别名匹配。调度器检查entry与SP-1所在页的记录和非空PA，再分配任务槽并接管描述。
- 作者补的真实memset解决大数组零初始化链接依赖。用户buffer扩大到4096+32，sizeof遍历，写入与比较都截为unsigned char；链接脚本只解除数据/BSS单页断言。

正式make -B -j4通过，无警告；最终ELF无未解析符号、无init/fini数组。make test通过70个具体场景（另1条all host tests汇总），原H/D/copy场景及事件顺序保留。快照覆盖root/count/全部16条VA与PA和有效映射；新增6组覆盖四页释放、第二数据页free失败重试、重复PA拒绝、数量与尾部、纯记录查找及非法输入。物理范围另测跨2^55边界和迭代。独立只读复核确认未削弱原失败码、顺序、权限/归属和快照断言。

正式默认timeout 8s make qemu输出yield/R/sleep/XHello from user!/Data OK/exit，之后至少两次S timer；返回124是限时观察。readelf显示RX filesz=memsz=0x282，RW filesz=0x11、memsz=0x1038，数据/BSS实际占两页。

临时副本真实QEMU的P0全部通过：

| 场景 | 观察结果 |
| --- | --- |
| data后续页覆盖起点不同的stack | 分配前拒绝，0次alloc，完整root/页数/输出不变 |
| 内容页总数17 | 同样0次alloc拒绝，状态不变 |
| filesz=4113、memsz=8229 | 强制数据PA不相邻，文件逐字节正确，BSS及页尾全零，资源完整恢复 |
| 默认真实用户ELF | 4内容页+2中间表=6，退出归还6页、4映射移除、描述全空，完整root和satp保留，后续两次S timer |
| 第四flush一次失败回归 | 实际6次alloc、8次flush（含回滚），全部4映射移除，root/页数/输出/satp保持，无已free物理页的残留映射 |

证据：正式构建和默认运行在.cache/batch06-finish-3l2q__dw/；宿主在.cache/batch06-host-tests.log、batch06-host-cases.log、batch06-host-results.json；QEMU探针与独立回滚回归在.cache/batch06-qemu-jyj88p7r/。临时文件复制探针提供已解析布局，默认程序使用正式ELF解析与U-mode执行；不把前者说成新ELF解析器验证。临时QEMU由脚本SIGTERM停止，返回0不表示客体主动关机。独立产品审查无新阻塞，临时探针前后正式源码哈希一致。

保留限制：两个LOAD、代码/栈各一页、最多16内容页、共享root、静态任务，配置不重定位ELF；装载成功前输出不得拥有旧资源。回滚自身再次失败时的历史局部资源归属限制仍保留。本次未提交或推送。

## 执行路径与资源变化

boot提供镜像和三处布局地址；loader验证字段与完整范围，按memsz计算数据页数，逐页向PMM申请物理页，复制文件初值并保留BSS零值，再建立映射和刷新。PageIter枚举虚拟页，PA分别由PMM决定，VA连续不要求PA连续。

全部成功后页记录交给UserMemory；task_create_user确认入口和栈记录仍有PA，再接管并清空源描述。yield/sleep和timer保留各页；exit先验证全部归属，再全部unmap、全部flush、逐页free、回收空L0/L1，root保留。默认4条内容记录描述代码、栈、两张数据页；额外两张中间表由页表路径管理，不放进这4条记录。

## 理解题与答案归档（2026-10-30）

1. 为什么VA范围互不重叠后还要检查PA？两个不同VA映射同一PA，与两条记录各自宣称拥有该页有什么区别？
2. 页对齐的段filesz=4113、memsz=8229，每页分别复制多少文件字节、补零多少段内字节？为什么不能把第一张PA当作连续大缓冲区？
3. 第二张数据页free失败，为什么保留原page_count和全部VA，却清空成功释放的PA？默认退出恢复6页，UserMemory为什么只有4条记录？

### 第一题：VA不重叠与PA归属

作者原答：

> 可能同时映射到一块物理地址上？va虽然在页表里连续，但是不一定在物理内存上连续存在

审查：两点都正确。不同VA可能指向同一PA；连续VA也不保证对应物理页连续。还需区分共享映射和重复拥有：共享映射本身可以合法，但当前每条页记录都负责独立释放，不能把同一物理页记录成两份拥有的资源。

修正版：VA范围互斥只能保证虚拟区域不重叠，不能证明物理资源互不相同。释放前还应独立检查非空PA是否重复，防止两条拥有记录重复归还同一物理页。若以后支持共享页，应另有共享所有权/引用计数等生命周期机制，而不是直接取消当前检查。逐页分配得到的PA不必连续，复制必须使用各页的实际PA。

### 第二题：文件初值与BSS按页分配

作者原答：

> 复制4113字节，补零4116字节

审查：总数正确，4113+4116=8229；题目还要求落实到每张4KiB页。段内补零与最后一页的段外尾部需要区分。

修正版：

| 页 | 文件复制 | 段内补零 |
| --- | ---: | ---: |
| 第一页 | 4096字节 | 0字节 |
| 第二页 | 17字节 | 4079字节 |
| 第三页 | 0字节 | 37字节 |

第三页剩余4059字节在段外，由PMM分配时清零。每页的源偏移和长度分别计算；不能将首张PA作为连续大缓冲区，因为后续VA可能映射到不相邻的PA。

### 第三题：失败重试与六页回收

作者原答：

> 数量和VA只是程序暂时持有，va和count暂时没释放问题不大，但是PA没释放可能导致其他VA重新映射到此区域，还有L0,L1

审查：指出额外的L0/L1正确；保留元数据和清空PA的原因需要修正。VA/count是重试所需的记录，不是“暂时不释放也无所谓”；物理页归还PMM后被其他映射使用是正常复用，危险的是旧描述仍声称拥有它。

修正版：第二数据页free失败后，保留原page_count与全部VA，让下一次调用仍能遍历原区域，完成刷新、剩余页归还和空页表回收。每次pmm_free_page成功后立即将对应physical_page清为nullptr，标记该页已不再归当前描述拥有，重试时不会重复释放；若旧PA记录未清，而该页已经被PMM重新分配，重试就可能误释放别人的资源。最后整体成功才清空描述。默认4条记录拥有代码、栈、两张数据页；额外的L0与L1由页表路径管理，所以退出共恢复6页，借用的root保留。

本批理解收尾完成。下一批在新对话中先核对仓库，再确定一个概念和完整任务范围；不由本次答案归档自动扩改产品代码。

## 完成前集中检查（历史，2026-10-30）

以下是修正前快照，问题已在上方最终修正中处理；后面的2026-10-29审查也仅为历史阶段记录。

- loader错误要求输出page_count非零，使正常空输出直接拒绝；数据file_size仍受单页限制，data count上限须为MAX_PAGE-2。成功只提交root/count，未写全部VA/PA。回滚仍只有{1,0,2}，漏掉后续数据页，可能留下指向已释放页的映射。
- release入口先拒绝空root，令空描述成功分支不可达；unmap前重复检查反向拒绝所有合法VA。范围互斥只检查VA，非空PA还须两两去重，允许重试中多个null PA。
- find_page应仅按合法页VA查描述有效前缀，不查MMU或按相同PA匹配；补root/count/地址边界。sched先检查entry/SP，再向下对齐entry和SP-1，查找失败或所找记录PA为空才拒绝，不能把查找成功当错误。
- 实际make -j4相关源可编译，loader报告遗漏pages初始化警告，最终链接缺memset；大数组零初始化使编译器生成该调用，lib/string.c当前仅实现memcpy。需补真实memset，不能用返回空桩绕过清零。user/main.c和user.ld尚未开始跨页演示修改，不视为已完成。

隔离宿主探针使用真实PMM/VM/user_memory/user_image、临时loader仅将fence.i替换为编译器屏障，flush用可控桩。正常空输出返回-1且0分配；为观察被遮挡路径而人为将输出count置1后，成功返回0/count4但首条VA/PA为0；第四次flush失败留下第二数据页映射，PMM立刻重用同一PA；同PA别名导致find_page错误返回未拥有VA；合法描述与空描述释放均-1。此为软件逻辑复现，不代表真实TLB/CSR验证，也不建议调用者用count1初始化输出。

证据.cache/batch06-current-review-5gbdzb_i/含源码哈希、临时探针、编译/结果与说明；独立只读审查一致。正式产品源码未修改，未运行QEMU或尚未适配的整套宿主测试，旧fixture适配仍由协作者承担。

## 本批要得到什么

让真实用户 ELF 的数据/BSS 超过 4 KiB 后仍能装载、执行并完整回收。把 UserMemory 从固定的代码/栈/数据三个单页字段，改为有容量上限的页集合。代码和用户栈仍各一页，仍是两个 LOAD、共享根页表、静态任务；不同时引入堆、动态容器、独立 root、ELF 重定位或调度器重写。

作者完整实现核心功能与用户程序，协作者解释、集中审查并承担旧测试适配和必要复验。同批修正、验证、原答与修正版继续维护本文件。

## user_memory.cpp阶段审查（2026-10-29）

本轮只检查作者已改的user_memory.h/.cpp，未把尚未适配的loader、调度器或旧fixture算成本次错误。新结构和C入口可编译，MAX_PAGE在C++用constexpr、C用宏不影响当前容量/布局；不是阻塞问题。

必须修正：入口遍历全部16项并拒绝null PA，导致未用尾部、空描述和部分释放重试都被拒；data_enabled及2/3长度仍是旧模型，必须先校验root/page_count再遍历有效记录；重复PA检查误写成VA比较且只看前三项；全部有效VA的非零/canonical/对齐及两两重复检查须在修改前完成。find_page目前仅声明，后续需按本批契约补定义。原有查询→unmap→flush→free→reclaim阶段顺序和成功free清实际成员仍正确。

目标Clang单文件语法检查通过。临时宿主策略探针（映射已移除、PMM/flush等为计数桩，不代替真实页表验证）复现：空描述返回-1；有效四页描述返回-1；部分释放重试返回-1；填满16项时返回成功但仅unmap/free前三页后清空整个描述。证据.cache/batch06-release-review-m7cqjfi5/含当前快照、探针和日志。独立只读审查一致，未代改实现/跑全仓测试或QEMU。

## 地址范围工具的局部精简（2026-10-29）

作者授权协作者选择精简或保留address.hpp。选择保留文件及现有Addr/PageNum接口，仅整理PageRange：保留start/end页号，提供try_from_bytes和is_valid/overlaps_with；去掉尚无调用方的sizeof(T)包装和迭代器，避免把对象描述大小误当成其管理内存大小。初读时end字段与end()重名，作者在检查期间已改成_end()；修改以这份最新快照为基础，移除迭代器不是恢复旧错误版本。

工厂只构造非空范围，按字节起止向外覆盖整页；零长度、加法/取整溢出、跨Sv39 canonical空洞或不能表示one-past端点都失败，输出不变。is_valid也检查页号宽度、顺序和同一canonical半区，使手工构造的范围遵守相同边界策略。零字节的空区间不属于本批映射契约；空集合的互不重叠检查仍可成功。

validate_no_overlap提供span<const PageRange>入口，只检查有效前缀；保留原数组模板作为转发，不把未使用尾部纳入有效记录。release中可用try_from_bytes(VirtAddr(va), PAGE_SIZE, ranges[i])替代局部lambda的范围计算，再把ranges和page_count组成span送去检查；PA重复仍另行校验。该调用接入和release其余修正继续由作者完成。

新增test_vm中的范围回归覆盖跨页、相邻/重叠、有效前缀、空/非法范围、失败输出保持、高VA和canonical/溢出边界。独立编译运行test_vm+真实PMM/VM通过，原地址/PTE/页表回归通过；RISC-V constexpr与vm/user_memory单文件语法检查通过，无警告。证据.cache/address-range-simplify-vm3gbvl_/含最新改前快照与日志；独立只读审查通过。未改user_memory.cpp、未运行B06尚未适配的整仓构建/宿主或QEMU，不视为整批验收。

## 为什么先改页归属

当前 data_page 只能记录一张物理页；仅解除 loader 的大小限制，会使第二张数据页没有归属记录，退出或回滚时无法正确处理。新页集合先记录每张内容页的 VA 和 PA，再让装载、查询和释放围绕这些记录工作。

页表页继续由页表路径管理，不加入用户内容页集合；root 仍借用。固定容量只是当前内核没有堆时的资源上限，物理页仍由 PMM 动态分配。

## 数据结构与公开接口

在 include/mm/user_memory.h 中替换旧七字段结构，不同时保留旧 code_page/stack_page/data_page 作为另一份状态：

```c
#define USER_MEMORY_MAX_PAGE 16U

typedef struct UserMemoryPage {
    uintptr_t virtual_address;
    void* physical_page;
} UserMemoryPage;

typedef struct UserMemory {
    Sv39PageTable* root;
    size_t page_count;
    UserMemoryPage pages[USER_MEMORY_MAX_PAGE];

#ifdef __cplusplus
    int release();
#endif
} UserMemory;
```

头文件显式包含 stddef.h、stdint.h 和 mm/vm.h，继续使用具名 struct 和条件式 C 链接保护。保留 user_memory_release 的原签名，并新增只读查找入口：

```c
const UserMemoryPage* user_memory_find_page(
    const UserMemory* memory,
    uintptr_t page_address
);
```

查找目标须为合法、非零、页对齐的 VA；描述为空、root 为空、数量为零/超限或找不到时返回 NULL。只遍历 pages[0, page_count)，匹配后返回借用记录，即使它的 physical_page 已在部分释放中清空，也可返回这条记录，由调用者判断是否仍持有物理页。它不查 MMU、不分配、不修改描述，不能替代完整的归属校验。借用指针不要跨描述转移/清空后继续使用。

数组记录顺序为：代码页、栈页、数据页0、数据页1……。page_count 只计算拥有的内容页，不包括 root/L1/L0。未使用的尾部清零，不参与查找与释放。

## 文件范围和分工

| 文件 | 本批修改 | 负责 |
| --- | --- | --- |
| include/mm/user_memory.h | 新页集合布局和查找声明 | 作者 |
| kernel/mm/user_memory.cpp | 按集合释放、查找、保持部分失败重试 | 作者 |
| kernel/mm/user_loader.cpp | 数据页计数、区间检查、逐页复制/映射和提交 | 作者 |
| kernel/task/sched.c | task_create_user 适配新描述，接管逻辑保持 | 作者 |
| user/user.ld | 去掉数据/BSS单页限制，保留代码单页和其他约束 | 作者 |
| user/main.c | 跨页BSS读写及yield/sleep后验证 | 作者 |
| test/user_memory_fixture.hpp、host_support.hpp/.cpp | 快照与页记录适配 | 协作者 |
| test/test_user_memory.cpp、test_user_data.cpp、test_user_access.cpp | 保留原场景语义，改用新记录 | 协作者 |

本批允许修改 UserMemory 的 C 布局，C 调用者和宿主 fixture 一起适配；trap_frame、汇编偏移不改。当前测试中有大量旧字段访问，这是已知的适配工作，不要求作者再手工重写一整套历史测试矩阵。

user_loader.h/.hpp 的装载参数和 UserMemoryLayout 三个地址保持。boot 继续提供配置，用户栈仍一页，当前默认地址足以容纳两页数据/BSS。user_elf.c 已能解析较大的 memsz；user_image_load_segment 已支持按目标容量检查，均不要求重写。user_access 已逐页复制，算法保持。Makefile/CMake 的实现源列表通常不变，loader 仍只参与目标构建。

作者先完成产品代码并运行 make；旧测试若暂因字段变化不能编译，交给协作者按新布局适配，不把它直接归为新算法失败。

## 装载器：先计算计划，再动资源

### 页数、容量和区间

代码段继续要求 0 < memsz <= PAGE_SIZE，代码和数据权限仍分别为 ELF flags=5/6；数据段要求 memsz > 0、filesz <= memsz，并满足镜像范围检查。不要再对数据统一套用 filesz/memsz <= PAGE_SIZE 的旧条件。

数据页数用不发生向上加法溢出的方式计算：

```text
data_pages = memsz / PAGE_SIZE + (memsz % PAGE_SIZE != 0)
total_pages = 1代码页 + 1栈页 + data_pages
```

先确认 data_pages <= USER_MEMORY_MAX_PAGE - 2，再做总数及后续乘法。当前容量16意味着最多14张数据页；这是内容页上限，中间页表会另占物理页，分配仍可能失败。

分配前校验所有地址、文件范围、入口和完整映射区间：代码与栈各一页，数据区按 data_pages 向外覆盖整页。起点不同不代表不重叠，比如 data=[0x40003000,0x40005000)、stack=[0x40004000,0x40005000) 必须拒绝。

可以使用已有 PageRange/validate_no_overlap，先用 checked 运算得到有效端点，再生成页号；不要对原始不合法地址直接 from_floor。半开区间允许首尾相接。区域上界或栈顶若数值溢出、跨 canonical 空洞或 one-past 端点不能表示，明确返回 INVALID_ARGUMENT；本批不新增最高地址哨兵策略，更不能绕回零。

全部检查在第一次 pmm_alloc_page 前完成，C shim 的判空和核心自己的内容校验继续保留。

### 每张物理页独立复制

为计划中的每页独立分配，不能假设相邻 VA 对应相邻 PA。保留局部 PreparedPage 固定容量数组及每页 attempted/mapped 标记。

复用 user_image_load_segment 时，每个数据页单独调用。令 P=PAGE_SIZE，x 为这一页相对段起点的字节偏移：

```text
page_mem  = min(P, memsz - x)
page_file = x < filesz ? min(page_mem, filesz - x) : 0
src_off   = segment.file_offset + min(x, filesz)
```

传入当前页 PA、容量 P、image.data()/image.size()、src_off、page_file、page_mem。纯 BSS 页的 src_off 停在文件内容末尾，不能无条件加 x 后跑出镜像，导致本来合法的零字节复制被拒绝。每页剩余部分由 PMM 初始清零保证；栈也继续使用清零页。

fence.i 在全部内容复制后执行一次。map/flush 遍历所有计划页，继续使用原外部 map/unmap/free 边界。失败回滚必须覆盖全部记录：栈→代码→各数据页处理 unmap/flush/reclaim，free 按代码→栈→各数据页。旧的三元素 rollback_order 不能漏掉新增数据页，未映射或未分配的记录仍按标记跳过。

全部成功后一次性提交 root、page_count 和页记录；失败不修改调用者输出。成功调用前输出不能占有尚未释放的旧资源。原回滚自身再次失败时的局部资源归属限制继续保留，本批不引入析构或新事务框架。

## 释放与重试

1. 空描述为 root=NULL、page_count=0；正常释放成功也回到全空。root为空但数量非零、root非空但数量为零、数量超限都拒绝，构造 span 或遍历前先检查数量。
2. 修改前先检查全部有效记录：VA 合法/非零/页对齐且不重复，非空 PA 不重复；查询到的映射必须属于对应物理页并带 U，已未映射则允许继续。不要先释放前面的页，再检查后面的归属。
3. 保持全部 unmap → 全部 flush → free 内容页 → reclaim 空表的阶段顺序。root不释放，原内核映射保留。
4. 每次 free 成功立即清对应 pages[i].physical_page；失败仍返回 PAGE_FREE_FAILED。保持全部 VA 和原 page_count，不能减计数或压缩数组，否则后续重试可能漏掉刷新/空表回收。
5. 只有全部阶段成功才整体清空描述。null PA 加仍保留的 VA 可能是合法的重试状态，不可当成整个描述为空。

仍不添加自动析构、删除复制操作或动态分配。C 调度器继续按约定显式接管后清空源描述；普通按值复制并不会自动生成第二份合法所有权。

## 调度与用户程序

task_create_user 原 entry 非零/2字节对齐、SP非零/16字节对齐及root检查保持。将旧 code_page/stack_page 存在性判断改成页集合查找：查 entry 所在页，以及 (user_stack_top - 1) 所在页，均须找到非空 physical_page。先拒绝零SP再减一；栈顶恰好在页边界，直接 floor(stack_top) 会落到栈外。查找发生在任务槽位分配和接管之前，失败时调用者描述保持。

成功后的 task->user_memory = *memory，再将 *memory 清空，现有调度/exit/getter流程保持；getter继续返回当前任务借用root。

用户演示先把现有 volatile buffer 扩到 4096 + 32 字节，检查全部BSS初值、写入并在yield/sleep后核对，保留Hello、Data OK和exit。原 i+1 写入 unsigned char 后会在256处截断，所以写入与期望比较都显式转换为 unsigned char；不要扩大数组后仍把字节与未截断的 i+1 比较。

在 user.ld 只移除数据/BSS的单页ASSERT，代码单页、入口和禁止不支持运行时节的断言保留。内核容量由loader检查，用户链接脚本不包含内核私有头。默认新数据区应恰占两页：以readelf核对，正常拥有4内容页，加2中间表，共消耗6页；退出恢复这6页，root不计入。

## 验收与学习分工

作者整批实现后集中审查，以下P0由协作者承担，不追加完整历史矩阵：

- make -B与适配后的make test通过；原两/三页fixture按新记录表示继续验证权限、归属、flush/free失败重试及复制语义，不只留下正常路径测试。
- 正式ELF的RW段memsz跨页，默认用户程序检查BSS零值、跨页写读与yield/sleep后保持，Hello/Data OK/exit正常；6页恢复，root与内核映射保留，退出后两次S timer。
- 一条起点不同但数据区覆盖栈页的配置在0分配时拒绝；一条超过16内容页容量的布局也在0分配时拒绝，root/页数/输出描述均不变。
- 在第二数据页free处注入失败，再重试：已经成功释放的页不重复归还，VA和数量仍能支持后续清理，最终内容页和空表全回收、root保留。
- 一个临时跨页有初值数据场景验证文件复制边界，包含 filesz 结束后开始的BSS；物理页不能被当作连续大缓冲区。这个探针由协作者准备，不要求作者再写另一套用户工程。

P1选做：纯BSS段、页数恰好上限、额外分配失败位置；P2选做：更多区间/失败矩阵。作者可以只讨论这些思路而不实现选项。

完成后先梳理页集合、按页复制和资源变化，再收尾约三题。任务初交付时只确定目标和接口；当前实际进度与阶段审查见文件开头，本文件持续更新实现和验证结果。
