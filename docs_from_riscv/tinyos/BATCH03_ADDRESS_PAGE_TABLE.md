# BATCH03：地址、页号、PTE 与页表

状态：已通过。实施时间：2026-10-27—2026-10-28；整理：2026-10-29。

完成地址与页号类型、PTE 值类及现有页表操作接入，保留硬件表页布局、C 接口与原始编解码契约。

下面历史讨论中的“尚未接入”“下一批”等字样描述当时状态；以“实际实现”中的通过结果为准。

导航：[批次索引](README.md) · [总路线图](../TINYOS_EVOLUTION.md)

本文的裸源码路径以仓库根目录为基准；可点击的相对链接以本文件为基准。过程性记录保留原时点，当前状态以验收结论为准。

## 2026-10-28：实际实现与验证

作者明确要求协作者直接搬迁已有函数逻辑，本次在此授权范围完成实现，未迁移用户地址空间或调度。

| 文件 | 当前职责 |
| --- | --- |
| [address.hpp](../../include/mm/address.hpp) | Addr的VA canonical/PA56位检查、页对齐与checked偏移；PageNum的27位VPN/44位PPN、索引和高VA还原 |
| [page_table_entry.hpp](../../include/mm/page_table_entry.hpp) | 新增单uint64值类PageTableEntry，集中编码、PPN/PA提取、flags、valid/leaf判断 |
| [vm.cpp](../../kernel/mm/vm.cpp) | 原十二个C入口保持；软件walk/map/query/reclaim实际使用新类型，构表与MMU架构操作继续分离 |
| [test_vm.cpp](../../test/test_vm.cpp) | 增加类型隔离、高VPN、checked失败输出不变、raw PTE和宽松C codec回归，原场景保留 |

原C接口的宽松codec契约保留：vpn_index对非canonical值仍按位提取，非法level返回512；
make_pte截取44位PPN并只保留flags低8位，映射入口仍严格验证。raw PTE构造不清除非法/预留位；
query/unmap对最终非法叶的历史错误码差异保持。Sv39PageTable仍为512个原始uint64条目，不把值类数组强转为表页。

Addr与PageNum原始整数构造保留原始值，是否有效由is_valid显式判断，from_floor作为按位转换只保留支持的页号位。
外部地址先校验再使用；try_add/try_sub/try_page_up、try_from_ceil与try_offset失败返回false且不改输出。
普通向上对齐/算术接口在违反契约时trap。整数地址、页号与指针转换不执行页表翻译，页表页访问仍依赖现有恒等映射。

验证：make -B和make test verbose均0、无警告；原宿主回归与新增两组场景通过。
一次timeout 8s make qemu观察yield/R/sleep/XHello/Data OK/exit各一次，退出后两次S timer，无setup失败/Trap。
返回124为常驻内核任务运行至限时。目标十二C符号完整，最终内核无未解析符号，vm对象无动态初始化节。
证据 `../../.cache/address-migration-n6kx646_/`。vm.h/CMake/Makefile哈希未变；未清理、暂存或提交。

后续先梳理新旧接口对应关系再按作者需要推进用户地址空间。本次不把旧query的create=0共享遍历改造为独立const遍历，
该旧路径实际只读；也不改变PMM、共享root、用户固定三页、trap或任务生命周期。

## 历史设计与修正过程

以下保留前期设计讨论与历史问题；其中“尚未接入”已被前面的实际结果更新。

2026-10-27地址头集中检查：作者已新增address.hpp中的Addr<Tag>/PageNum<Tag>。当前make和原make test通过，但vm.cpp/vm.h尚未包含新头，不能据此认为新类型通过。单独目标实例化from与比较时分别复现assert未声明、默认<=>缺std::strong_ordering；当前目标又无compare标准头。临时宿主探针仅在探针中补cassert/compare隔离算法问题，证实地址+=/-=未修改原值、page_up(UINTPTR_MAX)回绕0、高VA的VPN未规范化及正确VPN转VA缺符号扩展；原源码未改。

集中待修：复合赋值修改自身并返回引用；使用目标可用的显式比较代替默认<=>；对齐失败采用可用的显式返回策略，不依赖尚无裸机实现的assert；区分27位VPN/44位PPN以及canonical VA/56位PA；page_up与地址/页号增减明确越界失败；补两个页号别名和VPN index后再实际接入页表。from要求对齐与floor向下取整要区分，不把缺少的接口当作当前实现已有功能。

反例：高VA0xffffffc000001000应得到VPN0x4000001，当前得到0xffffffc000001；正确VPN0x4000001应恢复前述高VA，当前只左移得到非canonical的0x4000001000。当前from→to偶然往返不代表页号表示正确。页号0加-1、PA页号1<<44及低VA上边界向上对齐也须处理。地址差小负值在C++20并非必然错误，但超出ptrdiff_t表示范围仍需策略。

证据：../../.cache/address-review-frn_tl28/，包含目标错误、临时probe源码/输出及源码哈希核对；未改作者实现、未运行QEMU、未增加正式测试矩阵。

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

## 当时记录的后续建议

以下为当时的规划，不表示本批已实现或当前已开工。

**随后整体：用户地址空间与归属。** 将当前UserMemory、准备/复制用户段、区域权限、用户内存访问和回收归属组织成AddressSpace/区域管理接口。围绕已验证的行为设计拥有和借用关系，先明确支持的区域数量/分配方式，再决定是否扩展布局。保留可返回错误并可重试的显式release，不让析构吞掉错误或提前清空仍持有的资源。

**统一组织内存子系统。** 完成上述两层后再收拢对外接口与模块目录。MMU本身是CPU硬件；软件统一的是物理分配、地址空间、页表以及激活/刷新接口，不能因此把所有职责塞进单个MMU类。整体依赖为AddressSpace → PageTable → PMM，架构MMU接口负责satp/TLB，任务持有或借用明确的地址空间。

P0沿用相关原宿主场景和一次真实启动，由协作者承担；按实际新增类型/接口补必要检查，不新增完整历史故障矩阵。完整文件清单、类接口和验收条件在正式开批时一次给齐，较远阶段仍不预写大框架。
