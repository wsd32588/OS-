# BATCH12：物理页帧分配、回收与 FrameTracker

2026-10-18：作者已完成三处核心并移除编译guard，功能验收通过。
实际无警告构建、QEMU正常关机退出0；作者元数据实现通过既定重复回收拒绝验证。
以下保留整批要求与早期预检记录，第11节三题已集中复核并补充讲解。

## 1. 当前位置与本批目标

BATCH10提供内核堆；BATCH11提供PA/PPN等地址类型，功能与三题审查均已收尾。
接下来按第四章进入物理页帧管理：为后续页表、应用内存提供整页内存。

本批完成一个连续页池，支持分配、归还、优先复用，以及分配时整页清零。
用FrameTracker表示页的所有权，正常drop时自动归还；不改变现有应用装载、栈和调度。
satp继续Bare，本批不建页表、不安装映射、不将用户应用迁到这些页中。

## 2. 本项目为何不能直接从ekernel分到RAM末尾

固定官方ch4已采用不同的应用/地址空间布局，我们仍保留第三章两个固定装载窗口。
直接照抄[ekernel, MEMORY_END)会把应用目标窗口纳入页池，随后load_apps可能覆盖已经分配的页。

当前布局与本批选择：

| 区域 | 范围/规则 | 处理 |
| --- | --- | --- |
| QEMU RAM | 0x80000000..0x88000000，128MiB | 当前固定平台前提 |
| 固件保留区 | 0x80000000..0x80200000 | 不纳入页池 |
| 内核全部占用 | 0x80200000..ekernel，含heap、启动栈和任务栈 | 链接ASSERT保证ekernel不超过0x80400000 |
| 两应用窗口 | 0x80400000..0x80440000 | 整个窗口保留，不能只排除镜像当前字节数 |
| 本批页池 | 0x80440000..0x81000000 | 共3008个4KiB页 |
| 高处DTB | 当前日志在0x87e00000附近 | 不在本批小页池内 |

pool_bounds已提供：

```text
application_end = APP_BASE_ADDRESS + APP_COUNT × APP_SIZE_LIMIT
reserved_end    = max(ekernel, application_end)
start           = reserved_end按4KiB向上对齐
end             = 0x81000000，结束地址不包含在池内
```

计算使用checked运算；起止需在当前配置RAM内、按页对齐且start < end。
当前得到字节范围[0x80440000, 0x81000000)，对应PPN范围[0x80440, 0x81000)。
数量为(0x81000000-0x80440000)/4096=3008页，即11.75MiB。

这是一段有意缩小的页池，不是通用RAM发现；尚未解析DTB来识别全部可用/保留内存。
页池以外的空闲RAM暂不使用，也不额外在BSS定义一个3008页的大数组。
以后切换应用布局或扩展页池时，必须重新审查这些保留边界。

## 3. 整个资源关系

```text
固定BSS中的256KiB内核堆
  └─ 给recycled Vec提供页号记录所需的内存

页池中的3008个物理页
  └─ StackFrameAllocator只管理哪些PPN可分配
       └─ frame_alloc取出PPN → 解锁 → 清零4096字节 → 返回FrameTracker
            └─ 正常drop → 回收PPN → 下次优先复用
```

recycled存的是usize页号，不是每页的4096字节内容。
本批堆本身已静态保留，不依赖这个页池；不会形成“先有页才能建堆、先有堆才能分配页”的循环。
初始化预留至少3008个usize条目的容量，请求至少24064字节，分配器实际占用可能向上取整。
这片元数据在页分配器存活期间继续占用堆，所以frame演示结束后，不要求整个堆的使用量回到0。
原heap演示仍在frame初始化之前运行，其自身释放归零检查保留。

## 4. 已准备的文件与启动接线

| 文件 | 职责 | 作者工作 |
| --- | --- | --- |
| os/src/mm/frame_allocator.rs | 页池范围、元数据、所有者、清零及演示 | A/B/C三处TODO |
| os/src/mm/mod.rs | heap→address→frame初始化与演示 | 已接好 |
| os/Cargo.toml、Cargo.lock | 显式使用spin0.7.2 | 已接好，未升级包版本 |
| os/src/mm/address.rs | 已完成的PhysAddr/PhysPageNum转换 | 保持作者实现 |

spin0.7.2原本就是buddy的传递依赖，现作为精确直接依赖用于Mutex。
使用本项目已有缓存与MIT许可证，不安装全局工具，不引入lazy_static或新的同步框架。

```text
clear_bss → trap::init（SIE=0）→ mm::init
  → 原heap初始化与演示
  → 原address演示
  → frame_allocator::init：计算范围、锁外预留元数据、登记一次
  → frame_allocator::run_demo
  → 原load_apps / prepare_app / task::init / timer::init / run_first
```

作者无需修改main、链接脚本、用户程序和调度核心；main的mm失败日志已统一为memory initialization failed。
当前frame_allocator.rs保留compile_error，
避免三处TODO未完成时误以为新批次已经可运行。

## 5. 作者实现A：StackFrameAllocator::alloc

这是具体的页号分配策略，返回Option<PhysPageNum>。
名字中的Stack指回收页号按栈的LIFO顺序复用，不是为调用栈专门分配内存。

元数据含义：

| 字段 | 含义 |
| --- | --- |
| start | 页池起始PPN，包含 |
| current | 还未顺序分配过的第一个PPN |
| end | 页池结束PPN，不包含 |
| recycled | 曾分配、现已归还的页号栈 |

按以下顺序完成：

1. 先尝试recycled.pop；成功就转换为PhysPageNum并返回。
2. 没有回收页且current == end，返回None表示耗尽。
3. 否则保留旧current，推进current，再返回旧值对应的PPN。

使用BATCH11的PhysPageNum::new转换；正确元数据保证页号在表示范围内。
不要返回推进后的current，不要先判断顺序空间耗尽而忽略仍有recycled页。
此函数只改元数据，整页清零由外层在解锁后完成。

## 6. 作者实现B：StackFrameAllocator::dealloc

返回Result<(), &'static str>。先将PPN取为usize，集中检查以下条件：

- ppn < start：不属于本池。
- ppn >= current：超出已顺序分配过的区间，包含尚未分配过的页。
- recycled已经包含ppn：重复归还。

任一条件成立都返回Err，且不能修改current或recycled。
全部通过后才将页号push进recycled，并返回Ok(())。

只检查ppn < end不够：页可能在池内，但从未交给任何调用方使用。
固定官方实现未显式检查池下界，本项目需要保留start检查。

初始化已预留至少总页数的容量；正确生命周期下回收记录不会超过总页数，push不会扩容。
该方法不抹零物理页；清零发生在下次分配给新拥有者之前。
不要将current倒退到被释放的页，否则可能重新分配仍由其他Tracker持有的页。

## 7. 作者实现C：FrameTracker::drop

FrameTracker包含私有PPN，不实现Clone/Copy；构造函数也私有，只由成功分配路径创建。
它代表一页的所有权，而PhysPageNum只是可以复制的数字。

Drop中调用一次frame_dealloc(self.ppn)，明确处理Result：
成功则正常结束；Err意味着内部所有权不变量破坏，调用panic报告错误，不能忽略。
frame_dealloc返回前已经释放锁守卫，因此错误处理不会拿着页池锁进入panic。

不要在Drop里另取FRAME_ALLOCATOR锁，也不要手动drop self。
正常离开作用域会自动drop，显式drop(frame)会消费所有权，之后不能再使用该Tracker。
当前panic=abort不会替你进行栈展开和自动归还，不能把失败停机当作正常资源回收路径。

| 动作 | 页所有权变化 |
| --- | --- |
| let other = frame | 移动所有权，不产生第二个拥有者 |
| let number = frame.ppn() | 复制页号，原frame仍拥有该页 |
| drop(frame) | 销毁拥有者，将页号归还分配器 |

原始frame_dealloc入口保持私有，不允许外部仅凭复制出的PPN释放一个活页。

## 8. 锁、清零和安全边界

FRAME_ALLOCATOR使用实际的spin::Mutex，与前面TaskManager短作用域&mut不同。
分配时只在锁内取出PPN，守卫结束后再构造Tracker和清零。
不要持锁打印、分配堆对象、销毁Tracker或切换任务；Tracker的Drop也会申请这把锁。
初始化重复调用会返回Err，不会覆盖仍被Tracker使用的分配器状态。

bytes_mut(&mut self)返回&mut [u8; PAGE_SIZE]，引用生命周期绑定当前Tracker的可变借用。
调用方只要还在使用这个引用，就不能在安全Rust中同时销毁Tracker。
底层unsafe依赖：页来自已保留的有效RAM池、4096对齐、独占拥有，当前Bare地址可直接访问，
而且尚未交给映射、设备或其他使用者。仅有合法PPN格式并不足以证明这些条件。
没有给PhysPageNum添加返回&'static mut页数组的方法。

本批访问只是独占页的普通RAM读写，不要求volatile，也不涉及TLB刷新。
后续将页交给页表硬件或映射给应用时，需要重新保证引用与映射生命周期，
不能仍有硬件/应用访问就drop页，更不能直接沿用当前“未共享页”的全部安全前提。
本批继续限定单hart、内核SIE=0；这把锁不会自动开启安全的内核嵌套中断支持。

## 9. 可观察结果与完成标准

填完三处TODO，移除frame_allocator.rs的compile_error，再运行：

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
timeout 8s make run
echo $?
```

原heap/address日志之后应出现：

```text
[frame] region [0x80440000, 0x81000000) pages=3008
[frame] allocated ppn=0x80440, 0x80441
[frame] reused ppn=0x80440 cleared=true
[frame] released: free=3008 in_use=0
```

演示已提供：

1. 分配两页，检查不同页号、顺序地址、每页4096字节全0及in_use=2。
2. 给第一页写入0xa5，drop第一页，重新分配得到相同PPN且整页重新为0。
3. 释放全部Tracker，统计恢复到total=free=3008、in_use=0。
4. 继续原双应用三轮计算、双向timer抢占、各exit0一次，QEMU主动关机退出0。

统计free = end-current+recycled.len()；全部归还不要求current回到start，
因为已经走过的页可以记录在recycled里，仍然属于可用页。
默认QEMU仍为/usr/bin系统10.2.1，不使用PATH中旧固件关机不兼容的开发版。

## 10. 必要验证与预检记录

P0由协作者承担，作者新增测试代码为零，范围固定为正常/不变量与一条拒绝路径：

- 纯元数据：三页小池按序分配、耗尽返回None、归还后优先复用；
  重复归还返回Err，current/回收栈/统计不变，之后仍能正常分配。
- QEMU：上面实际页内存的分配/写入/归还/复用清零；元数据堆占用在页演示前后不变；
  satp仍0、内核SIE仍0，原应用运行退出正常。

不扩成随机压力测试、全边界矩阵、通用多池或OOM作业。
输入范围校验和私有所有权接口仍属于正常实现要求，不因只选一条拒绝探针而省略。

2026-10-18 `.cache/batch12-preflight-0vdl0a0z/` 仅在副本补齐三处：
同一元数据实现的64位宿主编译使用-D warnings，正常/重复归还拒绝断言通过；
内核离线锁定构建无警告，QEMU输出与上面一致、退出0，CSR与堆占用观察通过。
预检前后作者os/user源码及配置哈希一致，没有将临时核心实现复制到作者文件。
批前快照 `.cache/batch11-before-frame-8k3htbxk/` 保留BATCH11源码、Makefile和来源锁。
只核对官方ch4相关模块，未构建验收完整官方第四章工程。

2026-10-18作者实现验收：独立只读审查alloc/dealloc/Drop无必修功能问题。
同一作者元数据实现按既定三页场景在64位宿主使用-D warnings编译运行通过，
顺序分配/耗尽/优先复用正常，重复回收Err且current/回收栈/统计不变，之后仍能分配。
实际timeout 8s make run无警告构建、主动关机退出0；真实页复用后整页清零，
全部释放free3008/in_use0，原heap/address断言、双应用各三轮/双向timer/各exit0正常。
未代改作者实现或扩展测试矩阵；os/user源码及配置哈希前后一致。
证据 `.cache/batch12-author-review-_8_s5n45/`；仍未建立页表或开启Sv39。

## 11. 理解题与答案审查（2026-10-18）

以下保留作者原答，逐题记录审查与修正版；所有权、锁与清零的缺口已集中补充讲解，
不将讲解补齐的内容记作作者已独立答对。

### 第1题：页池边界、物理页数量与recycled容量

问题：为什么不能直接从ekernel分配到RAM末尾？3008个物理页与recycled的容量各代表什么？

**作者原答：**

> 因为ekernel是任意的一个地址，不一定能够对齐4kib吃尽内存，而且RAM末尾向下取整，只有完整落在合法物理 RAM 范围内的页帧才能纳入管理，3008个物理页是当前特定容量下的可用物理内存页，recyled是当前以使用的内存页数

**审查：** 完整页与合法内存边界的方向正确，但本项目ekernel是链接器确定的内核结束位置，
且已按4KiB对齐。主要限制是后续仍有应用窗口及RAM高处的DTB等保留内容。
recycled记录已归还的空闲页，不是正在使用的页数。

**修正版：**

对齐不能替代保留区域检查。当前内核结束之后，两个应用仍要装载到固定窗口，
所以不能将[ekernel, RAM_END)整体纳入页池。本批明确保留应用窗口并避开高处DTB，
只管理[0x80440000, 0x81000000)的完整页帧。

| 数量 | 含义 |
| --- | --- |
| 3008个物理页 | 本批页池总资源，不是整台机器的全部空闲内存 |
| recycled.len() | 曾经分配、现在已归还且可以复用的页号记录数 |
| recycled.capacity() | 元数据缓冲区无需扩容就能容纳的页号记录数 |

尚未顺序分配过的[current, end)也是空闲资源，因此：

```text
空闲页数 = end - current + recycled.len()
正在使用 = current - start - recycled.len()
```

recycled中的每个usize只是一个页号记录，不是那一页的4096字节内容。

### 第2题：回收栈优先、current边界与归还后的状态

问题：为什么优先检查recycled，回收时不能简单让current减一？全部释放后current没回到start有问题吗？

**作者原答：**

> 需要先检查是否还存在再recyled内，而且current不是连续的，释放了B但是C还在使用，那么下一次就会误伤到C的数据了

**审查：** 已正确指出活页重叠风险。需要修正“current不是连续的”：current是单调推进的
首次分配边界，出现空洞的是已分配区间中的使用情况；并补上全部归还后的计数解释。

**修正版：**

本批优先复用recycled中的空闲页；即使current已经到end，只要回收栈不空，仍然能分配。
如果先因current == end返回None，就会错误地忽略可复用页。

```text
依次分配 A、B、C：current指向D
归还B：recycled=[B]，current仍指向D
```

此时让current减一会使它指向仍在使用的C，下次顺序分配可能重复交出C。
因此回收应记录空闲页号，不能把任意归还误当作连续退栈。

全部归还后current保持原位置没有问题，已经走过的页号都可以记录在recycled中，
结合尚未分配区间，空闲总数仍恢复为3008。无需让current回到start。

### 第3题：页号、拥有者、移动与锁

问题：为什么PPN可以复制，而FrameTracker不能？为什么不能持锁销毁Tracker，清零又放在解锁后的分配路径？

**作者原答：**

> PhysicalPageNumber只是个地址数字，而FrameTracker拥有生命周期，不能随便销毁，尤其是在Rust，上一个的借用为传递就销毁，导致使用权就此断裂，而且对于Rust,FrameTracker的不只是复制，还顺带会带走所有权

**审查：** 已注意到页号与生命周期不同，但需要区分复制和移动。
原答尚未覆盖持锁销毁与分配时清零的原因，补充如下。

**修正版：**

PhysPageNum是物理页号数字，不是物理字节地址；复制这个数字不会产生新的页所有权。
FrameTracker是唯一拥有者。如果允许复制两个拥有者，第一个销毁时就会归还页，
而第二个可能还在使用它，随后还可能重复归还。

```rust
let other = frame; // 移动所有权；之后frame不可再使用。
```

这里没有产生第二个拥有者，也没有立即归还页，所有权由other继续持有。
存在仍在使用的借用时，安全Rust会阻止销毁对应拥有者。

持有页分配器锁再销毁Tracker，会发生Drop→frame_dealloc→再次申请同一把锁，
当前执行流等待自己释放锁，形成死锁。

清零放在分配路径，是为了新拥有者拿到干净页，不带上一个拥有者的残留数据。
分配器取出PPN后，该页已不在可分配集合中，解锁不会让它重新变成空闲页，
因此仍可由新拥有者独占清零。4096字节写入不需要占用仅保护元数据的锁。
归还路径只登记页号，下次交给新拥有者前再清零即可。

## 12. 来源与本批差异

固定官方ch4 `f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79` 的
[frame_allocator.rs](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/mm/frame_allocator.rs)。
教材对应[管理Sv39多级页表中的物理页帧管理部分](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/4sv39-implementation-2.html)。

本项目明确区别：保留第三章应用窗口、使用固定小页池、补下界校验、元数据预留容量、
Tracker构造/PPN私有、引用绑定拥有者生命周期、使用已有spin而不引入UPSafeCell/lazy_static。
核心仍是同一条路径：取得页号→独占页→使用→拥有者结束→页号归还，下一批再把页用于页表。
