# BATCH04：用户内存准备、访问与释放

状态：已通过。实施时间：2026-10-28；整理：2026-10-29。

将用户内存准备、类型化安全复制及显式释放迁入 C++，统一重复页操作，并补齐现有 C 接口的 C++ 兼容性。

本批结束时装载主体仍在 boot/private；后续迁移与布局配置见 [BATCH05](BATCH05_ELF_LOADER.md)。本文保留这一阶段的路径和资源契约。

导航：[批次索引](README.md) · [总路线图](../TINYOS_EVOLUTION.md)

本文的裸源码路径以仓库根目录为基准；可点击的相对链接以本文件为基准。过程性记录保留原时点，当前状态以验收结论为准。

## 剩余C接口的C++兼容性收尾（2026-10-28）

作者授权继续修改剩余接口。已为SBI、trap、调度、syscall、timer、UART、ELF解析及用户汇编包装的C函数声明补条件式extern C，类型与C++模板接口留在块外；syscall声明仍在非汇编分支内。trap.h的布局断言分别使用C11 _Static_assert/C++ static_assert，user_api.h的退出声明分别使用_Noreturn/[[noreturn]]。此处只处理现有接口的语言兼容，装载逻辑位置和模块职责收拢留到下一批。

构建与宿主测试通过、无警告；16个C接口头在宿主和RISC-V的C11/C++20模式下逐个严格检查，共64项通过。C++探针实际引用26个入口，全部保留原C符号并匹配内核/用户ELF导出，最终内核无未解析符号。原C和用户汇编调用参与正常重编。证据.cache/c-interface-review-w3msuci7/含改前快照、两处旧C++语法失败和最终检查；最初独立探针漏加freestanding且宏头产生空翻译单元，调整探针配置后全部通过，不修改工具链。此次未重复运行QEMU、未clean或提交。

## 实现与重复操作简化（2026-10-28）

作者已完成三实现改.cpp、UserMemory成员释放及tinyos::copy_readable_user类型化入口。随后明确授权协作者简化重复页操作，已在原文件完成：

- prepare用局部PreparedPage数组描述代码/栈/数据页，分阶段循环完成分配、ELF内容复制、映射、flush和失败回滚。栈没有ELF段，只使用PMM清零页。mapping_attempted与mapped仍分开，正常顺序为代码/栈/数据，回滚unmap/flush/reclaim保留栈/代码/数据的原顺序，free仍为代码/栈/数据。
- release用局部PageReference数组绑定实际成员的地址，以span选择两页或三页；各阶段分别循环，成功释放立即清空实际成员，保留失败重试和原错误码。
- access由try_add统一检查起止地址与溢出，直接写入目标span，去掉重复canonical检查及多余指针转换；仍逐页查询并检查V/R/U。
- boot_private.h补齐C函数保护，vm_arch.h将保护范围覆盖全部C函数；prepare映射继续经过外部sv39_map_page，原注入入口保留。

正式make -B与make test verbose通过、无警告；原有复制/释放/部分失败重试测试通过。一次QEMU正常yield、sleep、Hello、Data OK、exit后仍有两次S timer，限时返回124为预期。三个C入口保持原名，最终内核无未解析符号，新对象无init/fini数组。

额外在临时宿主探针中对照简化前后prepare：仅移除RISC-V fence.i指令，不改其余算法，验证入口拒绝、部分分配回滚、已有映射冲突、flush失败回滚及正常准备/释放；页数和输出描述符合预期，记录的回滚事件次序一致。架构行为以正式QEMU结果为准。证据统一在.cache/page-loop-simplify-38luo4kk/，含改前快照、探针、前后日志及主构建/宿主/QEMU记录。本次未clean、暂存或提交。

## 功能梳理与三题审查（2026-10-28）

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

## 目标与文件范围

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

## 接口与职责

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

## 资源生命周期必须保持

- 准备阶段先检查ELF布局再分配，成功后才提交memory_out；任务创建成功才接管并清空调用者描述。
- root是借用，三张内容页归描述管理；内核根表与静态内核栈继续保留。
- release显式返回错误：解除映射 → flush → 释放内容页 → 回收空中间表。释放每页成功就清其指针，整体成功后才把描述置空；失败保留后续重试所需状态。
- 内部继续走现有sv39_unmap_page、pmm_free_page等可注入边界；准备工厂保留原外部map入口，不为成员调用绕过原--wrap测试。
- 析构不自动释放活动页表或吞清理错误。本批不增加RAII资源回收、内核堆、独立root、trap/调度切换。

## C改为C++时一次处理的语法差异

1. void指针转换需显式；使用现有Addr/PTE等类型，但不改变原返回码。
2. prepare的goto cleanup不能跨过load_reslut和const flags等变量的初始化；把相关声明放到首次goto前，或用清楚的局部作用域组织，不删掉失败清理。
3. C++20指定初始化器必须遵守UserMemory字段声明顺序；当前工厂把data_page写在code_address之前，应调整初始化列表而不是改结构体布局。
4. C++清空用UserMemory{}，C文件仍可保留(UserMemory){0}。boot_stages/sched本批继续用C，不能把C++语法写进去。
5. 头文件只在公开C函数声明处使用条件式extern C，C++类型/成员声明放在外面。所有C++调用到的架构/UART入口都需核对，不能只改新实现文件。

## P0与交付方式

作者把以上作为一整批完成后集中检查，不逐函数增加作业。P0由协作者承担：

- make -B与make test通过，目标C++/宿主C++都编译到同一实现；原C调用方仍能链接。
- 复用原user_access的范围/权限/跨页场景及UserMemory正常/重复释放、归属拒绝、部分失败重试；一条既有拒绝路径确认状态不变。
- 核对C ABI、七字段布局、没有动态初始化或新运行时依赖，原失败注入及事件顺序保持。
- 一次QEMU正常完成yield/R/sleep/X/Hello/Data OK/exit，退出后A和至少两次S timer继续。限时124仍按原含义判断。

旧prepare QEMU脚本可能仍引用.c及历史签名/页数，协作者按实际需要临时适配，不要求作者先修完整矩阵。
本批不修旧prepare清理失败时局部归属的历史限制，不把语言迁移扩成整个rollback重设计。
完成后功能梳理与约三题继续合并本文。之后再收拢内存模块对外接口，独立用户地址空间另行设计。
