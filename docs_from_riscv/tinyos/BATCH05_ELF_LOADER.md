# BATCH05：ELF 装载模块与布局配置

状态：已通过。实施时间：2026-10-28—2026-10-29；整理：2026-10-29。

将受限 ELF 装载从启动私有代码移入内存模块，由 boot 显式提供布局，并用同一配置设置用户栈顶。实现、P0 验证和三题审查均已完成。

这是刚完成的一批。新接口、布局检查、启动接线、拒绝/换栈/回收验证及三题原答都在本文件维护。

导航：[批次索引](README.md) · [总路线图](../TINYOS_EVOLUTION.md)

本文的裸源码路径以仓库根目录为基准；可点击的相对链接以本文件为基准。过程性记录保留原时点，当前状态以验收结论为准。

## 最终实现验收（2026-10-29）

作者已在boot_stages.c构造三页配置并调用公开C入口，用户栈顶来自同一配置；boot_private.h移除旧声明，旧user_prepare.cpp已删除。Makefile只链接新的kernel/mm/user_loader.o，最终内核保留user_memory_load_elf原C符号，旧prepare符号消失，MM核心不包含启动私有头或DEMO宏。独立只读接线审查未发现阻塞问题。

协作者完成本批约定P0，证据统一在.cache/loader-boundary-final-vi8uv9x0/：

- 正式make -B与make test均通过、无警告；最终内核无未解析符号，loader对象没有init/fini数组。正式默认QEMU完成yield、sleep、Hello、Data OK和exit，退出后仍有两次S timer；返回124为限时结束。
- 一次临时QEMU合并验证无效配置与第二合法栈布局：stack=code时返回非法参数，PMM分配调用增量0、完整根表/已有内核映射/页数/输出七字段均保持。
- 临时只将用户栈改为0x40005000，新栈为V/R/W/U/A/D且整页清零，原0x40001000未映射。装载精确消耗3内容页+2中间表；任务初始sp=0x40006000，首次真实U syscall保存sp=0x40005fc0。
- 用户正常输出并退出后，三处用户映射消失、输出描述为空、5页恢复、完整root恢复且仍是活动根表，随后两次S timer继续。临时探针的QEMU在观察8秒后由脚本SIGTERM停止，返回0不表示客体自行关机。

probe-qemu.log、probe-results.json及复制/插桩脚本记录临时观察，正式代码源哈希前后一致。首个临时副本漏带freestanding导致的构建失败仅修探针复制配置，正式实现未改；不扩完整历史矩阵、未clean或提交。旧prepare回滚自身失败时的资源归属限制仍是原有边界，不据此宣称任意故障都能完全回滚。

## 执行关系与收尾理解题

boot选择镜像和布局，经C入口检查指针后进入C++核心；核心校验内容、分配/复制/映射，成功后才写UserMemory。启动层按本次配置设置用户SP，任务创建接管描述，exit继续调用原显式release并保留共享root。配置控制可接受的装载位置，不为固定VA的ELF执行重定位。以下三题已完成集中审查，原答与修正归档如下：

1. 为什么新栈可以从0x40001000改到0x40005000，但只修改配置中的代码/数据VA，不一定能让原ELF在新地址运行？
2. C入口已经判空，C++核心又使用引用，为什么核心仍须检查段数、地址和镜像范围？
3. DEMO地址常量留在boot，而loader接收配置，对这两个模块分别有什么好处？

**第一题作者原答：**

> 栈地址一般是动态变化的·，而且栈一般不用的时候只有虚拟地址，而修改代码数据地址可能导致原来的代码找不到我们使用的地址

**审查：** 已认识到修改代码/数据地址可能使链接时地址依赖失效。需要区分运行时SP变化与栈区域基址选择；当前实现并非按需分配，装载时已分配并映射栈物理页，未用到的字节也有物理存储。

**修正版：** 本程序的初始用户栈由启动层提供SP，ELF没有指定这张栈页的固定位置，所以只要新栈可写、映射正确且SP与之匹配，就可以换位置。运行期间SP随函数调用变化，不等于栈区域基址也不断改变。ELF规定代码/数据段VA及入口，指令还可能依赖链接时地址或段间相对布局；单改配置不会重写这些信息或完成重定位，当前装载器会拒绝与段VA不匹配的配置。

**第二题作者原答：**

> C入口的判空是检查参数是否有效，但是C++代码部分的引用却是检查能否拿去真正使用在环境下而不出问题，比如超出段范围检查就没有在C代码里出现

**审查：** 已区分入口检查与内容范围校验，但引用本身不会检查环境、段边界或地址合法性。C入口的判空也仅排除空指针，不等于所有参数内容已经有效。

**修正版：** C入口在解引用前拒绝空指针，才能构造引用和span。引用约定绑定有效对象，但对象中的段数可能越界、地址可能未对齐，span也可能为空。C++核心是公开可直接调用的入口，因此自身仍显式校验镜像、段数、地址、范围和权限；这些保障来自检查代码，而不是引用语法。

**第三题作者原答：**

> 固定，好改，有简单但统一的内容

**审查：** 提到了统一和方便修改，还需说清模块依赖与策略归属。收益不在于把布局永久固定，而在于把布局选择集中在明确调用方。

**修正版：** boot决定本次演示使用什么镜像、把栈安排在哪里，是策略；loader负责验证并执行分配、复制、映射与清理，是机制。改变符合当前限制的布局时只需调整调用配置，内存模块无需包含boot私有头，也不会把DEMO常量当成全局假设；其他调用方可复用同一装载器，同时仍受两LOAD/三页等已声明限制。本批实现、验证与三题收尾完成，后续方向按作者安排。

## 历史审查与修改（2026-10-29）

接线前的进度提交复核：下面初审发现的合法性方向、核心empty/段数/checked页尾和栈顶、PageRange比较及Makefile/CMake配置问题当时均已修正。make编译新loader成功，最终链接尚因boot_start_user调用旧prepare_demo_user_memory失败，当时按在制进度提交；日志.cache/git-checkpoint-2026-10-29/。目前已按本节开头完成接线与验收，以下仅保留历史审查供对照。

作者明确本轮先检查已修改文件，boot_private和启动侧接线之后自己处理，本轮不要求完成。输入/输出地址混用、头文件包含和标签拼写已修；C shim只读span构造、image.data()、输出引用赋值、root取址及外部map/unmap已正确，新核心单文件目标语法检查通过。

当前集中修正：loader23–28行的canonical和对齐检查缺!，合法配置被当成错误，VA应使用VirtAddr；核心入口仍需补empty、段数和checked页尾/栈顶。新增PageRange模板268行用了PageNum未提供的>=，实际实例化已复现错误，可比较.val()；它尚未接入loader，现有单页地址相等检查无需强制与模板同时使用。Makefile55行新对象末尾缺续行反斜杠，make -n实际missing separator；宿主CMake24行误加不存在的kernel/user/user_loader.cpp，应移除此target-only源（含fence.i），而非改成mm路径。

独立只读审查一致；本轮未改正式功能、未执行QEMU或重复旧测试，不把未完成的boot接线算作本轮已修改文件的问题。

## 目标与可观察结果

由作者整批实现，将启动演示的地址选择与实际装载操作分开。启动层提供镜像、已解析的ELF元数据和三页布局；内存模块完成校验、分配、复制、映射、刷新和失败清理，成功后交出UserMemory。当前共享root、受限两段ELF、三张内容页和两张中间表继续作为运行基线。

默认运行仍输出Hello与Data OK并正常退出用户任务；另外可以仅改变用户栈位置，不修改装载算法，用户仍正常执行。配置不负责重定位固定VA的ELF：代码/数据地址必须与ELF段和入口匹配，不能只改配置就搬动程序。

## 文件范围与职责

| 文件 | 本批修改 |
| --- | --- |
| kernel/boot/private/user_prepare.cpp → kernel/mm/user_loader.cpp | 搬移原装载主体，替换DEMO常量为显式配置，提供C++核心及薄C入口 |
| include/mm/user_loader.h（新增） | 声明三页布局配置和提供给C启动层的入口，包含所需C兼容类型 |
| include/mm/user_loader.hpp（新增） | 声明实际可复用的C++装载入口，引用页表/元数据/输出并以span描述镜像 |
| kernel/boot/private/boot_stages.c | 包含公开装载头，以现有DEMO常量构造配置，调用新C入口，按同一配置计算用户SP |
| kernel/boot/private/boot_private.h | 移除旧prepare_demo_user_memory声明；DEMO地址策略继续留在启动侧 |
| Makefile | 对象项由kernel/boot/private/user_prepare.o替换为kernel/mm/user_loader.o |

准备函数仍包含fence.i，只进入目标构建，本批不加入宿主CMake源列表。已有UserMemory七字段及显式转移、调度/trap/syscall实现、地址页表/PMM接口和用户链接脚本不改。新内存模块不包含boot_private.h，不引用DEMO_USER_*，不在boot中留下纯转发的旧prepare兼容函数。

## 两个公开入口

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

## 分配前完成的校验

1. 三个配置页起点均非0、符合Sv39 canonical、按4KiB对齐，且两两不同。用checked加法确认每张页的最后一个字节合法；栈起点加PAGE_SIZE得到的栈顶也须无溢出且canonical。对齐后栈顶自然符合现有16字节调用约定。
2. 沿用两LOAD限制与原有大小/文件范围/权限检查。按配置的code_address和data_address查找相应段，拒绝重复/未知段，继续要求代码段flags=5、数据段flags=6；段开始地址必须可表示为有效VA，不能先截断再比较。
3. 入口必须为2字节对齐，并处于代码段实际file_size覆盖范围；仍用先比较下界再相减的方式，避免不受控加法溢出。

这些检查都应在第一次pmm_alloc_page前结束。root是否已有映射仍由现有map返回冲突处理，不提前覆盖，不把布局检查改为“目标必须已有映射”。不增加对输出哨兵值的全零要求：失败继续保留原输出，成功调用的前置条件是输出不拥有尚未释放的资源，避免覆盖泄漏。

## 装载流程与资源边界

直接复用已完成的PreparedPage数组与阶段循环，字段中的VA改来自memory_layout。分配、复制、map、flush和提交的相对顺序保持；栈没有ELF文件内容，继续使用PMM提供的清零页。保留外部sv39_map_page、sv39_unmap_page、pmm_free_page等注入边界。

mapping_attempted和mapped继续分开；回滚unmap/flush/reclaim保持原栈→代码→数据次序，free仍为代码→栈→数据。只解除本次成功建立的叶映射，不破坏原有映射。成功后才一次性写memory_out，root继续借用，三张内容页交给描述管理。

在boot_start_user中使用局部C配置，初值仍取当前三个DEMO地址。load成功后，将同一memory_layout.stack_address + SV39_PAGE_SIZE作为task_create_user的用户栈顶；不要配置已经换了栈，启动时还传另一个旧常量。这里的加法依赖装载成功已完成的checked验证。镜像起点、elf_size、layout->entry_address、任务成功后清空源描述和任务创建失败后的善后保持。

本批不合并prepare回滚与UserMemory::release为一套新析构框架。两者拥有不同的提交状态，已有prepare清理自身也失败时的局部归属限制继续保留，不扩大重设计。也不增加内核堆、动态容器、多页ELF段、ELF重定位、独立root或调度机制。

## P0验证与交付

作者完整实现后集中检查，以下必要验证由协作者承担：

- make -B、make test通过，原C启动方正确链接；MM实现没有boot私有头/DEMO宏依赖，目标新对象进入最终链接，旧对象不再作为依赖。
- 一次默认QEMU完成yield、sleep、Hello、Data OK、exit，退出后A及至少两次S timer继续；默认成功装载消耗三张内容页加两张中间表，退出回收到原有基线。
- 一条无效配置（例如stack_address与code_address相同）在分配前拒绝，PMM页数、根分支、原映射和输出描述不变。
- 临时仅改栈地址为0x40005000，顶为0x40006000，保留原ELF代码/数据VA；检查旧栈位置无映射、新栈页权限与清零正确、实际用户SP采用新值，并正常退出回收。这是配置与执行路径验证，不要求作者另写测试矩阵。
- 原copy/release的权限/范围/部分失败重试宿主回归保持，C/C++声明与ABI符合前一批约定。

作者可先运行make、make test、timeout 8s make qemu观察默认行为；124仍表示常驻演示被限时终止。P1可选是新的未对齐/溢出配置拒绝，P2可选是更多历史故障注入。现有verify脚本仍引用旧函数/路径/页数的部分，由协作者按当前验证需要适配，不要求保留过时接口或先修整套历史矩阵。

任务交付时只更新本文；当前实现及最终验收以本节开头为准，原任务保留作对照。功能梳理与三题也集中在本节。
