
# BATCH18：共享跳板页与真实页表切换

2026-10-22：作者已完成三处核心并移除guard，BATCH18功能验收通过，三题已集中复核并记录。
两次切表均在写satp之后执行SFENCE；实际无警告构建、QEMU主动退出0。
下文保留原任务与预检说明供复习，作者实现验收见第8节。

## 1. 本批解决什么

我们已经能从ELF得到用户页表，但它没有映射普通内核代码与内核栈。
如果在普通Rust函数中直接切到这张表，当前PC和栈的数值不会自动改变，程序就可能无法继续执行。

本批在两张表中加入同一条公共映射：

```text
内核表：VA 0xfffffffffffff000 → 内核里固定的一页跳板机器码
用户表：VA 0xfffffffffffff000 → 同一物理页
```

然后在这个高VA上运行一段无栈汇编，真实完成：

```text
Bare启动
  → 构造内核表和ELF用户表，加入公共页
  → 激活内核Sv39表
  → 跳到公共高VA
  → 写入用户satp并同步
  → 在用户表期间读取satp
  → 写回内核satp并同步
  → 返回低地址Rust调用点
  → 恢复Bare及原CSR，销毁临时空间
  → 原双应用照常运行
```

**本批全程S-mode。**“用户表”描述地址空间，不表示CPU已经进入U-mode。
本批尚不运行ELF入口、不使用用户栈，也不改旧TrapContext和调度器。
下一批再把用户现场与真正U/S往返接到跳板上；不把完整第四章运行方案一次塞进本批。

## 2. 为什么使用同VA、同PA

写satp不会把PC自动重定位到另一地址。切换附近的指令必须在两张表下都能正确取到。
同VA映射同PA，让过渡中的取指对应同一段机器码。

两表都使用ASID0，切换根表不会自动清除之前的翻译缓存；每次写satp后都要执行本批约定的
`sfence.vma zero, zero`。不能把描述简化为“写完satp，下一次取指必定重新遍历新表”。
公共映射保证过渡可用，同步完成后再依赖新地址空间。

两表各自拥有不同的页表页，但借用同一份跳板代码页。它不是给用户复制的新代码页，
不需要frame_alloc，也不加入data_frames；链接在内核里的机器码会一直存在。
另存两份完全一致的机器码也可能构造可行方案，但需要额外保证内容、布局与同步一致；
本项目直接共享同一物理页，避免引入这些额外条件。

## 3. 地址与权限

```rust
TRAMPOLINE = usize::MAX - PAGE_SIZE + 1;
```

它是Sv39最高canonical页的起点，即0xfffffffffffff000。
不计算“最后一页的排他结束地址”，因为超过usize::MAX会溢出。
最后一个合法页内偏移是PAGE_SIZE-1。

公共页权限为R|X|A，map自动加入V，**U=0、W=0**。
代码只在S-mode执行；以后U程序ecall陷入S-mode时，也可以在尚未切表时进入这个S级入口。
U位与当前CPU模式仍然是两个概念。

原MapArea构造器只支持低VA区域，因此这里直接调用PageTable::map映射一个VPN，
不放宽MapArea的原边界检查，不把最高地址伪装成低地址。

## 4. 链接地址如何换成公共虚拟地址

链接器提供两个边界：

- strampoline：这页在内核镜像中的低地址起点。
- etrampoline：低地址的排他结束边界，用于验证整页大小。

它们之间恰好4096字节，包含probe、fatal入口、地址常量及页内填充。
链接断言保证代码存在且没有超出一页，普通.text仍保持原RX映射。
内核初始化时按现有方式加载机器码，本批没有运行时修改代码，不额外增加fence.i。

某个入口符号的高地址必须保留它在页内的偏移：

```text
offset = linked_address - strampoline
alias  = TRAMPOLINE + offset
```

只有[start,end)内的符号才能这样转换。不能对所有内核函数都直接加这个偏移。
`page_bounds`已经提供受检边界读取，black_box避免相邻链接符号别名被错误优化。

## 5. 切表期间为什么不用栈

用户表不映射原内核栈。sp寄存器可以暂时保存那个数，但不能通过它读写内存。
所以两次切表之间没有Rust调用、压栈、出栈或普通内核数据访问，只操作寄存器和CSR。

本次汇编参数约定：

| 寄存器 | 入口含义 | 使用方式 |
| --- | --- | --- |
| a0 | 内核satp token | 返回前用于恢复内核表 |
| a1 | 用户satp token | 进入用户表 |
| a2 | 临时结果 | 保存用户表期间读取的satp |
| ra | 低地址Rust返回点 | 切回内核表后才能ret |
| sp | 原内核栈指针 | 汇编期间不解引用、不改变 |

RISC-V普通call将返回地址写入ra，不自动压到栈里。
Rust调用点和它可能使用的栈发生在内核表下；跳板自身无函数序言。
返回值a0最终是观测到的用户satp，而非调用时传入的内核token。
这里用ret，不用sret，因为本批不改变特权级。

## 6. 临时入口与恢复由接线提供

trampoline.rs提供封闭的启动作用域，检查Bare、SIE=0、sie=0以及完整内核映射。
它借用两套空间，保证整个切换期间表页和数据页都存活，不向外暴露通用安全激活接口。

进入时保存原stvec/sscratch，使用已有B15低地址fatal入口过渡，激活内核表后再设置公共高stvec。
sscratch暂存内核token，供公共页fatal路径恢复；本批没有用户trap栈交换，不套用旧sscratch约定。
正常退出先把stvec切回低地址fatal，再恢复Bare/同步，最后恢复原stvec和sscratch。
返回Bare并完成同步之后，才允许销毁两个空间。

高地址fatal不能直接写satp=0然后继续执行，因为下一条PC仍在高地址，Bare不会替它翻译到低PA。
提供的fatal先恢复内核表，使高公共页继续可取指，再从页内常量读取低入口的绝对地址并jr过去；
低入口随后恢复Bare、建立已有应急栈并失败关机。它是有限的失败诊断，不是通用异常恢复。

高VA下的普通PC相对寻址也会使用高PC作为基准。不能直接la/call公共页外的低内核函数。
提供的页内寻址只找到同一页的常量，常量里存的才是低入口绝对地址。
本批不要求作者编写这条保护路径，也不增加故障注入矩阵。

## 7. 已接好的文件与三处核心

- linker.ld：预留单独跳板代码页及大小断言，原BSS清零边界保持。
- mm/trampoline.rs：边界读取、CSR守卫、P0演示、受检别名函数骨架。
- mm/trampoline.S：无栈probe骨架和已接好的fatal入口。
- MemorySet新增map_trampoline骨架；原构造/拷贝/回收路径保持。
- mm::init在B17后调用演示；旧应用与依赖不改。

### A：MemorySet::map_trampoline

在memory_set.rs中完成：

1. 用super::trampoline::TRAMPOLINE构造VirtAddr，再floor得到VPN。
2. 调用super::trampoline::physical_page()?取得固定代码PPN。
3. 返回self.page_table.map(vpn, ppn, PTE_R | PTE_X | PTE_A)。

不要加入U/W，不申请数据FrameTracker，不让MapArea拥有这份静态内核代码。
只有新页表路径的表页会由PageTable分配并回收。

### B：virtual_address

trampoline.rs已验证linked位于固定代码页[start,end)内。
补齐TRAMPOLINE加上linked-start的结果，返回Ok；不改变原bounds拒绝分支。
已验证偏移小于4096，结果最大为usize::MAX，不会溢出。

### C：__trampoline_probe的两处切换

在trampoline.S中替换C1的两个nop：将a1写入satp，随后执行全量sfence.vma。
C2同样替换两个nop：将a0写入satp，随后执行全量sfence.vma。

中间的csrr a2,satp与末尾mv a0,a2/ret已经提供。
不要提前覆盖a0，不要改sp，不要在用户表期间返回低地址。
只需要两组各两条指令；不用新增循环、栈现场或Rust调用。

三处完成后删除trampoline.rs中的compile_error。无需修改旧trap汇编、task或用户程序。

## 8. 既定P0与资源数字

之前内核表9页，加入最高VA路径后多2个表页，共11。
ELF用户表原3个表页，加入高路径后为5个；它原本拥有的6个数据页保持。

```text
内核空间：11表页，0个数据owner
用户空间： 5表页，6个数据owner
总计：    22个分配页
共享代码：仍是内核镜像原有页，不额外占用页池
销毁后：  free=3008，in_use=0，堆恢复原基线
```

P0由协作者承担：核对同VA→同PA、精确RX/U0、内核/用户表的资源数字、
真实kernel→user→kernel的S-mode页表往返与用户阶段satp观测，恢复Bare/stvec/sscratch和完整回收。
唯一拒绝路径为virtual_address(etrampoline)返回Err，CSR和资源不变。
同时观察原B14/B15/B16/B17及双应用timer/退出。不要求作者另写测试。

预期输出：

```text
[trampoline] shared_page=true RX/U0=true
[trampoline] kernel_tables=11 user_tables=5 user_data=6 in_use_delta=22
[trampoline] outside_page=Err unchanged=true
[trampoline] kernel -> user -> kernel S-mode=true observed_satp=true
[trampoline] restored: satp=Bare stvec/sscratch=true free=3008 in_use=0 heap_restored=true
```

P1选做：只读追踪一次“高fatal→内核表→低fatal→Bare”路径，新代码0行。
P2可选：专门的故障注入与异常矩阵，数十行以上，留有实际需要时再做，不阻塞本批。
真正U-mode执行、TrapContext扩展和调度器地址空间切换随后安排。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
# 完成A/B/C并移除guard后：
make build
timeout 8s make run
echo $?
```

最初骨架保留guard；作者现已完成三处并移除guard，无警告构建、正常关机退出0。
批前快照：`.cache/batch17-before-trampoline-7ieb3irs/`。
临时预检：`.cache/batch18-preflight-lngxr7sm/`，仅在副本补齐A/B/C并移除guard。
离线构建无警告、QEMU主动退出0，五组跳板标记及上述既定P0全部通过；
旧B14/B15/B16/B17和双应用各三轮/双向timer/各exit0/allExited正常。
预检前后作者os/user源码配置哈希一致，记录见results.json、patches.json与qemu.log。
交付时独立只读复核无必修问题；当时作者骨架构建只因预期guard失败，无额外错误或警告。
临时副本通过不等于作者已完成核心。交付核对另见`.cache/batch18-delivery/`。

**作者实现验收（2026-10-22）：**A/B及C两组指令已完成，切回内核表的SFENCE已放在csrw satp,a0之后。
独立只读复核无必修问题；修正后一次timeout 8s make run无警告、QEMU退出0，全部既定P0通过。
共享RX/U0映射、22页资源、越页入口拒绝、S-mode真实页表往返、Bare及CSR/资源恢复正确；
原B14/B15/B16/B17及双应用各三轮、双向timer、各exit0/allExited正常。
证据`.cache/batch18-final-review-soz8pt61/`，作者源码配置前后哈希一致，协作者未代改核心或扩展矩阵。
此前SFENCE位置未修正时虽通过QEMU，静态检查未通过，旧记录仅作为历史：`.cache/batch18-author-review-n5wkiszy/`。
本批功能通过；以下三题已作答，原答、审查与修正版保留如下。

## 9. 完成后的三个理解题

1. 本项目如何用同VA映射同PA保证切表期间连续取指？如果只共享同PA，却在两张表中使用不同VA，会有什么问题？
2. 为什么使用用户页表期间CPU仍是S-mode，sp能保留原值却不能拿来访存？为什么ret前必须恢复内核表？
3. 共享代码页为何没有新FrameTracker，但页池仍多消耗4个表页？为什么高地址不能直接退回Bare，销毁前又必须先完成恢复和同步？

### 原回答与集中审查（2026-10-22）

**第1题原答：**

> 两张表分别再不同的特权位，相互隔开，使用需要使用一个VA分别映射两个表，一个PA映射再不同VA会导致两个程序可能同时使用该页表进而导致错误，比如我要S->S,他要S->U

**审查：**注意到了同VA的安排，但把页表、CPU特权级和多个程序并发混在了一起。
页表本身不处于某个特权级；本批两次切表时CPU始终是S-mode。
问题来自切表后PC仍是原来的数值，不是两个程序同时抢用页表。

**修正版：**写satp改变地址翻译配置，不改变PC。两张表在同一个高VA映射同一跳板PA，
同步之后，该VA仍能取到同一段代码的后续指令。
如果同一PA只映射在另一张表的不同VA，PC不会自动跳到那个新VA，原PC可能未映射或指向其他内容。
本项目用同VA→同PA实现这一点；这不意味着系统中不能为一个PA建立不同VA的别名。
特权变化另由trap或sret等机制处理，写satp不执行S→U。

**第2题原答：**

> 不用来回切换，保留值只是为了能恢复，但不代表可以随意滥用破坏状态，如果不用sp访存而是其他satp，那么你不容易判断到底要用哪一个寄存器

**审查：**“保留值不等于可以访问”这个方向正确，但satp并不是可以替代sp的访存地址寄存器。
本批确实进行了内核表→用户表→内核表的往返；CPU一直是S-mode，因为没有执行sret进入U-mode。

**修正版：**sp是保存地址数值的通用寄存器；satp保存分页模式、ASID和根页表PPN。
访问sp指向的内存时，MMU仍按当前satp选择的表翻译该VA。
用户表没有映射原内核栈，因此可以保留sp的原数值，却不能据此执行栈上的load/store。
普通Rust代码可能使用栈，ra里的低内核返回地址也不在用户表中，所以probe必须先恢复内核表并同步，再ret。
“用户页表”描述地址空间用途，并不表示CPU已经进入U-mode。

**第3题原答：**

> 共享代码页里状态不会随意切换，而是长时间保留的，至于4个新页表则是需要对齐状态，方便统一管理

**审查：**静态代码的长期存在与答案有关，但决定FrameTracker归属的是分配来源和所有权。
新增四页的原因是两条三级页表路径，不是对齐状态。高PC退出Bare与释放顺序原答尚未覆盖，下面补齐。

**修正版：**跳板机器码已经占用内核镜像的一页，其生命周期覆盖整个内核运行。
本次没有从页池为它新分配数据页，所以没有新数据FrameTracker。
两张根表分别为最高VA新增一个L1表页和一个L0表页，共2×2=4个表页，归各自PageTable所有。
高地址执行时不能直接改成Bare：PC仍在高地址，停止翻译后无法落到低物理代码页。
正常路径先恢复内核表并同步，ret回低内核地址，再恢复Bare并同步。
在此之后销毁两张表，才不会让硬件继续使用已经回收的表页或残留翻译。

三题的理解缺口已集中讲解；以上修正版包含协作者补充，不把补充部分计作原答已经独立覆盖。

## 10. 来源与分批取舍

固定ch4的[trap.S](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/trap/trap.S)
与[linker-qemu.ld](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/linker-qemu.ld)
给出公共跳板与切表路线。教材进入
[基于地址空间的分时多任务：建立跳板页面](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/6multitasking-based-on-as.html)。
切换与同步依据[RISC-V Supervisor规范](https://docs.riscv.org/reference/isa/v20240411/priv/supervisor.html)。

官方把跳板放进完整U/S trap路径。本批先用可返回的S-mode probe验证相同核心机制，
保留旧应用路径，下一批再接用户现场与实际用户执行。不宣称本批已经实现分页用户任务。
