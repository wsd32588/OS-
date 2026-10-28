# BATCH14：映射区域与内核地址空间构造

2026-10-19：作者已完成四处核心并移除 guard，独立只读审查无必修问题，功能验收通过。
实际 timeout 8s make run 无警告构建、QEMU主动关机退出0；四区映射/权限、重叠拒绝与资源回收均通过。
作者源码配置前后哈希一致，记录 `.cache/batch14-author-review-sypusfsa/`。
以下保留整批要求与早期预检说明，第9节三题已集中复核并记录原答/修正版；当前仍为 Bare。

## 1. 本批要解决什么

PageTable 已经能建立单页映射。但内核要管理的是整段代码、只读数据、栈、堆等区域，
不能在启动代码里散放大量逐页 map 调用。

本批增加两个层次：

```text
MapArea：一个区域的半开范围和权限，只描述恒等映射
MemorySet：一张 PageTable + 一组 MapArea，组合成完整映射计划
PageTable：把区域逐页落实为真正的 PTE，拥有用于存表的物理页
```

完成后，软件查询应确认：内核代码 RX，只读数据 R，可写区域与页池 RW，所有叶项 U=0。
销毁这个临时地址空间后，页帧和堆占用回到演示开始前的基线。

本批不写 satp。原应用仍在 Bare 下运行；已有 trap、调度和用户程序不改。
先构造并验证内核映射，后续再安排启用分页与应用地址空间的切换。

## 2. 四个映射区域

所有范围均为页对齐的 [start, end)，end 不包含在内。

| 区域 | 边界来源 | 传给 map 的权限 | 叶项实际 flags |
| --- | --- | --- | --- |
| 内核代码 | [stext, etext) | R\|X\|A | V\|R\|X\|A，0x4b |
| 只读数据 | [srodata, erodata) | R\|A | V\|R\|A，0x43 |
| 全部可写内核区域 | [sdata, ekernel) | R\|W\|A\|D | V\|R\|W\|A\|D，0xc7 |
| 当前页池 | frame_allocator::pool_bounds() | R\|W\|A\|D | V\|R\|W\|A\|D，0xc7 |

第三个区域合并 data、启动栈、普通 BSS、静态堆及任务栈。这样不需要为可能为空的 data 单独造区域，
也不会遗漏位于 sbss 之前的启动栈。heap 和栈已被包含，不能再重复添加重叠区域。

恒等映射指 VA 和 PA 数值相同：VPN=PPN。两者仍是不同类型，权限与映射关系仍由页表描述。
映射已有 RAM 不需要为它重新申请数据页；页表本身的页仍需由 frame_alloc 分配。

## 3. 当前布局与刻意保留的空隙

```text
0x80000000 .. 0x80200000  固件：不在本批映射里
0x80200000 .. ekernel     内核：按 RX / R / RW 分区
ekernel    .. 0x80440000  未映射，包含两应用的固定装载窗口
0x80440000 .. 0x81000000  页池：RW，当前共3008页
0x81000000 .. RAM_END    未映射，包含高处DTB等内容
```

这里是本项目固定 virt/128MiB 平台的局部内核映射，并非完整 RAM 发现或完整可运行的用户地址空间。
页池映射让其中的物理字节在恒等映射下有相同数值的 VA 可访问，也覆盖用于存页表的那些页。
**映射页池不等于把3008页全部分配出去。** 分配器的空闲/在用状态仍由它自身维护。

当前输出、timer 和关机都通过 SBI，本批无需增加 S-mode 直接访问 UART/finisher 的 MMIO 映射。
后续需要直接驱动设备时，再添加对应区域和实际验证。

这个内核页表也没有给现有用户代码、用户栈建立 U=1 映射。
因此不能直接启用它后照旧进入用户态；应用映射与切换方案要在后续明确。

## 4. 链接脚本与页边界

已有 linker.ld 使用带点的 .stext/.etext/.srodata 符号。
本批保留它们，增加供 Rust 使用的无点 stext/etext/srodata，以及 erodata/sdata 边界。
kernel_layout() 已提供，读取这些符号的地址，不读取符号处的字节内容。
相邻边界可能有相同数值；接线中用 black_box 保留链接器给出的数值，
避免优化器按两个外部静态对象必不同址的假设折叠边界比较，作者不必改这部分。

边界按页对齐，当前满足 etext=srodata、erodata=sdata。
可写区结束取已对齐的 ekernel；不直接将未保证页对齐的 ebss 当作映射端点。

sbss 仍在 .bss.stack 之后，只用于原来的清零区间。
clear_bss、启动栈大小和入口汇编不改，映射范围从更早的 sdata 开始覆盖整个可写区。

本批 MapArea 仅接受：start<end、两个端点都4KiB对齐、end<2^38 的低地址区域。
不进行 floor/ceil 式向外取整，不支持跨 canonical 空洞或末端恰为2^38的特殊排他端点。
当前内核所有范围均符合这个有意收窄的接口，暂不增加通用范围迭代器。

## 5. 所有权与构造失败

MapArea 只保存 start_vpn、end_vpn、permissions，可以 Copy，因为它不拥有物理页。
MemorySet 的 Vec 保存这些描述；真正的页表页拥有者仍在 PageTable.frames 中。

恒等映射的代码、栈和堆本来就存在，不由 MemorySet 释放。
页池中的空闲页也不能因为被映射而创建一批 FrameTracker；那会错误地取得未分配页的所有权。
drop(memory) 只销毁区域元数据和页表页，不归还内核数据、栈、堆或整个页池。

本批只提供整组区域的构造，不实现逐区追加、删除和权限修改：

```text
先检查所有区域两两不重叠
  → 预留并复制区域元数据
  → 创建局部 PageTable
  → 逐区域、逐页建立映射
  → 全部成功才返回 MemorySet
```

若某个 map 因页帧不足失败，局部 PageTable 会自动 Drop，回收此前已建立的所有表页；
局部 Vec 也会释放。没有半成品 MemorySet 返回调用方，因此无需本批先实现 unmap 回滚。
已经使用过的页分配边界 current 可能前移、recycled 顺序也可能变化，恢复的是可用资源数量与所有权，
不承诺任意失败后所有分配器内部字节完全相同。
本批的重叠拒绝发生在任何分配之前，因此该拒绝路径的页数和堆统计应完全不变。

## 6. 文件与四处核心

已经接好：

- `os/src/mm/memory_set.rs`：类型、链接边界读取、查询包装、演示与资源检查。
- `os/src/mm/page_table.rs`：新增 leaf_flags 只读查询，并让同级模块复用权限检查。
  作者原有编码、解码、map、translate_va 函数体保持原样。
- `os/src/mm/mod.rs`：页表演示后调用 memory_set::run_demo()?。
- `os/src/linker.ld`：补齐上述无点边界符号，保留原清零范围。

四处 TODO 都在 memory_set.rs。完成后移除该文件顶部 compile_error。
不需要修改 main、frame_allocator、address、用户代码、汇编或 Cargo 依赖。

### A：MapArea::new

依次验证并构造描述：

1. 拒绝 start>=end、任一端点未对齐、end>=LOW_SV39_END。
2. super::page_table::validate_leaf_flags(permissions)?，再拒绝 permissions 中不属于 KERNEL_FLAGS 的位。
   这样排除 U、V、G、RSW 等；调用者只传本批支持的权限，V 由 PageTable::map 添加。
3. VirtAddr::new(start/end).ok_or(...)?.floor()，得到两个页号。
4. 返回 Self { start_vpn, end_vpn, permissions }。

范围检查用 Err，不用 assert 代替。因为 start<end<2^38，两个地址都处于合法低半区。
两个端点已对齐，floor 不会丢失需要保留的偏移，也不会意外扩大映射。

### B：MapArea::map

遍历 start_vpn.as_usize()..end_vpn.as_usize()，每个 number：

- 用 VirtPageNum::new(number) 构造 VPN；
- 用 super::address::PhysPageNum::new(number) 构造相同数值的 PPN；
- page_table.map(vpn, ppn, self.permissions)?。

最后返回 Ok(())。这些构造的范围已由 A 保证，可以用 expect 表达内部不变量。
不要把字节地址当作页号传入，也不要包含排他的 end 页，更不要为每个映射分配数据页。

### C：MemorySet::new

输入是 &[MapArea]，输出 Result<Self, &'static str>：

1. 枚举每个区域，与它之后的区域比较 overlaps；发现重叠立即 Err。
   [a,b) 与 [b,c) 只是相邻，允许同时存在。
2. 创建局部 Vec，用 try_reserve_exact(areas.len()) 处理元数据分配失败，
   再 extend_from_slice(areas) 复制描述。
3. 创建局部 PageTable::new()?，逐个 area.map(&mut page_table)?。
4. 成功后将局部表和 Vec 移进 Self。

不排序、不修改调用方切片。全 pair 检查对当前4个区域足够清楚，无需引入区间树。
错误传播时 Rust 会清理局部拥有者，不能为了返回错误而遗忘整个页表。

### D：MemorySet::new_kernel

从已提供的 kernel_layout() 和 frame_allocator::pool_bounds()? 取得边界，
按第2节表格构造四个 MapArea，用临时数组组成切片传给 Self::new。

不要写死当前编译产物中的 etext/ekernel 数值；代码增长后这些边界会变化。
不使用 sbss 作为可写区起点，不重复映射 heap，不把两个应用槽纳入页池。

## 7. 可观察结果与页数

完成后的新输出应为：

```text
[space] areas=4 table_frames=9 identity=true permissions=true
[space] boot_stack/heap/page_tables=RW gaps=unmapped satp=Bare
[space] overlap=Err unchanged=true
[space] released: free=3008 in_use=0 heap_restored=true
```

当前全部映射的 VPN2=2，因此共用一张 L1 表。
内核在 VPN1=1 的2MiB区域内，页池从 VPN1=2 的中间延伸到 VPN1=7 的末尾，
共需7张 L0 表。加根表和 L1 表，所以表页总数为1+1+7=9。
这里的9依赖本批固定布局，不是任意地址空间的固定页表数量。

MemorySet 存活时比基线多用9页，无新增数据页；销毁后恢复 free=3008、in_use=0。
演示会查询每个映射页的页首及每区最后一字节，并比较实际 PTE flags，
另核对启动栈、当前 sp、堆两端、根表物理页均有 RW 恒等映射。
内核结束后的间隙、应用窗口起点、页池排他终点应没有映射。

这些检查证明数据结构与权限位设置正确。当前仍是 Bare，不能据此宣称 CPU 已执行了读写/执行权限保护。

## 8. 验收范围与命令

P0 必须，由协作者承担：四区域恒等映射与精确权限、9表页且不分配数据页、
映射与资源生命周期不变量、一条重叠区域拒绝路径；同时确认 satp0/SIE0、
原两应用各三轮、timer双向抢占、各exit0及正常关机。
本批观察与拒绝已放在演示里，不要求作者另外编写测试框架。

P1 选做：只读追踪一次 map 分配失败时哪些局部拥有者被销毁，新增代码0行，
学习点是 ? 与 RAII 的配合。P2 通用高半区范围、动态区域增删、大页与完整故障矩阵留后续，
不作为本批完成条件。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
timeout 8s make run
echo $?
```

早期骨架因 guard 预期不能构建成功；当前作者已完成四处并删除 guard，无警告构建、默认系统 QEMU 退出0。
124 表示被 timeout 终止，不等于主动关机成功。不要将旧二进制输出作为本批验收。

批前快照：`.cache/batch13-before-memory-set-bfdsxunz/`。
预检副本：`.cache/batch14-preflight-o57bimxb/`，只在副本补齐四处核心。
离线构建无警告、QEMU主动关机退出0；四区权限与逐页查询、9表页、启动栈/堆/表页覆盖、
重叠拒绝、资源恢复、Bare/SIE0及原双应用均通过。
早期预检还复现并修正了上面的链接边界同址优化问题，保留修正前日志供对照。
以上是早期骨架预检；作者版本现已单独通过验收，记录见页首目录，不能混淆两次验证。

## 9. 理解题与答案审查（2026-10-19）

以下保留作者原答，逐题记录审查与修正版。集中补充的讲解不记作作者已独立答对。

### 第1题：恒等映射与9个页表页

问题：VPN 与 PPN 数值相同，为什么还要建立页表？映射3008页的页池，为什么只新增9个在用页？

**作者原答：**

> 首先MMU之后没有例外存在，然后就算是内核部分也需要区分flags

**审查：** 内核也要通过映射设置权限的方向正确，但应限定为启用Sv39后的S/U模式正常访存，
不能笼统说所有特权级都没有例外。原答尚未解释3008个映射页与9个实际分配页的区别。

**修正版：**

启用Sv39后，S/U模式的正常取指和数据访问需要有效映射，即使VA=PA，也要从PTE找到目标并检查权限。
恒等映射不是绕过页表，只是映射前后地址数值相同。

给3008页建立映射只是登记地址对应关系，没有把这3008页全部分配出去。
当前新增的9个在用页用于存放页表树：1个根表、1个L1表、7个L0表。
池内其余空闲页仍由物理页分配器管理，MemorySet不会取得它们的数据页所有权。

### 第2题：可写区边界与BSS

问题：为什么可写区从 sdata 开始，而不能直接从 sbss 开始？堆、启动栈和静态任务栈各落在哪里？

**作者原答：**

> text是只读只执行的二进制代码区，不能更改，然后就是data存变量等内容，bss里是堆栈，这几个东西不能混为一谈

**审查：** 已注意到代码与可写数据需要不同权限，但BSS不等于堆栈；
还需要结合本项目链接布局说明sbss之前有哪些内容会被漏掉。

**修正版：**

text是代码，本项目设为R|X，可以读和执行，不能写；rodata存只读数据，设为R。
data通常保存带初始值的可写静态数据；BSS通常保存零初始化的静态数据。

本项目把静态堆后备数组和任务栈数组放在普通BSS，启动栈则在.bss.stack，位于sbss之前。
从sbss开始映射会漏掉data与启动栈；[sdata,ekernel)才覆盖整个可写内核区域。
因此本批合并它们的RW权限映射，但这不表示这些对象在概念上或生命周期上相同。

### 第3题：错误传播、Drop与活动页表

问题：构造第四个区域时 map 返回 Err，前三个区域的表页怎样归还？为什么可以不实现逐页 unmap，
同时又不能直接把同样做法用于已经写入 satp 的活动页表？

**作者原答：**

> 失败了有rust机制的?兜底，会自动释放的，但是活动中的页表不是遇到错误了，你依然是需要自己管理的

**审查：** 已注意到错误传播时的资源清理，但不是?本身执行释放。
活动页表不能直接销毁的原因是CPU仍在使用它，与是否发生错误无关。

**修正版：**

map的Err经?提前返回，局部PageTable与区域Vec随作用域结束执行Drop，表页拥有者归还已经分配的表页，
Vec释放自己的元数据缓冲区。没有半成品地址空间返回，且这张表尚未被硬件使用，因此可以整体销毁，
不必先逐页解除映射；被描述的内核数据页也不归这个对象释放。

活动页表即使对应的软件变量离开作用域，CPU仍可能通过satp和缓存的地址翻译使用它。
直接Drop会把硬件还在使用的表页交还分配器，随后可能被复用。
必须先切换到其他有效地址空间或在当前身份映射前提下恢复Bare，完成必要的地址翻译缓存刷新，
确认旧表不再被使用，然后才能释放。Rust的普通Drop不能自动得知CPU是否仍在使用它。

## 10. 来源与本批取舍

固定官方 ch4 `f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79`：
[memory_set.rs](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/mm/memory_set.rs)、
[linker-qemu.ld](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/linker-qemu.ld)，
教材对应[内核与应用的地址空间](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/5kernel-app-spaces.html)。

本批只取地址空间抽象与内核恒等映射；不提前搬入官方的 Framed、ELF、trampoline、activate。
只支持恒等映射，所以暂不增加只有一个取值的 MapType 枚举；后续需要应用数据页时再扩充。
以整体构造接口限制部分失败的外部可见性，保留作者已完成的底层页表函数。
官方完整 ch4 未在本环境构建验收，本批参考核对不等于验证整个官方工程。
