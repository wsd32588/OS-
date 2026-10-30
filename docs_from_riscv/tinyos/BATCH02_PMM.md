# BATCH02：物理页分配器封装

状态：已通过。实施时间：2026-10-26；整理：2026-10-29。

用 PhysicalPageAllocator 集中管理物理页分配状态，保留四个 C 入口、LIFO 回收、整页清零和原错误语义。

原答、集中审查、修正版和实现约束都在本文件维护。

导航：[批次索引](README.md) · [总路线图](../TINYOS_EVOLUTION.md)

本文的裸源码路径以仓库根目录为基准；可点击的相对链接以本文件为基准。过程性记录保留原时点，当前状态以验收结论为准。

## 作者实现集中审查（2026-10-26）

作者已将PMM改为.cpp、五项状态收进匿名命名空间内类，并用四个C ABI转发；头文件保护和宿主CMake源路径接线正确。未发现迁移引入的算法错误，协作者未修改实现。

make -B与make test均0且无警告，PMM8192页/20000次操作与其余原宿主场景通过；对象有四个原C符号、无动态初始化节或C++运行时依赖，最终内核无未解析符号。一次限时QEMU完成yield/R/sleep/XHello/Data OK/exit，退出后两次S timer，没有setup失败或Trap，124来自限时。证据 `../../.cache/pmm-cpp-review-2pb7eimy/`；核对八个源配置前后哈希不变。

集中列出的本批收尾：类显式删除复制/移动构造和赋值并保留默认构造；available_pages加尾部const；五项成员明确初值并用constinit约束唯一实例。当前ppa{}已经值初始化清零，不属于未初始化故障；实际对象也没有观察到动态构造，这项补的是编译期约束。另三个地址字段的旧注释已错位，可随手修正，不作为功能阻塞。

作者已补成员初值、constinit与const查询，删除复制/移动操作。最终make和make test均0且无警告；临时编译期探针确认不可复制构造/赋值、不可移动构造/赋值、平凡析构以及const对象可查询，最终内核无未解析符号，PMM对象无动态初始化节。证据同目录target-final.log、host-final.log、constraints.cpp/log及final-constraints.json；未改作者实现，未重复QEMU。

作者的删除赋值声明采用按值复制参数与const右值参数，虽与建议签名不同，实际禁止复制/移动赋值的约束经探针成立，不作为新一轮修改要求。

## 功能梳理与理解题（已集中审查）

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

## 目标与调用关系

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

## 三处修改与接口边界

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

## 存储、初始化与生命周期

分配器实例本身是少量元数据，存放在静态存储区，不从PMM或堆申请。默认状态五项为零/空，启动时 `boot_init_pmm` 仍调用 `pmm_init(__kernel_end, device_tree)`，由init设置真实物理区间。

使用成员常量初值和可常量求值的默认构造，实例可写成 `constinit PhysicalPageAllocator g_page_allocator{};`。constinit要求静态初始化，编译器会拒绝需要动态初始化的写法；它不代表对象不可修改，也不会代替运行时init。构造阶段不扫描RAM、不申请页、不调用UART或SBI。

类应禁止复制和移动，防止复制出两份管理同一物理区间的游标/链表。保持平凡析构，不让析构自动释放所有已分配页；页是否仍被页表或任务使用由调用者管理。

原PMM允许重新init，现有宿主场景依赖这个能力。保留“有效init重置为新区间，清空回收状态”的语义；先计算并验证新边界，再提交成员状态，错误返回不改旧状态。重新init只用于启动或受控测试重置，运行中不能用它抹掉活跃页的记录。本批不另加一次性init限制。

## 必须保持的算法与不变量

- 起点向上页对齐、终点向下页对齐，只管理区间内完整4096字节页；内存上界仍由现有启动参数决定，不在本批加入DTB解析。
- 始终满足 `allocatable_start <= next_free_page <= allocatable_end`；顺序游标只前进，释放时不减游标。
- 优先从回收链表摘头，链表为空才顺序分配；回收继续头插，保持LIFO。摘头时先读并保存next，再清零该页，防止把链表链接清掉。
- 两种分配路径返回前都清零整页；耗尽返回NULL。
- 释放仍拒绝NULL、非页对齐、区间外、尚未顺序分配和重复归还的页；拒绝时状态不变，成功返回0，失败返回-1。
- 可分配页数仍是 `recycled_page_count + (allocatable_end - next_free_page) / PAGE_SIZE`。节点数量等于回收计数，回收链表不含仍在使用的页。
- 回收节点保存在空闲页内部，不另外申请节点或引入容器。可把 `FreePage` 定义为类的私有节点类型，字段只有next。
- C++会要求明确的指针转换：void指针到对象指针可以用static_cast，整数地址与指针之间用reinterpret_cast。不因改语言而改变页地址语义。
- 类封装本身不提供并发保护。本批保留单hart、现有调用时机约束，不同时加锁、改中断状态或改变调度。

## 验收范围与分工

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
