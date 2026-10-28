# BATCH10：第四章起步，内核堆与 Box/Vec

2026-10-17：作者已完成heap_allocator.rs的两个函数并移除编译guard，
实际构建、正常运行与重复初始化拒绝路径均验收通过，三题已集中复核。
以下保留整批要求与早期预检记录；当前准备BATCH11地址类型文档。

## 1. 我们到第三章哪里了

第三章的核心主线已经走到最后一节“分时多任务系统与抢占式调度”：

| 教材主题 | 对应批次 | 当前完成点 |
| --- | --- | --- |
| 多道程序放置与加载 | BATCH06 | 两应用同时驻留，各自有用户/内核栈和现场 |
| 任务切换 | BATCH07 | TaskContext 与 __switch，能首次进入及续接 |
| 多道程序与协作式调度 | BATCH08 | 状态枚举、TCB、yield、逐任务exit |
| 分时多任务与抢占式调度 | BATCH09 | S timer 抢占、无yield应用交替运行 |

get_time 系统调用169尚未实现；task_info410、统计、浮点、S-mode嵌套等练习也未做。
这些覆盖限制保留，不宣称第三章每个接口和习题都完成，也不追加为进入第四章的前置作业。
第九批三题已经作答并集中修正，记录在BATCH09第10节。

本批对应第四章第一节“Rust中的动态内存分配”。之后按教材继续：

```text
内核堆（本批）
  → 地址空间概念与地址类型
  → Sv39硬件规则
  → 物理页帧分配与页表管理
  → 内核/应用地址空间、ELF布局
  → 在分页地址空间上运行多任务
```

本批不设置satp、不建立页表、不改变已有应用入口、用户栈或调度策略。

## 2. 为什么先做堆

之后管理物理页、映射区域等信息时，会需要 Vec 一类可增长容器。
先给内核建立按字节申请和释放内存的能力，才能使用这些容器。

| 存储方式 | 当前例子 | 谁管理生命周期 |
| --- | --- | --- |
| 静态区域 | HEAP_SPACE、任务表、预留栈数组 | 贯穿内核运行，由链接布局保留 |
| 调用栈 | 局部计数、函数调用帧 | 随函数调用建立和回收 |
| 堆中分配 | Box拥有的值、Vec元素缓冲区 | 分配器管理空间，拥有者正常drop时释放 |

堆的底层区域可以是静态数组，区域内部的块仍可以运行时分配/释放。
这不是矛盾：静态数组解决“内存从哪里来”，分配器解决“其中哪些块空闲”。
堆分配器按请求的字节数和对齐提供块；后续物理页分配器管理4KiB页帧。
两者职责不同，本批不会实现后者。当前satp仍为Bare，不需要先有页表才能分配这片堆。

这是内核自己的堆。用户程序是独立二进制，不会自动继承内核的全局分配器。

## 3. 内存布局和版本

本项目先保留256KiB（262144字节）内核堆，按4096字节对齐，放普通BSS：

```text
内核布局：代码/只读数据/数据/boot stack/普通BSS（含heap、任务栈等）
                               heap在[sbss, ebss)内，由clear_bss先清零
ekernel <= 0x80400000           链接ASSERT继续防止内核覆盖应用窗口
0x80400000、0x80420000         当前两个应用的独立装载起点
```

堆不放进.bss.stack，因为那一段专门排除在清BSS范围外。
原有链接脚本会捕获静态堆，BSS为NOLOAD，镜像不需要保存整片零字节。
内存地址由本次链接布局决定，不硬编码预检中某个heap地址。

固定官方ch4示例使用3MiB堆；直接照搬会越过当前应用边界，所以本批明确使用256KiB。
堆在内核镜像保留范围内，之后划分物理页池时也必须排除整个内核保留范围。

依赖对照官方固定ch4 `f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79`：

- 官方要求buddy_system_allocator的0.6系列，本项目精确固定为 `=0.6.0`。
- 该版的LockedHeap不带const泛型，写 `LockedHeap::empty()`，不能照新版写 `<32>`。
- 当前解析得到spin0.7.2，已写入os/Cargo.lock，后续继续 `--offline --locked`。
- 两个包及各自LICENSE保存在本项目.cache/cargo；实际下载包均为MIT。
- 不启用buddy的const_fn feature，不添加nightly配置或 `#[alloc_error_handler]`。

本机Rust1.98.1可使用默认no_std分配失败路径，现有panic_handler承接失败；
正常释放通过drop发生。panic=abort不等于会展开栈并替你释放所有对象。
教材在线页保留了一些旧nightly写法，本项目按固定代码和实际stable编译结果接线。

## 4. 模块和完整启动路径

| 文件 | 职责 | 作者工作 |
| --- | --- | --- |
| os/Cargo.toml、Cargo.lock | 固定堆分配器依赖 | 已准备 |
| os/src/main.rs | 引入alloc，禁用S中断后初始化mm | 已接好 |
| os/src/mm/mod.rs | 堆初始化和演示入口 | 已接好 |
| os/src/mm/heap_allocator.rs | 堆存储、分配器、统计与演示 | A/B两个函数，完成后移除guard |

`extern crate alloc` 只引入Box/Vec等接口；`#[global_allocator]` 才将这些分配请求
接到HEAP_ALLOCATOR。静态分配器刚创建时还是empty，必须先交给它有效内存区域。

```text
entry建立boot stack → rust_main
  → clear_bss：清零普通BSS，包括heap存储和初始化标志
  → trap::init：SIE=0，安装入口
  → mm::init
    → init_heap：把永久存活的heap区域交给分配器，仅一次
    → run_demo：Box/Vec分配 → 内容/地址/统计检查 → drop → 统计回到基线
  → 原load_apps / prepare_app / task::init
  → timer::init / run_first
  → 原两应用抢占、退出、正常关机
```

堆演示在timer启动前完成，避免把动态分配、调度和页表同时改动。
clear_bss在init_heap之前；一旦初始化，不能再清零或直接读写HEAP_SPACE，
否则会破坏分配器保存在其中的空闲链表或仍存活的对象。

## 5. 作者实现A：init_heap

返回类型为 `Result<(), &'static str>`，按以下顺序完成：

1. 调用is_initialized。如果已经初始化，立即返回Err，不能触碰分配器。
2. 调用heap_bounds取得start，大小使用KERNEL_HEAP_SIZE。
3. 在unsafe中调用 `HEAP_ALLOCATOR.lock().init(start, KERNEL_HEAP_SIZE)`。
4. 使用 `addr_of_mut!(HEAP_INITIALIZED).write(true)` 标记完成，再返回Ok(())。

第3步需要的安全前提是：内存永久存活、对齐且可写、由分配器独占管理，
本次只初始化一次，且发生在清BSS之后、任何分配之前。把这些原因写进SAFETY注释。
heap_bounds已使用原始指针取地址，无需给static mut创建引用。

本版本的buddy init实际会把传入范围加入空闲区。重复登记相同范围可能重复分配同一内存，
破坏存活对象和空闲链表，因此拒绝检查必须放在init调用之前。

本批调用前提是单hart启动阶段且SIE=0；初始化标志不是多核并发初始化协议。

## 6. 作者实现B：demo_allocations

在函数内引入 `alloc::boxed::Box` 和 `alloc::vec::Vec`：

1. 创建 `Box::new(42usize)`。
2. 创建空 `Vec<usize>`，逐个push范围 `0..DEMO_VALUES` 的值，共512个。
3. 保持两个对象同时存活，调用已提供的检查函数：

   ```rust
   report_live_allocations(
       boxed.as_ref(),
       values.as_slice(),
       values.capacity(),
   );
   ```

4. 显式 `drop(values)`、`drop(boxed)`，之后不再使用它们。

这里boxed、values是示例变量名，你可以保留自己的可读命名。
检查函数已核对Box值42、Vec长度512及每个元素、对齐、完整分配范围和存活统计。
外层run_demo会再核对释放后的统计，不要求作者另写测试框架。

需要分清的Rust概念：

- Box是拥有堆内值的对象。Box变量通常在当前栈上，`boxed.as_ref()` 指向堆内的usize。
- Vec逻辑上管理元素指针、len和capacity；`&values` 是Vec对象自身的地址，
  `values.as_ptr()` 才是元素缓冲区地址。
- 本例Vec::new尚未申请元素缓冲区，push时才按需申请或扩容。
  capacity可以大于len，检查分配范围使用capacity乘元素大小，不能只看第一个元素。
- drop会消费拥有者并释放资源；正常离开作用域也会自动drop。
  释放空间不保证抹零，也不保证下一次获得完全相同的地址。

## 7. 这次的lock确实是锁

前面TaskManager的短作用域&mut是独占引用；这里LockedHeap.lock()实际取得自旋锁守卫。
它们不是同一机制。

使用 `.lock().init(...)` 的临时守卫会在该语句结束时释放。
statistics也只在内部持锁，复制三个数返回后释放，调用方拿到的不是守卫。

不能持有HEAP_ALLOCATOR.lock()的守卫再创建Box、扩容Vec或drop堆对象：
这些操作内部也会请求同一把分配器锁，可能让同一执行路径等自己释放锁。
不要持锁打印、panic或切换任务；先结束这段访问再进行其他操作。
本批只在启动阶段演示，内核运行期间继续保持SIE=0，也没有中断处理中的堆分配。

三个统计量来自实际0.6.0接口：

| HeapStatistics字段 | 含义 |
| --- | --- |
| requested_bytes | 当前仍存活的分配请求大小总和 |
| allocated_bytes | 分配器实际占用的块大小总和，可能包含向上取整 |
| total_bytes | 已交给分配器管理的总大小，本批262144 |

## 8. 完成与验收现象

填完两个函数，移除heap_allocator.rs的compile_error；保留原布局与状态检查。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
timeout 8s make run
echo $?
```

在应用装载日志之前，应看到：

```text
[heap] region [实际起点, 实际终点) bytes=262144
[heap] box=42 vec_len=512 capacity=512 requested=4104 actual=4104
[heap] released: requested=0 actual=0
```

上面的capacity/存活字节数是当前工具链预检观察值，检查逻辑允许capacity大于512；
关键是内容和分配范围正确、释放后统计回到基线。
随后仍有原两应用compute日志、双向timer抢占、各exit0一次，最终QEMU正常退出0。
默认QEMU继续使用Makefile中已验证的/usr/bin系统10.2.1，不改回PATH中的开发版。

本批之后satp仍为Bare；Box/Vec能够分配，不代表已经实现页表或用户地址空间隔离。

## 9. 必要验证与预检记录

P0由协作者承担，固定两次QEMU，作者新增测试代码为零：

- 正常：Box/Vec同时存活，检查数值/512元素、对齐、完整堆范围；drop后两个存活统计归零，
  total保持262144；堆在普通BSS内、4096对齐、与任务栈不重叠、低于应用窗口；
  satp=0且内核SIE=0，原双应用timer/退出仍正常，最终退出0。
- 拒绝：已经初始化且有活Box时，再次init_heap得到Err；Box值、地址与分配统计保持原样；
  之后还能再分配/释放另一个Box，最终统计回到基线，再运行原用户应用退出0。

学习点是“内存区域只交给分配器一次”和“拥有者释放资源”，不扩成分配算法压力矩阵。
本批不要求自行实现buddy算法、OOM测试或新增用户内存接口。

2026-10-17临时 `.cache/batch10-preflight-5h1vfeu9/` 补齐两个函数并加观察后，
上述两条均无警告构建、QEMU退出0；作者os/user源码哈希前后不变。
改动前快照 `.cache/batch09-before-heap-4k1zbhrj/` 保留BATCH09代码和旧依赖锁。
只核对官方ch4相关模块，未构建/验收官方整个第四章工程。

2026-10-17作者实现验收：实际make build无警告、timeout 8s make run主动关机退出0；
Box/Vec存活统计4104、释放后归零，原双应用timer抢占、三轮计算和逐任务退出正常。
只在临时副本加入上述重复初始化探针，Err拒绝后活Box值/地址/统计不变，
仍可继续分配释放，最终QEMU退出0。作者os/user源码及配置哈希前后一致，无必修功能问题。
记录在 `.cache/batch10-author-review-nm8lhfbx/`；不要求作者追加测试。

## 10. 理解题与答案审查（2026-10-17）

以下保留作者本次原答，再逐题给出审查和修正版。理解题已集中复核；
本节不替代对作者实际代码的构建与功能验收。

### 第1题：静态区域为何能动态分配，堆与物理页分配器各管什么？

**作者原答：**

> 虽然bss内部是静态的，但是实际使用的时候是动态调整使用的，堆内的对象是一个随使用而创建销毁的，但是堆本身不会伸缩大小，4kib的物理页只负责分配，而不管你内部干什么，但是heap程序只申请释放，管理的依然是

**审查：** 已正确区分固定的内存区域与运行时变化的使用情况。最后一句尚未写完；
物理页分配器还负责回收，不仅负责分配。“堆本身不伸缩”是本批设计，不是所有堆的要求。

**修正版：**

HEAP_SPACE是位于普通BSS的静态存储，本批地址由链接决定，大小固定为256KiB，
贯穿内核运行。运行时改变的是内部哪些块已占用、哪些空闲，以及何时分配和归还。
“动态分配”说的是这些决定在运行时发生，不要求堆的总大小必须变化。

| 角色 | 管理内容 |
| --- | --- |
| Box、Vec等拥有者 | 对象/元素的生命周期，正常drop时归还拥有的堆内存 |
| 堆分配器 | 指定区域内的空闲块和已分配块，按请求字节数与对齐分配、回收；buddy内部按2的幂组织块 |
| 物理页分配器 | 以4KiB页帧为单位分配、回收，不解释页内数据的含义 |

物理页可以用于页表、用户数据、栈，或交给堆分配器进一步管理。
当前堆直接来自BSS，还没有接到后续的物理页分配器上。
未来物理页分配器只能管理明确划给它的可用范围，必须排除内核、固件等保留区域。

### 第2题：Box/Vec对象与数据在哪里，len/capacity/drop各意味着什么？

**作者原答：**

> Box/Vec都在这些变量在栈上，但是在new之后数据都在堆内空间，Vec的len是现有内容长度，而capacity是最大容量，该变需要扩容重新分配内存，而len只是重新计算元素数量，drap(values)是释放的堆内缓冲区，而不是直接删除栈上变量

**审查：** 已抓住局部拥有者与堆数据的区别，需要修正四点：Vec::new尚未分配缓冲区；
capacity是当前免重新分配的容量，不是最终最大容量；len由操作维护，不是遍历重算；
drop不仅释放缓冲区，还消费拥有者，之后不能继续使用values。函数名拼写为drop。

**修正版：**

本例局部Box/Vec对象可以按“放在调用栈上”理解；实际编译器可能将部分值保存在寄存器，
Box/Vec类型本身也不限制只能放栈上。学习重点是区分拥有者与它管理的数据：

- Box::new(42usize)分配堆内的usize值，boxed持有其所有权；boxed.as_ref()指向该值。
- Vec对象管理元素指针、len和capacity，values.as_ptr()指向元素缓冲区，
  &values指向Vec对象自身。这里不依赖这三个字段在内存中的排列顺序。
- 对本例Vec<usize>，Vec::new创建空容器，没有申请元素缓冲区；push时才按需分配。

len是当前已初始化的元素数，由push、pop等操作维护。
capacity是当前缓冲区无需重新分配就能容纳的元素数，始终不小于len。
已有容量足够时push无需扩容；不足时才需要申请更大空间，元素缓冲区地址可能改变。
具体增长倍数和首次容量不是本题要依赖的接口保证。
本例检查完整缓冲区范围使用capacity乘size_of::<usize>()，不能只按len计算。

```rust
drop(values);
// 此后不能继续使用values：它的所有权已经被消费。
```

drop会销毁Vec中的元素，并归还其拥有的堆缓冲区；本例usize没有额外析构资源。
本例Vec::new在尚未分配、capacity为0时没有堆缓冲区需要释放；仅len为0仍可能保留容量，
例如clear之后。Box的drop同样先销毁拥有的值，再归还对应堆分配。
栈槽无需立即清零或单独释放，但变量已经不可再使用。
这里归还的是堆内存块，不是将HEAP_SPACE整体归还给物理页分配器。

buddy实际块大小由请求大小、对齐和最小块要求决定，allocated_bytes可以大于requested_bytes，
但并非必然大于；本批预检两者同为4104。不能笼统认为每次分配都会额外附加一个块头。
释放内存也不保证抹零，或让下次分配取得同一地址。

### 第3题：重复init和持锁分配为何危险，与TaskManager的&mut有何区别？

**作者原答：**

> 重复对同一个堆init会重复写相同的数据，分配器锁的情况下，new会触发锁的机制，当场卡死

**审查：** 持锁再申请导致死锁的判断正确。重复init的关键不只是重复写数据，
而是把同一区域再次登记为空闲空间，破坏分配器状态和存活对象；原答还需补上借用与锁的区别。

**修正版：**

本项目锁定的buddy0.6.0中，Heap::init(start, size)调用add_to_heap(start, start + size)，
向空闲链表添加块并累计total。它不会把已有堆安全重置。
重复登记相同范围会破坏空闲链表，可能让两个分配返回重叠内存；
空闲链表节点写入块首部，还可能覆盖仍存活的对象。管理总量统计也会被错误累计。
因此必须先检查是否已初始化，在调用init之前拒绝重复登记，再在首次成功后设置标志。

LockedHeap使用实际的自旋互斥锁，同一执行流持有守卫时再次分配的路径是：

```text
持有分配器锁
  → Box::new申请内存
  → 全局分配器再次请求同一把锁
  → 自旋等待原守卫释放
  → 当前执行流无法继续到释放位置，形成死锁
```

会申请内存的Vec扩容、会归还堆内存的drop也有相同问题；不是每次Vec::push都需要分配，
但不能在持有分配器锁的代码中依赖这一点。先释放守卫，再进行这些操作。
statistics只在内部持锁并复制统计值，返回前守卫自动drop；
HEAP_ALLOCATOR.lock().init(...)的临时守卫在该语句结束时自动释放，无需显式unlock。
持锁期间也应避免打印、panic或切换任务，以免引入重入、等待或持锁跨切换的问题。
本批内核SIE=0不改变这把锁不可重入的事实。

TaskManager的&mut是Rust的独占引用，当前代码没有对应的运行时互斥锁。
普通安全Rust通过借用检查限制引用使用；从原始指针建立引用的unsafe代码仍需人工保证别名规则，
不能指望编译器自动发现跨任务的冲突。因此__switch前仍须结束管理器的借用。
LockedHeap.lock()则在运行时取得互斥锁，用守卫的生命周期管理释放；
&mut借用与自旋锁不能混为同一种机制。

参考：[第四章动态内存分配](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/1rust-dynamic-allocation.html)，
[buddy_system_allocator v0.6.0](https://github.com/rcore-os/buddy_system_allocator/tree/v0.6.0)，
[Rust no_std分配失败说明](https://doc.rust-lang.org/alloc/alloc/fn.handle_alloc_error.html)。
代码固定对照ch4提交 `f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79`，
实际依赖版本与校验由os/Cargo.lock记录，不随在线教材或最新版API漂移。

