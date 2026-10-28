# TinyOS 渐进改造草案

更新：2026-10-28。用户内存迁移和C接口兼容已通过；当前任务为将受限ELF装载移入内存模块，并由启动层显式提供布局配置。

标准库接入单独记录在 [CPP_STANDARD_LIBRARY.md](CPP_STANDARD_LIBRARY.md)。本文继续维护地址/页表设计、实现问题和学习记录，不重复维护库选型与工具链接线。
作者确定本轮rCore完成第四章后先回TinyOS，以实际功能实现和作者亲手设计测试为主，
遇到理解缺口再回rCore补充；不要求先完成全教程，不自动进入第五章。
当前rCore B22功能与最后问答已完成，第四章本轮收尾，TinyOS主线恢复。
ELF当前状态与原答/修正统一见[ELF学习总结](USER_ELF_LEARNING.md)。本文件集中保留当前内存模块边界任务、已完成C++改造及后续方向，不另建零散任务文档。

## 方向与本轮范围

作者希望利用 rCore 学习所得，逐步收尾 TinyOS 的临时代码，同时边实现边制定
C++ 改造计划，避免等规模扩大后集中重构。延续既有的 C++ 主体方向，按模块推进。
只把紧接的受限ELF装载边界整理细化为本批任务，较远阶段保留建议，不作为同时开展的作业。

rCore 当前的可借鉴进展集中在地址类型、页帧所有权、MemorySet 与 ELF 用户空间构造。
截至2026-10-25，BATCH18共享跳板、BATCH19单ELF分页U-mode执行和BATCH20分页write/用户恢复已通过。
BATCH21分页协作调度与BATCH22分页timer抢占均已通过作者功能验收及问答；旧Bare路径仍保留。
整体归纳见[rCore章节总结](rcore/README.md)。两套实现独立验收；TinyOS的ELF装载现也已单独通过。
TinyOS 的 sleep、用户内存安全复制、部分释放重试和失败回滚也是需要保留的既有成果。

恢复后由作者主导完整功能与有学习价值的测试，协作者先帮助明确问题、接口、核心不变量和可观察结果，
再解释、审查、定位问题并承担例行复验，不默认交付大框架只留几个TODO。
下面先给出当前完整任务，已完成部分用于复习，较远阶段仍是建议，不自动开展。

## 当前一批：将受限ELF装载归入内存模块

### 作者当前实现审查（2026-10-29）

提交前最新复核：下面初审发现的合法性方向、核心empty/段数/checked页尾和栈顶、PageRange比较及Makefile/CMake配置问题均已修正。make实际编译新loader成功，最终链接在boot_start_user仍调用旧prepare_demo_user_memory处失败；启动接线由作者留待之后完成，本次按在制进度提交。make test通过，未运行QEMU；日志.cache/git-checkpoint-2026-10-29/。以下保留初审说明供对照，不再作为这些项目仍未修复的结论。

作者明确本轮先检查已修改文件，boot_private和启动侧接线之后自己处理，本轮不要求完成。输入/输出地址混用、头文件包含和标签拼写已修；C shim只读span构造、image.data()、输出引用赋值、root取址及外部map/unmap已正确，新核心单文件目标语法检查通过。

当前集中修正：loader23–28行的canonical和对齐检查缺!，合法配置被当成错误，VA应使用VirtAddr；核心入口仍需补empty、段数和checked页尾/栈顶。新增PageRange模板268行用了PageNum未提供的>=，实际实例化已复现错误，可比较.val()；它尚未接入loader，现有单页地址相等检查无需强制与模板同时使用。Makefile55行新对象末尾缺续行反斜杠，make -n实际missing separator；宿主CMake24行误加不存在的kernel/user/user_loader.cpp，应移除此target-only源（含fence.i），而非改成mm路径。

独立只读审查一致；本轮未改正式功能、未执行QEMU或重复旧测试，不把未完成的boot接线算作本轮已修改文件的问题。

### 目标与可观察结果

由作者整批实现，将启动演示的地址选择与实际装载操作分开。启动层提供镜像、已解析的ELF元数据和三页布局；内存模块完成校验、分配、复制、映射、刷新和失败清理，成功后交出UserMemory。当前共享root、受限两段ELF、三张内容页和两张中间表继续作为运行基线。

默认运行仍输出Hello与Data OK并正常退出用户任务；另外可以仅改变用户栈位置，不修改装载算法，用户仍正常执行。配置不负责重定位固定VA的ELF：代码/数据地址必须与ELF段和入口匹配，不能只改配置就搬动程序。

### 文件范围与职责

| 文件 | 本批修改 |
| --- | --- |
| kernel/boot/private/user_prepare.cpp → kernel/mm/user_loader.cpp | 搬移原装载主体，替换DEMO常量为显式配置，提供C++核心及薄C入口 |
| include/mm/user_loader.h（新增） | 声明三页布局配置和提供给C启动层的入口，包含所需C兼容类型 |
| include/mm/user_loader.hpp（新增） | 声明实际可复用的C++装载入口，引用页表/元数据/输出并以span描述镜像 |
| kernel/boot/private/boot_stages.c | 包含公开装载头，以现有DEMO常量构造配置，调用新C入口，按同一配置计算用户SP |
| kernel/boot/private/boot_private.h | 移除旧prepare_demo_user_memory声明；DEMO地址策略继续留在启动侧 |
| Makefile | 对象项由kernel/boot/private/user_prepare.o替换为kernel/mm/user_loader.o |

准备函数仍包含fence.i，只进入目标构建，本批不加入宿主CMake源列表。已有UserMemory七字段及显式转移、调度/trap/syscall实现、地址页表/PMM接口和用户链接脚本不改。新内存模块不包含boot_private.h，不引用DEMO_USER_*，不在boot中留下纯转发的旧prepare兼容函数。

### 两个公开入口

user_loader.h中，类型声明放在extern C块之外，函数声明放在条件式extern C块之内：

```c
typedef struct UserMemoryLayout {
    uintptr_t code_address;
    uintptr_t stack_address;
    uintptr_t data_address;
} UserMemoryLayout;

int user_memory_load_elf(
    Sv39PageTable* root,
    const void* image,
    size_t image_size,
    const UserElfLayout* elf_layout,
    const UserMemoryLayout* memory_layout,
    UserMemory* memory_out
);
```

头文件包含stddef.h、stdint.h、mm/user_memory.h和user/user_elf.h。user_loader.hpp包含该C头及span，声明：

```cpp
namespace tinyos {
int load_user_elf(
    Sv39PageTable& root,
    std::span<const unsigned char> image,
    const UserElfLayout& elf_layout,
    const UserMemoryLayout& memory_layout,
    UserMemory& memory_out
);
}
```

C入口先拒绝root/image/elf_layout/memory_layout/memory_out中的空指针及零镜像长度，再构造引用和span并转发。C++核心检查空镜像和布局，不在接口内部保存调用者的span或元数据引用。核心可用VirtAddr做checked地址运算；无需为配置另造一组地址类或管理类。

### 分配前完成的校验

1. 三个配置页起点均非0、符合Sv39 canonical、按4KiB对齐，且两两不同。用checked加法确认每张页的最后一个字节合法；栈起点加PAGE_SIZE得到的栈顶也须无溢出且canonical。对齐后栈顶自然符合现有16字节调用约定。
2. 沿用两LOAD限制与原有大小/文件范围/权限检查。按配置的code_address和data_address查找相应段，拒绝重复/未知段，继续要求代码段flags=5、数据段flags=6；段开始地址必须可表示为有效VA，不能先截断再比较。
3. 入口必须为2字节对齐，并处于代码段实际file_size覆盖范围；仍用先比较下界再相减的方式，避免不受控加法溢出。

这些检查都应在第一次pmm_alloc_page前结束。root是否已有映射仍由现有map返回冲突处理，不提前覆盖，不把布局检查改为“目标必须已有映射”。不增加对输出哨兵值的全零要求：失败继续保留原输出，成功调用的前置条件是输出不拥有尚未释放的资源，避免覆盖泄漏。

### 装载流程与资源边界

直接复用已完成的PreparedPage数组与阶段循环，字段中的VA改来自memory_layout。分配、复制、map、flush和提交的相对顺序保持；栈没有ELF文件内容，继续使用PMM提供的清零页。保留外部sv39_map_page、sv39_unmap_page、pmm_free_page等注入边界。

mapping_attempted和mapped继续分开；回滚unmap/flush/reclaim保持原栈→代码→数据次序，free仍为代码→栈→数据。只解除本次成功建立的叶映射，不破坏原有映射。成功后才一次性写memory_out，root继续借用，三张内容页交给描述管理。

在boot_start_user中使用局部C配置，初值仍取当前三个DEMO地址。load成功后，将同一memory_layout.stack_address + SV39_PAGE_SIZE作为task_create_user的用户栈顶；不要配置已经换了栈，启动时还传另一个旧常量。这里的加法依赖装载成功已完成的checked验证。镜像起点、elf_size、layout->entry_address、任务成功后清空源描述和任务创建失败后的善后保持。

本批不合并prepare回滚与UserMemory::release为一套新析构框架。两者拥有不同的提交状态，已有prepare清理自身也失败时的局部归属限制继续保留，不扩大重设计。也不增加内核堆、动态容器、多页ELF段、ELF重定位、独立root或调度机制。

### P0验证与交付

作者完整实现后集中检查，以下必要验证由协作者承担：

- make -B、make test通过，原C启动方正确链接；MM实现没有boot私有头/DEMO宏依赖，目标新对象进入最终链接，旧对象不再作为依赖。
- 一次默认QEMU完成yield、sleep、Hello、Data OK、exit，退出后A及至少两次S timer继续；默认成功装载消耗三张内容页加两张中间表，退出回收到原有基线。
- 一条无效配置（例如stack_address与code_address相同）在分配前拒绝，PMM页数、根分支、原映射和输出描述不变。
- 临时仅改栈地址为0x40005000，顶为0x40006000，保留原ELF代码/数据VA；检查旧栈位置无映射、新栈页权限与清零正确、实际用户SP采用新值，并正常退出回收。这是配置与执行路径验证，不要求作者另写测试矩阵。
- 原copy/release的权限/范围/部分失败重试宿主回归保持，C/C++声明与ABI符合前一批约定。

作者可先运行make、make test、timeout 8s make qemu观察默认行为；124仍表示常驻演示被限时终止。P1可选是新的未对齐/溢出配置拒绝，P2可选是更多历史故障注入。现有verify脚本仍引用旧函数/路径/页数的部分，由协作者按当前验证需要适配，不要求保留过时接口或先修整套历史矩阵。

任务交付时只更新本文；作者当前实现进度见本节开头审查，待整批完成后继续验收。完成后的功能梳理与约三题仍归档本文。

## 已完成：用户内存准备、访问和释放

### 剩余C接口的C++兼容性收尾（2026-10-28）

作者授权继续修改剩余接口。已为SBI、trap、调度、syscall、timer、UART、ELF解析及用户汇编包装的C函数声明补条件式extern C，类型与C++模板接口留在块外；syscall声明仍在非汇编分支内。trap.h的布局断言分别使用C11 _Static_assert/C++ static_assert，user_api.h的退出声明分别使用_Noreturn/[[noreturn]]。此处只处理现有接口的语言兼容，装载逻辑位置和模块职责收拢留到下一批。

构建与宿主测试通过、无警告；16个C接口头在宿主和RISC-V的C11/C++20模式下逐个严格检查，共64项通过。C++探针实际引用26个入口，全部保留原C符号并匹配内核/用户ELF导出，最终内核无未解析符号。原C和用户汇编调用参与正常重编。证据.cache/c-interface-review-w3msuci7/含改前快照、两处旧C++语法失败和最终检查；最初独立探针漏加freestanding且宏头产生空翻译单元，调整探针配置后全部通过，不修改工具链。此次未重复运行QEMU、未clean或提交。

### 实现与重复操作简化（2026-10-28）

作者已完成三实现改.cpp、UserMemory成员释放及tinyos::copy_readable_user类型化入口。随后明确授权协作者简化重复页操作，已在原文件完成：

- prepare用局部PreparedPage数组描述代码/栈/数据页，分阶段循环完成分配、ELF内容复制、映射、flush和失败回滚。栈没有ELF段，只使用PMM清零页。mapping_attempted与mapped仍分开，正常顺序为代码/栈/数据，回滚unmap/flush/reclaim保留栈/代码/数据的原顺序，free仍为代码/栈/数据。
- release用局部PageReference数组绑定实际成员的地址，以span选择两页或三页；各阶段分别循环，成功释放立即清空实际成员，保留失败重试和原错误码。
- access由try_add统一检查起止地址与溢出，直接写入目标span，去掉重复canonical检查及多余指针转换；仍逐页查询并检查V/R/U。
- boot_private.h补齐C函数保护，vm_arch.h将保护范围覆盖全部C函数；prepare映射继续经过外部sv39_map_page，原注入入口保留。

正式make -B与make test verbose通过、无警告；原有复制/释放/部分失败重试测试通过。一次QEMU正常yield、sleep、Hello、Data OK、exit后仍有两次S timer，限时返回124为预期。三个C入口保持原名，最终内核无未解析符号，新对象无init/fini数组。

额外在临时宿主探针中对照简化前后prepare：仅移除RISC-V fence.i指令，不改其余算法，验证入口拒绝、部分分配回滚、已有映射冲突、flush失败回滚及正常准备/释放；页数和输出描述符合预期，记录的回滚事件次序一致。架构行为以正式QEMU结果为准。证据统一在.cache/page-loop-simplify-38luo4kk/，含改前快照、探针、前后日志及主构建/宿主/QEMU记录。本次未clean、暂存或提交。

### 功能梳理与三题审查（2026-10-28）

当前启动层提供受限ELF与共享root，prepare验证布局后分配代码/栈/数据页、复制段内容、映射和刷新，成功后才写输出描述。调度器接管UserMemory；syscall通过C入口进入类型化复制核心，按用户页表和V/R/U权限读取数据；退出时显式release先检查归属，再unmap、flush、free和回收空表。共享root继续借用，局部页描述数组不拥有新资源，不增加动态分配。

**第一题：为什么分别记录mapping_attempted和mapped？**

作者原答：

> 因为两个不一样，attempted是检查是否进行过map，而mapped记录是否成功，未遂和成功是两个处理逻辑

审查：区分正确，还需说明失败可能留下中间页表，以及不能误删别人的映射。

修正版：mapping_attempted表示已经调用map，失败时仍可能创建了中间页表，所以清理路径按尝试标记刷新并回收空表。mapped表示本次真正建立了叶映射，只有它为真才能解除这条映射。若因已有映射冲突而失败，原映射不属于本次准备，不能unmap。

**第二题：为什么每释放成功一页就清对应成员，却等全部成功才清空UserMemory？**

作者原答：

> 释放一页后立即清空成员，如果后续失败了，那也可以直接接着之前的部分开始清理，但是如果是直接清空整个UserMemory错哪里你都不知道，到时候如果重新释放的时候会导致之前清理的没有成功释放成员，导致内存管理出现问题

审查：已抓住重试需要保留进度；重试不是从某条代码继续，而是重新进入流程，通过资源状态跳过已完成部分。清空整个描述的直接后果是丢失剩余资源归属，造成泄漏。

修正版：代码页free成功后立即置空code_page，后续栈页free失败时，stack_page仍保留原指针，root和各VA也保留。再次release会重新检查并执行各阶段，空页指针不会再次free，未映射结果允许继续，剩余物理页和空表可以继续回收。提前清空整个对象会丢失尚未释放的页和后续清理所需地址。

**第三题：为什么目标可用span，用户源VA不能直接转成指针读取？**

作者原答：

> 缓冲区是一块完整的内存块，连续是span最适配的场景，而用户源va就只是一串地址，你拿到一串数字的指针似乎也没啥用

审查：连续范围确实适合span，但关键是当前地址空间中的合法可访问性。用户VA也能在正确映射和权限下被解引用；强制转换只改变类型，不建立映射、不检查权限。span要求元素地址连续，不要求物理页连续，也不会自行验证地址或延长生命周期。

修正版：目标是内核已经持有、大小明确且可写的缓冲区，因此用span描述其指针和长度。用户源地址来自调用者，可能非法、未映射或指向不允许读取的区域；即使VA连续，后续页的映射和权限也可能不同。必须校验范围，再逐页查询用户映射并检查V/R/U，通过当前内核可访问的物理页映射复制。本项目仍共享root，权限与范围检查依然不能省略；以后独立root时，同一VA还可能对应不同PA。span本身不分配、不翻译、不验证这些条件。

本批三题已集中审查并归档。下面保留原任务与接口约定，便于对照实现；下一批按既定方向收拢内存模块职责和对外接口。

### 目标与文件范围

把原有三页用户内存闭环迁入C++，使用已完成的地址与页表类型。沿用共享root、两段受限ELF、
代码/栈/数据三页及五页正常回收，先建立清楚的模块职责。作者完整实现，协作者集中审查和例行复验。

| 文件 | 本批修改 |
| --- | --- |
| kernel/mm/user_memory.c → .cpp | 释放逻辑进入UserMemory::release，原C函数判空后转发 |
| include/mm/user_memory.h | 给struct加UserMemory标签，保留七字段顺序，加条件式C++成员声明与C函数保护 |
| kernel/mm/user_access.c → .cpp | 用户访问策略进入下面的C++函数，原user_copy_from继续服务syscall.c |
| include/mm/user_access.hpp（新增） | 公开实际复用的C++用户复制入口，采用地址类型与span，不新增只持root的包装类 |
| include/mm/user_access.h | 保留原C函数签名并补C链接保护 |
| kernel/boot/private/user_prepare.c → .cpp | 沿用原受限ELF工厂，类型检查与映射使用现有C++能力，保留C入口和rollback |
| boot_private.h、vm_arch.h、uart.h | 补必要的C函数链接保护，避免C++调用方找不到原C符号 |
| test/CMakeLists.txt | 将user_memory/access源后缀改.cpp；准备函数含fence.i，只进目标构建 |

Makefile现有.cpp规则及对象名可复用，先核对依赖；不另建一套对象清单或改全仓语言。

### 接口与职责

UserMemory仅新增 `int release();` 成员，原七字段依次仍为root、code_page、stack_page、
code_address、stack_address、data_page、data_address。字段继续满足C调用方与宿主fixture布局；
不加用户定义构造/析构或删除复制操作，保留任务创建时显式按值转移再清空源描述的机制。

用户复制采用C++自由函数，不把U权限策略塞进硬件表页类型：

```cpp
namespace tinyos {
int copy_readable_user(
    const Sv39PageTable& root,
    VirtAddr source,
    std::span<unsigned char> destination
);
}
```

声明放user_access.hpp，包含所需C++头；实现放user_access.cpp。span只描述内核目标缓冲区，
不是把用户VA直接变成内核可访问指针。源地址仍逐页查询PA，检查V/R/U并按页内剩余长度分块复制。
仅使用当前子集和宿主std::span共有的data/size/[]等能力，不扩写标准库。

原三个C入口prepare_demo_user_memory、user_copy_from、user_memory_release的参数、返回值保持。
C shim在长度为0时先返回成功，然后检查root/destination，再构造引用/span，不能先解引用null root。
使用 `length - 1` 校验最后一个字节；UINTPTR_MAX处单字节等原有合法边界继续成立。
跨页中途失败仍允许目标保留合法前缀，不顺便改成事务式复制。

### 资源生命周期必须保持

- 准备阶段先检查ELF布局再分配，成功后才提交memory_out；任务创建成功才接管并清空调用者描述。
- root是借用，三张内容页归描述管理；内核根表与静态内核栈继续保留。
- release显式返回错误：解除映射 → flush → 释放内容页 → 回收空中间表。释放每页成功就清其指针，整体成功后才把描述置空；失败保留后续重试所需状态。
- 内部继续走现有sv39_unmap_page、pmm_free_page等可注入边界；准备工厂保留原外部map入口，不为成员调用绕过原--wrap测试。
- 析构不自动释放活动页表或吞清理错误。本批不增加RAII资源回收、内核堆、独立root、trap/调度切换。

### C改为C++时一次处理的语法差异

1. void指针转换需显式；使用现有Addr/PTE等类型，但不改变原返回码。
2. prepare的goto cleanup不能跨过load_reslut和const flags等变量的初始化；把相关声明放到首次goto前，或用清楚的局部作用域组织，不删掉失败清理。
3. C++20指定初始化器必须遵守UserMemory字段声明顺序；当前工厂把data_page写在code_address之前，应调整初始化列表而不是改结构体布局。
4. C++清空用UserMemory{}，C文件仍可保留(UserMemory){0}。boot_stages/sched本批继续用C，不能把C++语法写进去。
5. 头文件只在公开C函数声明处使用条件式extern C，C++类型/成员声明放在外面。所有C++调用到的架构/UART入口都需核对，不能只改新实现文件。

### P0与交付方式

作者把以上作为一整批完成后集中检查，不逐函数增加作业。P0由协作者承担：

- make -B与make test通过，目标C++/宿主C++都编译到同一实现；原C调用方仍能链接。
- 复用原user_access的范围/权限/跨页场景及UserMemory正常/重复释放、归属拒绝、部分失败重试；一条既有拒绝路径确认状态不变。
- 核对C ABI、七字段布局、没有动态初始化或新运行时依赖，原失败注入及事件顺序保持。
- 一次QEMU正常完成yield/R/sleep/X/Hello/Data OK/exit，退出后A和至少两次S timer继续。限时124仍按原含义判断。

旧prepare QEMU脚本可能仍引用.c及历史签名/页数，协作者按实际需要临时适配，不要求作者先修完整矩阵。
本批不修旧prepare清理失败时局部归属的历史限制，不把语言迁移扩成整个rollback重设计。
完成后功能梳理与约三题继续合并本文。之后再收拢内存模块对外接口，独立用户地址空间另行设计。

## 已完成：PhysicalPageAllocator封装PMM状态

### 作者实现集中审查（2026-10-26）

作者已将PMM改为.cpp、五项状态收进匿名命名空间内类，并用四个C ABI转发；头文件保护和宿主CMake源路径接线正确。未发现迁移引入的算法错误，协作者未修改实现。

make -B与make test均0且无警告，PMM8192页/20000次操作与其余原宿主场景通过；对象有四个原C符号、无动态初始化节或C++运行时依赖，最终内核无未解析符号。一次限时QEMU完成yield/R/sleep/XHello/Data OK/exit，退出后两次S timer，没有setup失败或Trap，124来自限时。证据 `../.cache/pmm-cpp-review-2pb7eimy/`；核对八个源配置前后哈希不变。

集中列出的本批收尾：类显式删除复制/移动构造和赋值并保留默认构造；available_pages加尾部const；五项成员明确初值并用constinit约束唯一实例。当前ppa{}已经值初始化清零，不属于未初始化故障；实际对象也没有观察到动态构造，这项补的是编译期约束。另三个地址字段的旧注释已错位，可随手修正，不作为功能阻塞。

作者已补成员初值、constinit与const查询，删除复制/移动操作。最终make和make test均0且无警告；临时编译期探针确认不可复制构造/赋值、不可移动构造/赋值、平凡析构以及const对象可查询，最终内核无未解析符号，PMM对象无动态初始化节。证据同目录target-final.log、host-final.log、constraints.cpp/log及final-constraints.json；未改作者实现，未重复QEMU。

作者的删除赋值声明采用按值复制参数与const右值参数，虽与建议签名不同，实际禁止复制/移动赋值的约束经探针成立，不作为新一轮修改要求。

### 功能梳理与理解题（已集中审查）

现在C调用方仍通过原pmm_*符号进入；薄封装把操作交给同一ppa实例，成员函数用this访问五项私有状态。封装改变状态的组织方式，页的含义、LIFO复用与清零、调用者释放责任均保持。

constinit保证ppa元数据在静态初始化阶段就准备好；它不使对象只读，也不替代boot_init_pmm调用的pmm_init。init才设置实际可分配区间。available_pages的尾部const承诺查询不修改对象，private限制外部直接改状态。

1. 如果复制一个分配器，两个对象都保留同一物理区间和游标，随后各分配一次，会出现什么问题？为什么禁止复制比分配前多检查一次指针更根本？
2. ppa成员初值为零且使用constinit，为什么启动还要pmm_init？constinit与available_pages() const中的const有什么区别？
3. C文件vm.c不知道PhysicalPageAllocator的定义，为什么仍能调用pmm_alloc_page？参数、this与返回值在C封装和成员函数之间如何流转？

题干修正：原题把“constinit”与“初始状态为零”连在一起，容易误导。零值来自成员初值/初始化表达式，constinit约束静态初始化，不指定零值。以下保留作者原答；修正版为协作者补充，不冒充作者独立回答。

**第一题作者原答：**

> 使用另一个对象的在第一个对象释放释放了，却还持有原本数据，要么悬置要么就是使用其他数据造成bug

**审查：** 已指出独立状态管理同一物理资源会产生错误归还、陈旧访问等后果；题目中更早的问题是两份相同游标会重复分配同一页，不需要等到释放才出错。

**修正版：** 若分配器A和B的游标都为P，A分配P并前进自己的游标，B仍认为P未分配，也会返回P。两个使用者于是持有同一张物理页并互相覆盖。回收链表头若也被复制，链表节点与两份计数还会失去一致性。禁止复制可以从类型边界阻止产生这种分裂的管理状态，而不是仅检查返回指针是否非空。

**第二题作者原答：**

> constinit是全部初始化为0，但是实际上我们的初始化还需要使用其他数据，使用单个constinit作用有限，而函数末尾的const是保证函数内部不更改类内变量的

**审查：** 已理解启动时还要填真实区间，以及const查询不改普通成员；需要纠正constinit本身不是清零操作。

**修正版：** 成员的 `= 0`、`= nullptr` 决定本实例的初值；constinit保证静态初始化，拒绝需要动态初始化的写法，也可以初始化非零值。pmm_init在启动时接收真实边界并建立可用区间，才能正常分配。成员函数尾部const使this指向const对象，禁止经this直接修改普通非mutable成员；它不保证没有所有副作用，也不禁止通过成员指针修改所指内存。

**第三题作者原答：**

> extern "C"转化为C可以理解的形式了，而且本身就只是一些二进制文件而已，在ppa.alloc_page里this一般是指ppa

**审查：** 已抓住C互操作边界和this对象；extern C应说采用C语言链接，不是把实现转换为C语言。对本次 `ppa.alloc_page()` 调用，this确定为&ppa。

**修正版：** C调用方根据pmm.h里的声明生成对pmm_alloc_page符号的调用；C++实现采用C语言链接，保留该符号供现有C调用方链接。函数体仍是C++，其中调用ppa.alloc_page时，编译器把&ppa作为成员函数的隐式this参数；成员函数访问该实例状态，返回的void指针经薄封装原样交回C调用方。两边是目标机器代码还不够，仍须匹配符号、调用约定与参数/返回类型。extern C本身不限制内部符号可见性，也不自动把任意C++类变为C兼容类型。

本批功能、类约束验证、全路径梳理和三题审查在此完成。下一批尚未下发，不自动开始地址类型或页表迁移。

### 目标与调用关系

由作者完整实现一个维护物理页分配状态的C++类，继续服务原来的四个C接口。可观察结果与原内核一致：PMM初始化、分页、用户装载与退出都成功，回收页能复用且重新清零。

```text
boot_stages.c / vm.c / user_memory.c / 宿主测试
                         ↓ 原有C ABI
pmm_init / pmm_alloc_page / pmm_free_page / pmm_available_pages
                         ↓ 薄转发
一份PhysicalPageAllocator实例
                         ↓ 成员函数维护
物理区间、顺序游标、回收链表、回收页数
```

原来的五项全局分配器状态收进对象：`next_free_page`、`allocatable_start`、`allocatable_end`、`free_list`、`recycled_page_count`。状态只能由成员函数维护，不能留一份旧全局变量再新增一份类成员。

### 三处修改与接口边界

1. 将 `kernel/mm/pmm.c` 改为 `pmm.cpp`，在该文件匿名命名空间内定义 `PhysicalPageAllocator`、内部页节点和唯一实例。当前没有其他C++模块需要直接使用类，不新增公开.hpp或目录。只有类定义与薄C封装，不引入第二份分配算法。
2. 在 `include/mm/pmm.h` 的原四个函数声明外围补 `__cplusplus` 条件下的extern C保护；宏、函数名、参数与返回类型保持现状。C调用者与宿主 `--wrap=pmm_free_page` 继续有效。
3. 宿主 `test/CMakeLists.txt` 把 `pmm.c` 路径换为 `.cpp`。已有C++20严格目标属性继续使用；Makefile现有 `%.o: %.cpp` 会生成原名 `kernel/mm/pmm.o`，通常无需改OBJS或再写规则。

C接口保持原样：

```c
int pmm_init(const void *memory_start, const void *memory_end);
void *pmm_alloc_page(void);
int pmm_free_page(void *page);
unsigned long pmm_available_pages(void);
```

类的公开职责建议为 `init`、`allocate_page`、`release_page`、`available_pages() const`，其余状态和辅助方法私有。成员方法名由作者保持一致即可；不要把整个类放进extern C块。

薄C入口只把参数转发给同一个实例并返回结果。例如调用关系为 `pmm_alloc_page → g_page_allocator.allocate_page`；分配算法放在成员方法中，不在C封装再复制一遍。

### 存储、初始化与生命周期

分配器实例本身是少量元数据，存放在静态存储区，不从PMM或堆申请。默认状态五项为零/空，启动时 `boot_init_pmm` 仍调用 `pmm_init(__kernel_end, device_tree)`，由init设置真实物理区间。

使用成员常量初值和可常量求值的默认构造，实例可写成 `constinit PhysicalPageAllocator g_page_allocator{};`。constinit要求静态初始化，编译器会拒绝需要动态初始化的写法；它不代表对象不可修改，也不会代替运行时init。构造阶段不扫描RAM、不申请页、不调用UART或SBI。

类应禁止复制和移动，防止复制出两份管理同一物理区间的游标/链表。保持平凡析构，不让析构自动释放所有已分配页；页是否仍被页表或任务使用由调用者管理。

原PMM允许重新init，现有宿主场景依赖这个能力。保留“有效init重置为新区间，清空回收状态”的语义；先计算并验证新边界，再提交成员状态，错误返回不改旧状态。重新init只用于启动或受控测试重置，运行中不能用它抹掉活跃页的记录。本批不另加一次性init限制。

### 必须保持的算法与不变量

- 起点向上页对齐、终点向下页对齐，只管理区间内完整4096字节页；内存上界仍由现有启动参数决定，不在本批加入DTB解析。
- 始终满足 `allocatable_start <= next_free_page <= allocatable_end`；顺序游标只前进，释放时不减游标。
- 优先从回收链表摘头，链表为空才顺序分配；回收继续头插，保持LIFO。摘头时先读并保存next，再清零该页，防止把链表链接清掉。
- 两种分配路径返回前都清零整页；耗尽返回NULL。
- 释放仍拒绝NULL、非页对齐、区间外、尚未顺序分配和重复归还的页；拒绝时状态不变，成功返回0，失败返回-1。
- 可分配页数仍是 `recycled_page_count + (allocatable_end - next_free_page) / PAGE_SIZE`。节点数量等于回收计数，回收链表不含仍在使用的页。
- 回收节点保存在空闲页内部，不另外申请节点或引入容器。可把 `FreePage` 定义为类的私有节点类型，字段只有next。
- C++会要求明确的指针转换：void指针到对象指针可以用static_cast，整数地址与指针之间用reinterpret_cast。不因改语言而改变页地址语义。
- 类封装本身不提供并发保护。本批保留单hart、现有调用时机约束，不同时加锁、改中断状态或改变调度。

### 验收范围与分工

作者整批实现后交协作者集中审查。无需新写测试框架或补完整故障矩阵。P0由协作者承担：

| 优先级 | 验证 | 明确预期 |
| --- | --- | --- |
| P0 | 原宿主PMM基本场景与确定性压力测试 | 8192页、20000次操作通过；优先复用、清零、重复/非法释放拒绝、计数恢复正确 |
| P0 | 现有VM、用户内存与复制回归 | make test通过；原释放失败注入和重试仍生效，C符号未绕开 |
| P0 | 真实目标构建和符号检查 | PMM由Clang按当前ISA/ABI编为C++，四个C符号仍存在，内核无未解析符号，无动态全局构造/意外C++运行时依赖 |
| P0 | 一次正常QEMU观察 | PMM/分页/用户装载成功，yield/R/sleep/X/Hello/Data OK/exit，退出后A与至少两次S timer继续 |
| P1可选 | 非法init不破坏已有状态 | 只需在宿主增加一个小场景，作者可自行练习或跳过；验证旧区间仍能正常分配 |

命令沿用：

```sh
make test
make -B
riscv64-linux-gnu-nm -g --defined-only kernel/mm/pmm.o
riscv64-linux-gnu-nm -u kernel.elf
timeout 8s make qemu
```

不需要make clean。QEMU返回124仍可能只是限时结束；按完整用户输出和退出后内核继续判断。构建、对象元数据、宿主命令与资源行为由协作者检查，不要求作者维护新脚本。

这批的新增学习点是对象状态、this、成员访问控制、常量初始化和C封装。先由作者自行组织类与实现，不提供整份成品框架；遇到具体语法或设计疑问再解释。完成后把本批功能梳理及约三题继续合并到本文，不新增零散答题文件。

## 已完成：让段复制模块真正以C++进入内核

目标：由作者将 `kernel/user/user_image.c` 改为 `.cpp`，保持原C接口和算法，由C代码调用它，最终链接进真实RISC-V内核并运行。重点是编译、链接与ABI；状态封装、类、所有权设计留到后批。

### 当前验收（2026-10-26）

作者已完成.cpp、头文件C ABI、Makefile和CMake源路径。make -B与make test均成功且无警告，目标原C符号与ISA/ABI正确，最终内核无未解析符号，模块无意外C++运行时或动态初始化依赖。一次QEMU正常完成yield/R/sleep/XHello/Data OK/exit，退出后两次S timer，限时返回124符合常驻内核任务行为。
证据：`../.cache/cpp-pilot-review-pq6dlz62/`；七个源配置前后哈希保持，协作者未代改实现。

作者随后已给tinyos_test_kernel补齐CXX_STANDARD 20、CXX_STANDARD_REQUIRED YES与CXX_EXTENSIONS NO。make test复验0且无警告，编译数据库确认该实现使用-std=c++20；证据同目录host-strict-cpp20.log/json。配置收尾完成，未重复QEMU，功能梳理与三题尚未进行。

### 已核对的工具链

2026-10-26 PATH中没有 `riscv64-linux-gnu-g++`，GCC查询cc1plus也未解析出安装路径。已有Clang 21.1.8；临时C++小程序以如下目标参数成功生成ELF64/RISC-V对象，flags为RVC/soft-float ABI，导出未改名的C符号。此探针只证明目标编译可用，尚未验证本模块与整个内核的混合链接。

本批使用 `clang++ --target=riscv64-unknown-elf`，C与汇编继续用现有GCC规则，最终继续用现有RISC-V ld链接。共同使用 `ARCH_FLAGS` 中的ISA、lp64、medany及禁用relax设置。

### 作者整批修改范围

1. **实现文件**：将 `kernel/user/user_image.c` 改为 `user_image.cpp`，保留参数校验、返回值、复制、补零和失败零写入行为。可将void指针转换写成static_cast；本批不要求为纯函数包装类，不引入堆、容器或动态全局对象。
2. **公共头**：`include/user/user_image.h`继续供C和C++共同包含。用 `#ifdef __cplusplus` 包围 `extern "C" { ... }`，使函数声明在C++下采用C语言链接；实现先包含这个头，定义就沿用该声明的链接方式。普通C调用者无需更名或改参数。
3. **根Makefile**：新增明确的目标C++编译器、C++选项和 `%.o: %.cpp` 规则。现有OBJS仍为 `kernel/user/user_image.o`，不能把旧.c和新.cpp同时编进内核。继续沿用DEPFLAGS，C与汇编规则保持各自语言。
4. **宿主CMake**：将tinyos_test_kernel源列表中的该路径改为.cpp，给这个库的C++部分明确设置C++20、必须满足标准、关闭扩展；原C源仍按C11编译。现有16个段复制场景继续测试同一实现，不另外复制算法。仅C++适用的编译选项用语言条件限定。

目标C++选项参考（ARCH_FLAGS继续复用）：

```text
-std=c++20 -Wall -Wextra -Wpedantic -O0 -g
-ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
-fno-stack-protector -fno-pie
-fno-unwind-tables -fno-asynchronous-unwind-tables
-Iinclude
```

禁止异常、RTTI等并不意味着自动获得完整C++运行时；本模块没有这些需求。保持直接ld链接，不额外链接libstdc++。如出现未定义辅助符号，应检查原因，不能靠忽略链接错误通过。

`test/c_api.hpp`已有外层extern C，与头内保护兼容；该文件目前“same C objects”的注释可相应更正为相同内核实现。其余启动、ELF装载、PMM与调度代码本批不改。

### 集中验收与协作者承担的P0

作者完成上述修改后集中审查，例行复验由协作者承担：

- `make test`通过既有宿主场景，包括段复制的16个场景；确认CMake实际用C++编译该实现。
- `make -B`通过真实目标编译与链接，确认.cpp由目标Clang处理，其余C仍由GCC处理；不需要make clean。
- `riscv64-linux-gnu-nm kernel/user/user_image.o`导出 `user_image_load_segment`，没有C++改名；`nm -u kernel.elf`无未解析符号。检查对象ISA/ABI及没有意外C++运行时、动态初始化依赖。C端真实调用参与链接，不能仅靠宿主测试判定成功。
- `timeout 8s make qemu`保持yield/R/sleep/X/Hello/Data OK/exit，退出后时钟与A继续；124仍是限时结束观察。
- 保留现有错误输入拒绝与失败不写目标的测试即可，不新增故障矩阵。若暴露真实回归，再补针对性的复现。

临时工具链探针、正式混合链接与运行、严格C++20库配置现均已通过。作者选择继续PMM封装；编译/链接边界的理解可合并到本批梳理，不用再停留于工具链试点。

## 历史基线（2026-10-22）

2026-10-22：TinyOS 构建成功，61 个具体宿主场景通过，另有一条 PASS 汇总。
QEMU 启用 Sv39 后仍停在 `Demo user memory setup failed.`。
当日证据在 `../.cache/code-audit/2026-10-22/`。
现有 C++/Rust 实验只验证过宿主算法和部分 freestanding 编译，尚未接入内核。
当时PATH未找到RISC-V g++；本批已补核对Clang可用性，见上方。

## 临时代码如何分批收尾

| 当前位置 | 问题或限制 | 建议处理时机 |
| --- | --- | --- |
| `kernel/boot/private/user_prepare.c` | 原映射前查询和BIN切分问题已修正 | ELF批已完成，保留失败回滚 |
| `kernel/boot/private/boot_stages.c` | 已统一ELF起点、长度和读取的入口 | ELF批已完成 |
| `kernel/boot/private/` | 演示地址策略与通用装载操作混在启动私有代码中 | ELF 路径恢复后，再按职责提取；先保留受限两段布局 |
| `UserMemory` | 固定 code/data/stack 字段，共享 root | 学到独立地址空间后，另批设计区域与所有权描述 |
| `kernel/task/sched.c` | 三个静态任务槽、静态栈与全局状态 | 进入任务生命周期改造时处理 |
| `kernel/mm/pmm.c` | 以 DTB 地址保守界定内存上界 | 需要支持实际内存布局时另批加入 DTB 内存节点解析 |
| 测试脚本、编译数据库 | 旧注入路径、旧 prepare 签名和 bin 假设未完全适配 | 修复当前批所需入口；不把完整历史矩阵当推进前提 |
| `experiments/cpp-rust/Makefile` | 仍引用目录整理前的 C 源码和头文件路径 | 复用实验前修正路径并重新验证，不能只引用旧 PASS |

这些限制不要求一起修完。每批只解决一个概念，避免同时更换语言、算法和资源模型。

## 建议实施顺序

### 1. 恢复受限 ELF 装载（已完成）

按现有 `USER_ELF_LOADING.md` 收尾：校验 layout 的段地址与权限策略，使用真正 ELF
字节及段 offset/filesz/memsz 复制和补零，入口来自 layout，保留现有回滚与回收行为。
这一批仍用当前语言与固定布局，不同时增加多页段或独立地址空间。

P0 由协作者承担：正常用户程序与退出后时钟继续；核对装载字节/BSS/权限；
一条拒绝路径与资源不变检查；复用现有宿主场景。其他历史探针按适用性单独记录。
作者完成核心实现后，集中梳理执行路径并沿用约三道理解题。

### 2. 试通一个真实 C++ 内核模块（已通过）

建议首试 `kernel/user/user_image.c` 的段复制函数：当前 45 行，无全局状态、
无分配或所有权转移，已有 16 个宿主场景，适合隔离验证 C++ 编译、链接与 C 调用边界。
这一步的目标是让一个 C++ 对象真实进入 QEMU 内核；先保留函数接口与行为。

先核对可用目标编译器，并与现有 C 对象统一 ISA、ABI、代码模型、relax 和链接约定。
宿主测试继续调用同一实现；核对目标链接的未解析符号、运行库依赖，再跑最小启动演示。
不因试点而新增通用容器、堆或完整 C++ 运行时。

### 3. 用 C++ 封装 PMM 状态（已完成）

把顺序分配游标、区间和空闲链表归入一个明确初始化的对象，暂时保留现有 C ABI
入口供 C 调用者使用。保持页清零、复用顺序、错误码与重复释放检查；底层只维护一份状态。
这是第一个以状态封装改善设计的模块，与上一批的工具链试点分开。

现有实验的 PMM 使用外部 Page 数组并返回页索引，而真实内核使用物理区间和页指针。
可以参考实验中的组织方式，不能直接替换。实例存储、初始化时机与中断访问约束要写清。

### 4. 地址与页表整体改造，再组织用户地址空间（当前批）

#### 2026-10-28：作者授权搬迁后的实际实现

作者明确要求协作者直接搬迁已有函数逻辑，本次在此授权范围完成实现，未迁移用户地址空间或调度。

| 文件 | 当前职责 |
| --- | --- |
| [address.hpp](../include/mm/address.hpp) | Addr的VA canonical/PA56位检查、页对齐与checked偏移；PageNum的27位VPN/44位PPN、索引和高VA还原 |
| [page_table_entry.hpp](../include/mm/page_table_entry.hpp) | 新增单uint64值类PageTableEntry，集中编码、PPN/PA提取、flags、valid/leaf判断 |
| [vm.cpp](../kernel/mm/vm.cpp) | 原十二个C入口保持；软件walk/map/query/reclaim实际使用新类型，构表与MMU架构操作继续分离 |
| [test_vm.cpp](../test/test_vm.cpp) | 增加类型隔离、高VPN、checked失败输出不变、raw PTE和宽松C codec回归，原场景保留 |

原C接口的宽松codec契约保留：vpn_index对非canonical值仍按位提取，非法level返回512；
make_pte截取44位PPN并只保留flags低8位，映射入口仍严格验证。raw PTE构造不清除非法/预留位；
query/unmap对最终非法叶的历史错误码差异保持。Sv39PageTable仍为512个原始uint64条目，不把值类数组强转为表页。

Addr与PageNum原始整数构造保留原始值，是否有效由is_valid显式判断，from_floor作为按位转换只保留支持的页号位。
外部地址先校验再使用；try_add/try_sub/try_page_up、try_from_ceil与try_offset失败返回false且不改输出。
普通向上对齐/算术接口在违反契约时trap。整数地址、页号与指针转换不执行页表翻译，页表页访问仍依赖现有恒等映射。

验证：make -B和make test verbose均0、无警告；原宿主回归与新增两组场景通过。
一次timeout 8s make qemu观察yield/R/sleep/XHello/Data OK/exit各一次，退出后两次S timer，无setup失败/Trap。
返回124为常驻内核任务运行至限时。目标十二C符号完整，最终内核无未解析符号，vm对象无动态初始化节。
证据 `../.cache/address-migration-n6kx646_/`。vm.h/CMake/Makefile哈希未变；未清理、暂存或提交。

后续先梳理新旧接口对应关系再按作者需要推进用户地址空间。本次不把旧query的create=0共享遍历改造为独立const遍历，
该旧路径实际只读；也不改变PMM、共享root、用户固定三页、trap或任务生命周期。

以下保留前期设计讨论与历史问题；其中“尚未接入”已被本节实际结果更新。

2026-10-27地址头集中检查：作者已新增address.hpp中的Addr<Tag>/PageNum<Tag>。当前make和原make test通过，但vm.cpp/vm.h尚未包含新头，不能据此认为新类型通过。单独目标实例化from与比较时分别复现assert未声明、默认<=>缺std::strong_ordering；当前目标又无compare标准头。临时宿主探针仅在探针中补cassert/compare隔离算法问题，证实地址+=/-=未修改原值、page_up(UINTPTR_MAX)回绕0、高VA的VPN未规范化及正确VPN转VA缺符号扩展；原源码未改。

集中待修：复合赋值修改自身并返回引用；使用目标可用的显式比较代替默认<=>；对齐失败采用可用的显式返回策略，不依赖尚无裸机实现的assert；区分27位VPN/44位PPN以及canonical VA/56位PA；page_up与地址/页号增减明确越界失败；补两个页号别名和VPN index后再实际接入页表。from要求对齐与floor向下取整要区分，不把缺少的接口当作当前实现已有功能。

反例：高VA0xffffffc000001000应得到VPN0x4000001，当前得到0xffffffc000001；正确VPN0x4000001应恢复前述高VA，当前只左移得到非canonical的0x4000001000。当前from→to偶然往返不代表页号表示正确。页号0加-1、PA页号1<<44及低VA上边界向上对齐也须处理。地址差小负值在C++20并非必然错误，但超出ptrdiff_t表示范围仍需策略。

证据：../.cache/address-review-frn_tl28/，包含目标错误、临时probe源码/输出及源码哈希核对；未改作者实现、未运行QEMU、未增加正式测试矩阵。

2026-10-26作者希望扩大为完整模块，避免逐个地址类型/函数小批迁移。2026-10-27已开始vm.cpp成员操作迁移；当前采用下述直接成员方案，地址四类型与PTE类型仍待实现，整批尚未验收。

**下一批整体：地址类型、PTE与页表。** 一起处理VirtAddr/PhysAddr/VirtPageNum/PhysPageNum、PTE编解码/检查、三级map/query/unmap/空表回收，以及现有MMU激活/刷新接口的C++边界。vm.c可迁为vm.cpp，vm_arch可按所定接口迁移，架构操作继续独立于宿主可测试的软件构表逻辑。一次整批实现和集中验收，不要求每引入一个类型就停下再测一轮。

- 公共C接口保持可用，C调用方从同一入口进入，类型转换集中在边界；不同时改所有启动/调度文件的语言。
- Sv39PageTable继续是硬件读取的512个uint64_t项、大小4096字节。作者已选择直接为该结构体添加C++ map/query/unmap/reclaim成员声明，定义在vm.cpp，原C函数检查并转发。不再另建PageView/PageTableView，避免与成员操作重复；旧View提议已被本方案替代。
- 成员声明在vm.h的__cplusplus条件区，全部公开C函数声明用C链接保护；不增加虚函数、额外管理成员或析构释放。不把PMM的唯一实例/禁止复制设计机械套给表页。
- 后续地址类型可集中在一个C++专用include/mm/address.hpp中供vm.cpp及后续用户空间模块复用，PTE类型按实际接口安排。不必为每个值类型各开一份文件。这是同一整体批次的余下部分，不另拆小批验收。
- 如果未来确有必要额外提供借用视图，其可复用声明应在.hpp、实现放.cpp，不能只藏于vm.cpp匿名命名空间；当前不预建。
- map不隐式启用页表，unmap不擅自释放内容页；保持软件页表、satp与TLB操作的明确边界和既有调用时序。
- 保留查询页内偏移、查询不分配、4KiB叶策略、错误码、部分构表失败后的空表回收、释放失败时父项恢复、原页表布局测试与--wrap拦截。
- 当前共享root、恒等映射访问页表、用户固定三页策略继续作为本批运行基线。独立root、跳板、trap和调度切换属于新运行机制，另行设计，不绑进语言改造。

**随后整体：用户地址空间与归属。** 将当前UserMemory、准备/复制用户段、区域权限、用户内存访问和回收归属组织成AddressSpace/区域管理接口。围绕已验证的行为设计拥有和借用关系，先明确支持的区域数量/分配方式，再决定是否扩展布局。保留可返回错误并可重试的显式release，不让析构吞掉错误或提前清空仍持有的资源。

**统一组织内存子系统。** 完成上述两层后再收拢对外接口与模块目录。MMU本身是CPU硬件；软件统一的是物理分配、地址空间、页表以及激活/刷新接口，不能因此把所有职责塞进单个MMU类。整体依赖为AddressSpace → PageTable → PMM，架构MMU接口负责satp/TLB，任务持有或借用明确的地址空间。

P0沿用相关原宿主场景和一次真实启动，由协作者承担；按实际新增类型/接口补必要检查，不新增完整历史故障矩阵。完整文件清单、类接口和验收条件在正式开批时一次给齐，较远阶段仍不预写大框架。

### 5. 任务与调度

在地址空间生命周期稳定后，再封装任务表、状态迁移、栈和调度策略，
处理动态任务与后续进程功能。trap 汇编现场布局与调度对象的内部结构分开维护。

## C/C++ 边界先确定这些规则

- 汇编调用入口、syscall ABI、共享现场结构及必要的模块互操作采用明确的 C ABI。
  C ABI 入口可以由 C++ 实现，不要求所有入口永久保留 C 源文件。
- C/汇编共用头只暴露兼容布局、常量和函数声明；C++ 实现细节留在私有头/源码。
  汇编使用的偏移继续有静态断言约束，按需补 `__cplusplus`/`extern "C"` 边界。
- C 与 C++ 源文件分别设置编译规则，旧 C 文件继续按 C 编译。
- 首批采用 freestanding、关闭异常与 RTTI；不自动链接 libstdc++。
  是否需要编译器运行库辅助函数，应检查实际目标符号，而不是一概禁止或忽略。
- 首批避免动态全局构造、依赖退出执行的析构、线程安全局部静态初始化等启动期隐式依赖。
  编译开关不能代替明确的初始化顺序；对象显式 init，分配失败显式返回。
- 在内核堆和失败策略确定前，首批不使用依赖堆的容器或普通 new/delete。
  语言版本与可用语言特性、库功能分别约定，不因选择现代 C++ 就假定完整标准库可用。
- 所有权转移要明确；独占页或地址空间对象应禁止隐式复制，移动后源对象为空。
- 当前 `user_memory_release` 支持部分成功后重试，必须保留尚未释放的资源与状态。
  初期继续显式返回清理结果；不得在析构中吞掉错误、清空未释放资源，或释放活动页表。

## 每批更新方式

只将紧接下一批的条目细化为文件、接口、核心不变量、可观察结果和 P0 验证。
实现验收后再写实际选择、验证结果与下一项；较远阶段保持草案，允许按学习结果调整。
目录只为实际模块边界调整，不为未来概念预建大框架。

ELF批已由作者实现并通过验证；随后单模块C++正式实现也已由作者接入并通过功能验证；协作者承担例行复验，未安装工具链或代改功能代码，也未新增测试矩阵。

## 参考

- [当前 ELF 装载任务](USER_ELF_LOADING.md)
- [独立语言实验及限制](../experiments/cpp-rust/README.md)
- [GCC freestanding 环境说明](https://gcc.gnu.org/onlinedocs/gcc/Standards.html)
- [GCC C++ 编译选项](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html)
