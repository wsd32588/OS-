# BATCH17：从 ELF 文件构造用户地址空间

2026-10-21：作者已完成三处核心并移除编译guard，BATCH17功能验收通过。
2026-10-22：三题已集中复核，原答、审查与修正版记录在第9节。
作者运行输出确认五条ELF检查标记、all applications exited与退出0；源码审查结果见第8节。
下文保留原任务与预检说明供复习，临时副本预检和作者实际运行分别记录。

## 1. 在第四章的哪里

已经具备：物理页分配、页表、区域描述、Framed数据页所有权、跨页初始化，
以及一次完整的内核Sv39启用/恢复试验。

接下来把之前的模拟字节换成真正的ELF文件：

```text
ELF文件
  → 读取程序头中的PT_LOAD
  → 按虚拟范围和权限规划Framed区域
  → 加一页guard、两页用户栈
  → MemorySet::new分配数据页并建表
  → 将各段文件初值复制到各自p_vaddr
  → 得到MemorySet、entry和用户栈顶
```

本批只构造、查询和读回，satp保持Bare。新空间尚缺运行所需的内核/trap切换安排，
不能直接激活。旧hello/hello_peer、静态用户栈和调度器照常运行。
后续再接trampoline、用户现场、页表切换及实际应用执行；不把它们混进本批。

## 2. ELF不等于BIN，段不等于节

BIN主要提供平铺的字节，旧loader已经知道固定入口与装载位置。
ELF还包含入口和程序头，告诉装载器各段应出现在哪个VA、占多少内存、具有哪些权限。
本批只读**程序头表**，不依赖节名，也不把整个ELF文件原样复制到用户VA。

| 字段 | 本批用途 |
| --- | --- |
| e_entry | 用户程序入口，之后用于初始sepc |
| p_type=PT_LOAD | 需要装入内存的段 |
| p_offset | 初值在ELF文件中的字节偏移 |
| p_vaddr | 初值在用户地址空间中的起点 |
| p_filesz | 从文件复制多少字节 |
| p_memsz | 段在内存中一共需要多少字节 |
| p_flags | ELF的读、写、执行权限 |
| p_paddr | 本批不用于选择物理页，PPN仍由frame_alloc决定 |

例如RW段的filesz=8、memsz=5016：文件只提供8字节初值，其后5008字节必须为零。
其中包括布局填充与BSS。本项目新分配的整页已清零，复制这8字节后保留其余部分即可。
不能用memsz作为文件切片长度，也不能复制ELF头到程序入口。

ELF可以让一个LOAD包含多个节。本次数据段同时包含.data和.bss，装载器无需分别寻找它们。

## 3. 权限必须逐位翻译

| 含义 | ELF段标志 | 页表标志 |
| --- | --- | --- |
| 执行 | PF_X=1 | PTE_X |
| 写 | PF_W=2 | PTE_W |
| 读 | PF_R=4 | PTE_R |

不能把p_flags直接当作PTE flags。所有应用段增加U和A；可写段再增加D。
V由PageTable::map统一加上，不在这里加入。
U仍然只是访问权限，构造ELF空间不会自动改变CPU特权级。

## 4. 字节范围、页范围、guard与栈

每个段的实际字节范围为[start, memory_end)，描述区域时向外取整：

```text
page_start = start & !(PAGE_SIZE - 1)
page_limit = page_end(memory_end)?
```

`page_end`是本批已经提供的受检向上取整函数；已对齐的地址不再额外增加一页。
MapArea::new_framed仍要求严格对齐，不要修改原构造器来偷偷接受任意边界。

复制初值必须使用**原始segment.start**，不能使用向下取整后的page_start；
二者不相等时，前者才能保留段在第一页中的偏移。

遍历所有段时取最大的page_limit作为guard_start，不依赖“最后一条段就是最高地址”。
[guard_start, guard_start+PAGE_SIZE)不建映射，之后放两页RWU用户栈。
所有栈地址加法用checked_add，MapArea构造器继续检查最终低VA边界。

本批不合并同页的不同段。若向外取整后重叠，MemorySet::new会返回Err。
这是当前装载器的支持范围，**不是说这样的ELF文件本身非法**。
原hello的text/rodata就共享一页，所以本批增加独立ELF示例，不更改旧应用布局。

## 5. 已接好的基础设施

- `os/src/mm/elf.rs`：有界ELF读取，输出ElfImage与借用文件切片的LoadSegment。
- `os/src/mm/user_image.rs`：UserImage/UserLayout、page_end和三处作者核心。
- `os/src/mm/elf_demo.rs`：既定P0演示及编译guard。
- `os/src/mm/mod.rs`：在B16演示结束后接入ELF演示。
- `user/src/bin/image_demo.rs`、`user/src/paged.ld`：独立ELF示例，三种权限和真实BSS。
- Makefile的`user-elf`/`user-elf-inspect`：单独链接至0x40000000，保留ELF结构并剥离调试信息。

新示例使用`user/target/paged/`，不会替换旧应用的构建输出或原linker.ld。
没有新增Cargo依赖。ELF解析细节由协作者提供，本批作者练习聚焦装载与地址空间。

解析器限定ELF64、小端、RISC-V、静态ET_EXEC：核对头、程序头表与文件范围，
最多32个程序头/8个非空LOAD，向外取整后的段内存总量最多128KiB。
检查filesz<=memsz、地址和文件偏移的受检加法、对齐同余及权限；拒绝空LOAD、
动态链接、解释器、TLS、无效入口和不支持的位。入口必须在可执行LOAD的实际内存范围内。
其他非装载程序头可忽略。此限制不宣称支持所有合法ELF，节表也不是本批装载依据。

`LoadSegment`提供：

```rust
start: usize,          // 原始p_vaddr，尚未向下取整
memory_end: usize,     // p_vaddr + p_memsz，已检查
data: &'a [u8],        // 已检查的文件初值，长度=p_filesz
flags: u32,           // 已检查的PF_R/W/X
```

借用只存在于解析/复制期间，返回的UserImage不借用ELF文件；初值已复制到自己拥有的数据页。

## 6. 作者完成三处核心

先阅读整个user_image.rs，再一次完成A/B/C。

### A：segment_permissions

从PTE_U | PTE_A开始，按flags & 4、flags & 2、flags & 1分别加入R/W/X；
W时同时加入D，最后返回u64权限。解析器已检查组合是否合法，无需重复写ELF解析器。

### B：plan_layout

函数开头已预留areas容量。需要完成：

1. 设置最大页尾的累积变量，遍历image.segments。
2. 计算每段向外取整后的范围，用A的权限生成MapArea::new_framed并push。
3. 更新最大页尾，作为guard_start。
4. checked_add一页得到stack_bottom，再加USER_STACK_SIZE得到stack_top。
5. 将RWU|A|D栈区域加入areas，返回UserLayout四个字段。

这里只生成描述，不分配物理数据页，不向guard插入区域。
区域之间是否重叠仍由整体MemorySet::new集中检查。

### C：UserImage::from_elf

依次调用elf::parse(data)?、plan_layout(&image)?、MemorySet::new(&layout.areas)?。
最后一个结果保存为可变memory。遍历image.segments：

```text
VA = VirtAddr::new(segment.start).ok_or(...)?
memory.copy_data(VA, segment.data)?
```

返回Self，填入memory、image.entry和三个布局边界。
出错时局部所有者自动销毁；不泄漏半成品，不写satp，不改旧loader。

三处完成后删除elf_demo.rs第一行compile_error。原TODO说明注释可暂时保留。
无需修改B16的copy_data、FrameTracker或PageTable，也无需另写测试。

## 7. 固定示例与预期资源

本次ELF包含3个LOAD：

| 段 | VA起点 | filesz / memsz | 权限 | 映射页数 |
| --- | --- | --- | --- | --- |
| text | 0x40000000 | 116 / 116 | RXU | 1 |
| rodata | 0x40001000 | 17 / 17 | RU | 1 |
| data+BSS | 0x40002000 | 8 / 5016 | RWU | 2 |
| guard | 0x40004000 | 不装载 | 无映射 | 0 |
| 用户栈 | [0x40005000,0x40007000) | 全零 | RWU | 2 |

text的具体字节数是本机固定工具链的观察值，功能验证不把116写成通用ELF要求。
这6个数据页都在同一个2MiB虚拟范围，共用3个表页，总共9页。
当前基线free=3008/in_use=0，构造后free=2999/in_use=9，销毁后回到原值。

预期输出：

```text
[elf] LOAD=3 entry=0x40000000 tables=3 data=6 in_use_delta=9
[elf] file_bytes=true bss_zero=true permissions=true
[elf] guard=0x40004000 stack=[0x40005000,0x40007000) zero=true
[elf] truncated=Err unchanged=true
[elf] released: free=3008 in_use=0 heap_restored=true satp=Bare
```

## 8. 验证要求与命令

P0由协作者承担：正常ELF的完整文件初值/零填充、精确权限、入口/guard/栈边界、
6数据+3表帧、完整回收及Bare保持；一条拒绝路径为63字节截断头返回Err，
原已装载内容与页数/堆不变。同时观察原B14/B15/B16和双应用抢占/退出。
不增加故障矩阵，也不把临时副本通过记作作者完成。

P1选做：只读追踪一次copy_data失败时UserImage构造器的局部Drop顺序，新代码0行。
P2选做：一般ELF兼容、共享页权限合并和完整输入矩阵，预计数十至数百行，不阻塞本批。
真正执行、动态链接和文件系统读ELF不在本批范围。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
make user-elf-inspect
# 完成三处核心、移除guard之后：
make build
timeout 8s make run
echo $?
```

最初骨架保留guard；作者现已完成三处并移除guard，实际运行主动退出0。
批前快照：`.cache/batch16-before-elf-5l55xaab/`。
临时预检：`.cache/batch17-preflight-jwhfd3ab/`，只在副本补齐三处TODO并移除guard。
离线构建无警告、QEMU主动退出0，以上P0以及原B14/B15/B16和双应用观察均通过。
预检期间作者os/user源码配置哈希一致；结果见results.json与qemu.log。
这证明交付骨架与演示可以接通，不代表作者已经完成三处核心。
交付时独立复核无必修问题；当时骨架构建仅因预期guard失败，无额外错误或警告。
最终骨架与预检实现的对应核对见`.cache/batch17-delivery/delivery-checks.json`。

**作者实现验收（2026-10-21）：**作者已提供全部五条ELF成功标记、
`[kernel] all applications exited`及退出码0。当前A/B/C源码与这些检查相符，
确认段权限转换、向外取整/最大段尾/guard与栈、原始VA初值复制及错误传播正确。
协作者仅补一次make build核对无警告，不重复已有成功QEMU运行；运行证据明确来自作者。
相对交付骨架，os/user仅user_image.rs三处实现与elf_demo.rs的guard移除发生变化，其余基础设施保持。
记录与源码哈希见`.cache/batch17-author-review-gj613wx2/`；随后三题复核记录如下。

## 9. 完成后的三个理解题

1. p_offset、p_vaddr、p_filesz、p_memsz各控制哪件事？为什么BSS无需在文件中占同样多字节？
2. ELF的flags为什么不能直接写进PTE？页范围向外取整后，为什么仍须从原p_vaddr复制？
3. 为什么guard取所有段最大页尾，用户栈不从ELF复制？拿到entry和栈顶之后，为什么还不能直接激活这个页表？

### 作者原答与集中复核（2026-10-22）

**第1题原答：**

> p_offset控制文件在ELF内的偏移量，vaddr控制在用户空间地址起点，filesz代表从文件复制多少字节，memsz表示总共在段内占据多少字节

**审查：**四个字段的理解基本正确；p_offset准确指当前段初值在ELF文件中的偏移，
p_memsz指段的内存字节数，不是向外取整后的整页占用。原答未解释BSS为何无需保存同样多文件字节。

**修正版：**p_offset确定从ELF文件哪里读取，p_vaddr确定复制到用户空间哪里，
p_filesz确定复制的初值字节数，p_memsz确定段在内存中所需的总字节数。
当p_memsz大于p_filesz时，剩余部分必须为零；BSS只需记录大小，由装载器提供零填充，
无需在文件中保存同样多的零字节。本项目通过新分配页清零，再仅复制filesz实现。

**第2题原答：**

> 因为要准确表示权限，应用需要A|U，但不一定需要D，因为可能在前一页才是开始的位置，如果直接使用取整后的地址可能会丢失内容

**审查：**注意到额外权限位和原始起点有价值，但不能直接使用ELF flags的关键是位位置不同。
从取整后的地址复制，主要问题是内容发生地址错位，不一定丢失字节。

**修正版：**ELF的PF_X=1、PF_W=2、PF_R=4，而PTE_R=2、PTE_W=4、PTE_X=8，
必须逐位翻译。本项目应用段再加入U、A，可写段再加入D，V由map统一加入。
这些是本项目的初始化策略；U是页访问权限，A/D用于访问与写入状态管理。
映射范围需要向外取整以覆盖整页，但初值必须从原p_vaddr开始复制。
例如p_vaddr=0x40002003，映射从0x40002000开始，数据仍应从页内偏移3写入；
若从页首复制，内容提前3字节，程序按原地址访问会读取错误内容。

**第3题原答：**

> 保证用户和程序永远不重叠，留下足够大的隔离带，ELF并不保存用户栈数据，而且ELF载入的话会直接挤爆内核栈，有了入口和栈顶但是这张页表不一定已经启动并且映射

**审查：**程序代码、数据与用户栈都属于用户地址空间，不是“用户和程序”两者隔离。
最大段尾用于避免栈与装载段重叠；一页guard只能拦截访问该未映射页的越界，不能保证所有越界均被捕获。
ELF段复制到FrameTracker拥有的物理数据页，不放进内核栈，因此不是为避免“ELF挤爆内核栈”。
本批新页表已经构造并有用户映射，只是未激活；不能直接激活的原因是缺少内核继续执行及trap切换所需安排。

**修正版：**取全部段的最大页尾，再留一页guard和用户栈，使栈位于所有装载段之后，避免布局重叠。
guard页不映射，访问它会触发异常，但跨越该页的更大越界不保证被拦截。
ELF通常不保存用户程序运行过程中产生的栈内容，本批单独分配全零用户栈。
新空间已经建立用户段和栈映射，却未提供当前内核取指、内核栈、trap入口及页表切换所需的完整安排。
直接写satp可能立即使当前S-mode代码无法继续取指或访存；只有入口和用户栈顶还不足以安全运行它。

三题已集中讲解；以上补充不记作作者原答已独立覆盖的内容。本次只记录答案，不准备下一批。

## 10. 来源与取舍

固定官方ch4的[from_elf](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/mm/memory_set.rs)
提供PT_LOAD→Framed→复制初值→guard/栈→返回入口的路线；教材仍在
[内核与应用的地址空间](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/5kernel-app-spaces.html)。
字段、零填充及段权限依据[ELF Program Loading](https://gabi.xinuos.com/elf/07-pheader.html)。

官方用xmas-elf读取格式。本批由协作者提供受限安全解析helper，保持当前离线依赖不变，
将作者练习集中在装载和已有MemorySet之间的连接；不宣称helper可替代通用ELF工具。
继续沿用整体构造而非动态push区域，使用真正最大段尾，所有分配有唯一拥有者。
暂不引入官方from_elf里的trampoline、TrapContext页与空堆描述。
