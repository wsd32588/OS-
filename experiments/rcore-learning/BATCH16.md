# BATCH16：Framed 区域、数据页所有权与跨页初始化

2026-10-20：作者已完成三处核心并移除编译guard，本批功能验收通过，三题已集中复核。
实际构建无警告，QEMU主动退出0；同VA隔离、跨页复制、guard拒绝及资源恢复均通过。
独立只读审查未发现功能必修问题；下文保留原任务说明供复习。

## 1. 本批目标

目前内核恒等映射只描述已有物理内存；它不为每个映射页分配新数据页。
准备用户地址空间时，需要给应用的虚拟代码页和栈页独立申请物理内存，并保留它们的所有权。

本批增加 Framed 映射，观察两个独立地址空间：

```text
空间A：VA 0x40000000 → A拥有的物理页
空间B：VA 0x40000000 → B拥有的另一物理页
```

两个空间使用相同VA，但内容互不影响。内核通过各自的数据页拥有者初始化内容，
不能在Bare下直接把这个用户VA当指针使用。

本批保持Bare，只构造和软件查询，不切换到新用户页表，不执行其中的数据，
不解析ELF，不替换现有loader、静态用户栈或TaskControlBlock。
之后再将这个能力接入应用装载、用户态运行和持续分页。

## 2. 类型与所有权关系

```text
MemorySet
  ├─ PageTable：拥有根表及中间表页
  ├─ Vec<MapArea>：可复制的区域描述
  └─ Vec<FramedPage>：每项保存VPN和唯一的FrameTracker
```

MapType有两个取值：

| 类型 | PPN来源 | 谁拥有被映射数据页 |
| --- | --- | --- |
| Identical | 数值等于VPN | 区域不取得所有权，沿用已有内存 |
| Framed | frame_alloc分配的物理页 | MemorySet.data_frames中的FrameTracker |

Framed指独立分配，不是对任意输入都承诺VA与PA数值必不同。
本次演示选VA=0x40000000附近，物理页来自0x80440000附近的页池，明确展示非恒等映射。

MapArea仍然只描述范围、权限和映射类型，因此可以Copy。
FramedPage含有FrameTracker，不能Copy/Clone；复制VPN或PPN数字不会复制物理页所有权。
Framed也不自动等于用户态：是否允许U访问仍由PTE_U决定，内核以后也可以使用U=0的Framed区域。

## 3. 本批演示布局

每个新空间有两个Framed区域和一个未映射间隔：

| 区域 | VA范围 | 页数 | 权限（不含自动加入的V） |
| --- | --- | --- | --- |
| 模拟代码区 | [0x40000000, 0x40002000) | 2 | R\|X\|U\|A |
| guard空页 | [0x40002000, 0x40003000) | 不分配 | 无映射 |
| 用户栈区 | [0x40003000, 0x40004000) | 1 | R\|W\|U\|A\|D |

这里是新演示的VA布局，不能与旧应用的固定物理装载窗口混为一谈。
这两个空间只有用户区域，没有运行中的内核代码、栈和trap映射，不能直接写入satp。
如果绕过此前的完整内核映射检查直接激活，当前S-mode代码就可能立即无法继续取指。

所有虚拟页位于同一个2MiB区间，故每个空间共用一条三级路径，消耗3个表页。
两个代码页加一个栈页还需要3个数据页，总共每空间6页，双空间12页。

```text
两个空间存活：在用12，空闲2996
销毁空间A：  在用 6，空闲3002，空间B内容仍可读取
销毁空间B：  在用 0，空闲3008
```

这些数字依赖当前页池和演示布局，不是所有用户地址空间的固定开销。

## 4. 构造、失败与销毁顺序

MemorySet::new保留原来的全pair重叠检查，再预留区域描述与所有Framed数据页拥有者的元数据容量。
总页数使用checked_add累加，元数据容量使用try_reserve_exact；这些步骤由接线提供。
建表前已预留所有owner槽位，所以映射成功后的push不会临时扩容。

逐页构造时：

```text
分配一个清零的数据页拥有者
  → 取出PPN数字
  → map(VPN, PPN, permissions)
  → 成功后将拥有者移进data_frames
```

如果当前map失败，它没有成功发布指向当前数据页的叶项，局部frame自动归还。
构造函数随后返回Err，局部page_table最后声明、最先销毁，再销毁之前已收集的数据页拥有者。
正常返回的MemorySet则按字段声明顺序销毁：page_table在前，data_frames在后。
两条路径都避免在仍保留映射时先归还此前成功映射的数据页。

所有这些页表都尚未激活，不需要本批增加unmap或TLB刷新。
活动地址空间仍必须先停止硬件使用并完成同步，不能直接套用这里的销毁方式。

## 5. 内核如何初始化非恒等映射的内存

比如想将8个字节写到用户VA=0x40000ffd：

```text
前3字节 → 第一个拥有者页内偏移4093..4096
后5字节 → 第二个拥有者页内偏移0..5
```

每到下一虚拟页，都要重新找它对应的FrameTracker。
物理页不保证相邻，不能找到第一个PA后就一路递增指针跨页写入。
更不能把0x40000ffd直接强转成指针；当前Bare会把它解释成物理地址，不是新空间的VA。

本批copy_data是装载初始化接口：在Bare、目标表未激活、没有外部使用者时，
通过独占的frame.bytes_mut()写入。因此可以填充未来权限为RXU的代码页，
不需要把未来用户权限临时改成W，也不会绕过用户态运行期间的权限检查。
真正执行新写入的代码之前还需考虑fence.i；本批写入的只是模拟字节，不执行。

## 6. 完整范围预检

已提供validate_copy_range(start, length)：

- 零长度直接成功，不读写目标。
- 非零长度先checked_add，拒绝范围溢出。
- 整个范围必须落在同一个Framed区域内，不能写Identical区域或跨入guard。
- 写入前确认范围内每个VPN都有数据页拥有者。

即使两个Framed区域相邻，本批也不允许一次copy跨越它们；这是为了先明确单区域初始化语义。
校验全部通过后才能写第一个字节。因此跨guard的失败必须整次不写，不能留下合法前缀。
后面的owner查找可以用expect表达已经检查过的内部不变量。

## 7. 文件与三处核心

已经接好：

- `os/src/mm/memory_set.rs`：MapType、FramedPage、owner容量预留、分发、范围预检、只读回读和统计。
- 原MapArea::map循环保留为map_identical，内核Identical路径仍不取得数据页所有权。
- `os/src/mm/framed_demo.rs`：双空间演示、权限/清零/偏移/拒绝/资源检查，原编译guard已移除。
- `os/src/mm/mod.rs`：B15恢复Bare之后调用新演示，随后仍运行旧应用。

原任务为填写memory_set.rs中的三处TODO，再删除framed_demo.rs顶部compile_error；作者现已完成。
无需改PageTable、FrameTracker、paging、用户代码或Cargo依赖。

### A：MapArea::new_framed

复用Self::new(start, end, permissions & !super::page_table::PTE_U)?，
先让原Identical构造器验证边界和其他权限位。然后把原始permissions放回描述，
将map_type改为MapType::Framed，返回描述。

这里只特许U位，不能把所有不认识的位都掩掉后偷偷接受；原构造器仍会拒绝V/G/RSW等不支持位。
只生成描述，不在这个函数中分配物理页。

### B：MapArea::map_framed

遍历start_vpn.as_usize()..end_vpn.as_usize()，每页：

1. 构造VirtPageNum，已验证范围可用expect。
2. frame_allocator::frame_alloc().ok_or(...) ?取得清零页拥有者。
3. 保存frame.ppn()，调用page_table.map(vpn, ppn, self.permissions)?。
4. 将FramedPage { vpn, frame }移进data_frames。

最后返回Ok(())。不能让frame在循环末尾被销毁，也不能为复用同一页而不断复制PPN。
meta容量由MemorySet::new统一预留，这里无需再reserve或取全局帧锁。

### C：MemorySet::copy_data

第一句调用self.validate_copy_range(start, data.len())?。
随后维护copied，直到copied == data.len()：

```text
current VA = start + copied
vpn        = current.floor()
offset     = current.page_offset()
count      = min(PAGE_SIZE - offset, data.len() - copied)
```

在self.data_frames.iter_mut()中find对应vpn的owner，
将data[copied..copied+count]复制到owner.frame.bytes_mut()[offset..offset+count]，再推进copied。
数据页拥有者的借用只需覆盖当前这一块，不向Vec追加，也不用产生裸指针。
最后Ok(())；零长度经过预检后循环不进入，自然成功。

## 8. 可观察输出与P0

```text
[framed] spaces=2 each_tables=3 each_data=3 in_use_delta=12
[framed] same_va/different_pa=true cross_page_copy=true isolated=true
[framed] guard_copy=Err unchanged=true
[framed] released: free=3008 in_use=0 heap_restored=true satp=Bare
```

正常路径把ABCDEFGH和12345678分别复制到两个空间的同一VA，从页内偏移4093开始。
通过各自owner回读，确认内容、边界旁零字节、PTE权限和翻译偏移正确。
唯一拒绝路径是从代码区域最后2字节开始写BAD!，范围跨入guard，必须Err且前2字节也保持0，
已有正常内容、映射、页数和堆统计都不变。

P0由协作者承担：上述双空间隔离、12页/6页/0页生命周期、跨页初始化、guard拒绝和资源恢复；
同时保留B14/B15演示与原双应用/timer/退出观察。作者不需另写测试框架。
P1选做为只读追踪一次部分建表失败的Drop顺序，新增代码0行。
P2解除映射、动态区域追加、完整OOM矩阵、ELF装载和真正U-mode运行留后续，不阻塞本批。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make build
timeout 8s make run
echo $?
```

最初保留guard的骨架不能构建成功；作者现已完成三处并删除guard，无警告构建、默认系统QEMU退出0。
124仍是timeout结束，不代表主动关机。

批前快照：`.cache/batch15-before-framed-4ospu_pn/`。
临时预检：`.cache/batch16-preflight-f3wrnxlw/`，仅副本补齐三处核心。
离线构建无警告、QEMU主动退出0，上述P0及原B14/B15/双应用观察均通过，作者源码前后哈希一致。
记录见results.json与qemu.log；临时预检通过不等于作者实现验收通过。

作者实现验收：`.cache/batch16-author-review-37qc4oe2/`。
实际执行一次timeout 8s make run，构建无警告且QEMU主动退出0；上述既定P0全部通过。
原B14/B15演示、双应用各三轮、双向timer和各exit0/allExited均正常。
验证前后37项os/user源码配置哈希一致；未代改作者核心实现、扩展矩阵或运行TinyOS C主线。
详情见results.json与run.log；随后三题的原答及修正记录如下。

## 9. 完成后的三个理解题

1. 为什么两个空间可以使用相同VA，却得到不同PA和内容？Framed与U位分别决定什么？
2. 为什么MapArea仍可Copy，而FramedPage不能？这里相比Identical映射多拥有了哪些资源，销毁顺序为何这样安排？
3. 从偏移4093复制8字节，为什么要分成3+5？第二页为什么不能简单沿第一个PA递增？如果尾部越过guard，为何预检必须放在写入之前？

### 作者原答与集中复核（2026-10-20）

**第1题原答：**

> 因为相同VA但是不一定相同页，一个程序有一个程序的根页表，Framed决定释放拥有被映射数据页并且在非恒等映射区，U决定是否处于U-mode。

**审查：**独立根页表使同VA可对应不同PA，这个核心正确。当前演示只构造两套空间，尚未接入实际应用。
Framed表示分配并持有数据页所有权，不保证VA与PA数值必不同；U是页访问权限，不决定CPU当前特权级。

**修正版：**不同地址空间各有根页表，同一VA经过不同表可以得到不同PA，所以内容互不影响。
Framed从页池分配数据页，MemorySet通过FrameTracker保持并最终回收其所有权。
U位表示该页是否允许U-mode访问；CPU进入U-mode仍由sret等特权机制完成。

**第2题原答：**

> MapArea只有几个数据的借用而不是所有权，而FramedPage真的拥有对页的所有权，先释放页表阻止继续使用该页，数据页后释放清空数据。

**审查：**区分描述和数据页所有权正确，但MapArea保存的是普通数值字段，并非借用。
先销毁未激活页表可以先移除映射结构；活动页表必须先安全切换并同步，不能靠释放内存阻止硬件访问。
FrameTracker释放时归还页池，不负责清零，本项目在下一次分配时清零。

**修正版：**MapArea只保存VPN范围、权限和MapType，可以Copy。
FramedPage包含唯一FrameTracker，不能复制出第二份页所有权，否则会重复回收。
相比Identical，MemorySet除表页外还拥有Framed数据页；本批先销毁未激活表页，再归还数据页。
销毁表示回收资源，不表示立即擦除数据；活动空间需先停止硬件使用并完成TLB同步。

**第3题原答：**

> 一页最多4096，而4093+5 > 4096，属于不同页了，虚拟地址连续只能说明这个数据是连续的，但不一定是在物理地址上连续分布，跨越Gurd的时候要小心防止破坏该区域，该区域不允许访问读写，如果写入可能造成缺页异常，并且后续字节已经写在上面了造成了污染。

**审查：**VA连续不保证PA连续，这点正确。准确分块为4096-4093=3，剩余8-3=5。
连续VA描述逻辑地址顺序，不保证底层物理存放连续。当前copy_data通过拥有者访问物理页，
并不访问用户VA来触发guard缺页；必须由软件预检拒绝。分段检查会先污染合法前缀，而非必然写到guard。

**修正版：**第一页只剩3字节，复制后按下一VPN重新找到FrameTracker，再复制5字节。
不能沿第一页PA递增，因为下一虚拟页可能映射到不相邻的物理页。
整段越过guard时必须在任何写入前返回Err；否则可能先改掉合法页尾两个字节，随后才发现未映射页，
造成“报告失败但内容已部分改变”。本批预检保证拒绝时整段零写入。

三题已集中讲解；以上补充不记作作者原答已经独立覆盖的内容。

## 10. 来源与本批取舍

固定官方ch4 `f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79` 的
[MapType、MapArea::map_one与copy_data](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/mm/memory_set.rs)，
教材对应[内核与应用的地址空间](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/5kernel-app-spaces.html)。

官方由每个MapArea的BTreeMap持有数据页，本批集中在MemorySet的Vec中，使描述仍可复制，
并复用现有整体构造和失败清理路径。copy_data增加带偏移跨页及全范围预检，
只保留当前需要的单区域初始化能力，不搬入ELF、trampoline或活动地址空间编辑。
