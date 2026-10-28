# BATCH19：在独立页表下运行 ELF 并接回用户 Trap

2026-10-24：作者已完成A/B/C并移除守卫，BATCH19功能验收通过，三题已集中复核并记录。
独立只读复核无必修问题；实际作者代码无警告构建、QEMU主动退出0，全部既定P0通过。
下面保留原任务与交付预检供复习，作者验收记录见第10节。
位置：第四章“基于地址空间的分时多任务”，先打通一个分页用户应用的执行路径。

## 1. 本批解决什么

B17已经把ELF的段复制到物理页，并构造用户页表；B18已经能在共享跳板上切换两张表。
现在把它们接起来：运行真实的image_demo，从用户态收到exit，再回到原内核启动调用。

```text
启动Rust（Bare）
  → 准备内核表、ELF用户表、分页用户现场
  → 启用内核表，保存启动调用的返回位置
  → 高跳板：切用户表，恢复现场，sret
  → U-mode：用户_start → 清BSS → main → exit(0) → ecall
  → 硬件进入S-mode，仍使用用户表，跳到高地址trap入口
  → 高跳板：保存用户现场，切内核表，恢复内核栈
  → 低地址返回桥：恢复启动调用，ret回Rust
  → 检查退出请求，恢复Bare和CSR，回收本次资源
  → 原来的两个Bare应用继续运行
```

本批的image_demo本身不打印Hello；成功标记由内核在收到退出请求后打印。
它会读取ELF中的初始化数据，并走用户运行时的正常退出路径。
这一批只处理单个应用的终止，不接入yield、timer调度或普通系统调用返回用户态。
后面再把这条分页路径接到现有任务管理器；不用在这一批同时改动旧调度器。

## 2. 页表与特权级分别在什么时候变

| 阶段 | CPU模式 | 当前satp | PC所在位置 | sp的用途 |
| --- | --- | --- | --- | --- |
| 准备与启用内核表 | S | Bare→内核表 | 低内核代码 | 启动栈 |
| 恢复用户现场 | S | 内核表→用户表 | 共享高跳板 | 读取分页现场 |
| 执行用户ELF | U | 用户表 | ELF入口及用户代码 | 用户栈 |
| ecall刚陷入 | S | 仍是用户表 | 共享高trap入口 | 先交换为现场地址 |
| 保存现场后 | S | 用户表→内核表 | 共享高跳板 | 随后换回启动栈 |
| 回到启动调用 | S | 内核表→Bare | 低内核代码 | 启动栈 |

写satp与sret承担不同职责。硬件trap也不会自动切satp或替你找内核栈。
用户表中映射U=0的跳板页，允许进入S-mode后的trap入口执行；用户代码不能直接执行它。

## 3. 为什么新增分页现场

旧的TrapContext保留原来的272字节布局，仍供旧Bare应用使用。
新模块中的PagedTrapContext保留相同的前34个机器字，再添加三个字段。
它占296字节，单独放进一个4KiB物理页；296是数据结构大小，不是Rust调用栈帧的大小。

| 字段 | RV64字节偏移 | 作用 |
| --- | ---: | --- |
| x[0]…x[31] | 0…248 | 用户整数寄存器，x[2]是用户sp，x[10]是a0，x[17]是a7 |
| sstatus | 256 | sret所需状态，以及trap保存的先前特权级 |
| sepc | 264 | 首次为ELF入口，陷入后为触发trap的用户指令地址 |
| kernel_satp | 272 | trap入口要切回的内核表token |
| kernel_sp | 280 | 本批保存启动调用续点的栈帧地址 |
| trap_handler | 288 | 本批低地址返回桥的入口 |

trap_handler沿用教程字段含义“恢复内核环境后跳往哪里”。本批填的是汇编返回桥，
桥先恢复普通函数调用约定，再让原Rust调用继续处理退出信息；不是旧trap_handler函数。
未来接调度时才会调整为持续运行的用户trap分发路径。

现场页的VA固定在TRAMPOLINE下面一页：

```text
TRAMPOLINE   = 0xfffffffffffff000  共享代码：R/X/A，U=0
TRAP_CONTEXT = 0xffffffffffffe000  自有现场：R/W/A/D，U=0，X=0
```

框架把同一现场页也映射进内核表，使设置高trap入口与切表之间的窗口仍能访问现场。
普通用户数据页仍只在用户表中按用户VA映射。内核通过已有物理页池恒等映射读取自己拥有的现场页。
现场页在用户表中出现，并不表示用户有权限改它；否则用户就能篡改kernel_satp和内核入口。

## 4. 启动调用怎样等用户回来

低地址__paged_enter按普通C调用约定进入，先在启动栈上保留ra、gp、tp和s0…s11。
15个机器字共120字节，栈帧取128字节维持16字节对齐；这个帧地址写入kernel_sp。
这里保存的是内核稍后继续执行所需的状态，与296字节用户现场分属两个对象。

用户运行时，原Rust调用链保持挂起。用户执行ecall后，跳板把所有用户整数寄存器写进现场页，
加载三个内核字段，切回内核表和启动栈，然后跳到__paged_return。
返回桥恢复内核的ra/gp/tp/s0…s11，普通ret回到__paged_enter的调用处。
此时Rust才能安全运行、读取保存的用户现场并处理退出。

本批退出后不会再次sret恢复这个用户应用，因此保留退出ecall的sepc便于检查，
无需推进sepc或把返回值写回用户a0。以后处理会返回用户的系统调用时，才需要相应操作。

## 5. 所有权、同步与异常窗口

内核表、ELF用户空间和现场页都由本批私有作用域保活。
用户态运行期间没有指向现场内容的Rust引用跨越汇编写入；返回后重新读取。
上下文映射助手只安装指定页的映射，物理页所有权仍属于本批的FrameTracker。
两个根表都必须在恢复Bare并完成SFENCE之后销毁，随后才能释放现场页。

每次切satp后都执行全量sfence.vma。ELF字节此前通过数据写入新页，
首次执行前还需要fence.i；前者同步地址翻译，后者保证本hart后续取指看见已写入的机器码。

B18的高fatal入口要求sscratch保存内核token，本批的trap入口要求它保存现场VA，不能混用。
本批框架准备好现场后才设置自己的高trap入口；保存现场后若发现SPP=1，走已有低地址fatal诊断。
普通U trap先恢复内核表，再设置低fatal入口并跳回低地址，避免在高PC直接恢复Bare。
这些保护接线已提供，不要求作者另写异常矩阵。

本演示开始时要求Bare、SIE=0、sie=0，不启动定时器。
初始现场也清除SPIE，防止sret把SIE置1；sie=0使U-mode期间也没有启用的S中断源。
只运行整数示例，关闭FS/VS，不保存浮点或向量现场。保留UXL等已有合法配置。

## 6. 作者完成的三处核心

按A→B→C完成后，一次性检查。重复的寄存器保存/恢复、入口接线、资源检查均由框架提供。

### A：PagedTrapContext的初始化

在mm/paged_user.rs的BATCH19 A处：

1. 创建全零的32项寄存器数组，把参数user_sp放进x[2]。
2. sepc设为entry；sstatus用传入状态清除框架列出的禁止位。
3. kernel_satp使用传入的内核token；kernel_sp先为0，由低入口建立真实续点帧后填写。
4. trap_handler指向框架提供的低地址__paged_return。

必须清除SPP/SIE/SPIE/SUM/MXR/FS/VS。前3项决定初始特权和中断状态，
SUM/MXR保持严格的访存语义，FS/VS为本批未保存的浮点/向量状态。
位号已经在骨架注释列出，对应需要清除的掩码为：

```rust
let mask = (1usize << 8) | (1 << 1) | (1 << 5) | (1 << 18) | (1 << 19)
    | (3 << 13) | (3 << 9);
```

用status & !mask得到初始状态，不需要改旧TrapContext构造器。

### B：恢复用户表与用户CSR

在mm/paged_user.S的BATCH19 B处，进入时a0是高现场VA，a1是用户token：

1. 把a1写入satp，紧接全量sfence.vma。
2. 把a0复制到sp，此时sp用于读取现场，尚不是用户栈。
3. 从256(sp)、264(sp)读出状态与入口，分别写入sstatus和sepc。
4. 把现场VA写入sscratch，为下一次trap交换sp做准备。

后面的寄存器恢复代码已提供。它最后从16(sp)恢复真正用户sp，执行sret进入U-mode。
不要提前恢复用户sp，否则后续ld会从用户栈错误地读取“现场”。

### C：陷入后切回内核环境

在mm/paged_user.S的BATCH19 C处，用户整数寄存器及sstatus/sepc已经保存：

1. 把272(sp)的kernel_satp读入t0，280(sp)的kernel_sp读入t1，288(sp)的trap_handler读入t2。
2. 将t0写入satp，紧接全量sfence.vma。
3. 把t1复制到sp。后续框架设置低fatal入口、检查异常来源并跳t2。

必须在换sp之前把三个字段取完，否则sp已不再指向现场。
t0/t1/t2可以暂用，因为用户原值已经保存。它们的值不是三个不同用户的状态，而是本次trap的返回路线。

完成三处后移除paged_user.rs中的compile_error守卫。无需修改旧trap.S、task、syscall或用户程序。

## 7. 本批验收与资源数字

最高现场页与跳板页共用已有高端L0，因此额外增加1个数据页，不再增加页表页。

| 资源 | 页数 |
| --- | ---: |
| 内核页表 | 11 |
| 用户页表 | 5 |
| 用户ELF与栈数据页 | 6 |
| 独占的分页现场页 | 1 |
| 总页池增量 | 23 |

共享跳板仍来自内核镜像。启动续点帧使用原启动栈，不另从页池分配。

P0由协作者承担，范围提前固定：

- 正常路径：真实ELF入口→U-mode→exit(0)→S-mode→原启动调用；scause=8、保存SPP=0、a7=93、a0=0。
- 核心不变量：现场RW/U0/NX，跳板RX/U0；23页，用户栈范围正确，陷入sepc指向ecall；
  回来后为内核token，最终Bare/原CSR恢复，free=3008、in_use=0、堆恢复基线。
- 唯一拒绝路径：把未映射guard页作为候选用户入口，返回Err且CSR、页数和堆不变；校验函数不写现场。
- 在同一次启动中观察B14…B18标记及原两应用timer/退出，程序正常关机返回0。

P1选做：只读追踪x[2]、kernel_sp和sscratch在各阶段的值，新代码0行。
P2可选：用户非法指令/坏权限等额外故障探针，约数十行；有需要再安排，不阻塞继续。

```sh
cd /home/asus/tinyos/experiments/rcore-learning
# 完成A/B/C并移除guard后
make build
timeout 8s make run
echo $?
```

本批只要既定P0通过，再作三题理解复核；无需补全整个syscall或调度器才算完成。

预期新增标记如下。原应用继续正常运行后，QEMU主动退出，最后echo输出0。

```text
[paged-user] shared RX/U0=true context RW/U0=true in_use_delta=23
[paged-user] unmapped_entry=Err unchanged=true
[paged-user] enter U-mode entry=0x40000000 user_sp=0x40007000
[paged-user] U ecall: id=93 code=0 SPP=0 observed_satp=true
[paged-user] restored: satp=Bare CSR=true free=3008 in_use=0 heap_restored=true
```

## 8. 完成后的三个理解题

1. ecall刚触发时CPU、satp和sp各发生什么？为什么高跳板和现场页都必须是U=0，却仍出现在用户表里？
2. x[2]、kernel_sp和启动续点中的ra各保存谁的信息？为什么恢复用户sp要放在现场ld之后，切回Rust前又必须恢复gp/tp和内核栈？
3. 相较B18为何只多1页？为什么本批exit不做sepc+=4，而销毁资源之前仍要恢复Bare并同步？

### 原回答、审查与修正版（2026-10-24）

**第1题原答：**

> ecall触发时候，cpu特权级从U->S，satp和sp不变
> 因为需要中转造访用户表，不在用户表映射没办法跳转

**审查：**特权级与satp/sp的变化答对了。“需要中转”应具体到硬件进入S-mode后仍使用用户表。
跳板页支持取指，现场页支持保存寄存器，两者都需要在切到内核表之前可访问；U=0的保护原因原答未覆盖。

**修正版：**本项目中U ecall委托给S-mode处理，硬件记录sepc/scause等状态、更新sstatus并跳转stvec，
但不会切satp或sp。高跳板必须在当前用户表中映射，才能执行trap入口；现场页必须可由S-mode访问，
才能先保存用户寄存器再切内核表。两页均U=0，阻止用户直接执行跳板或篡改现场中的kernel_satp、kernel_sp和内核入口。

**第2题原答：**

> x2保存当前用户栈顶，kernel_sp是内核栈顶，ra是内核情况下的返回地址，因为要返回到原来的位置，但不能污染当前使用的t0,t1

**审查：**用户/内核状态的归属方向正确，但“栈顶”需要区分固定区域上界与运行中的sp。
最后恢复用户sp的原因是保留现场的寻址基址，与避免污染t0/t1无关。

**修正版：**x[2]保存被打断时的当前用户sp，初始才设为用户栈区域上界。
kernel_sp保存内核在启动栈上保留128字节续点帧后的sp，指向已保存的内核寄存器。
续点帧中的ra让__paged_return通过ret回到原内核调用处；用户自己的ra另保存在现场x[1]。
前面的ld都以sp定位现场，因此必须等其他字段读取完成后，最后执行ld sp,16(sp)恢复用户栈。
切回Rust之前先恢复内核表、内核栈及gp/tp等调用环境，才能让原Rust调用继续安全执行。

**第3题原答：**

> 因为程序仍要使用，而这伴随着包括压栈在内等各种操作，但我们不希望污染用户和内核栈
>
> sepc是ecall自己的指令地址，我们自然不希望一直执行，但是exit的时候不会执行下一次了
>
> 释放资源的时候，我们需要保证自己不会被自己绊脚，如果不恢复Bare的话，我们每次查询依然通过satp指向的根页表，如果此时来了TLB缓存刷新，会直接暴毙

**审查：**exit不恢复用户这一点答对了，也抓到了不能释放活动页表的危险。
额外一页实际用于分页现场，不是新栈。硬件缺失缓存翻译时随时可能查表，不必等一次主动TLB刷新才出现危险。

**修正版：**B19比B18只多一份现场物理页；用户栈已由B17建立，内核续点仍放原启动栈。
现场页与跳板页共用已有高L0，所以额外表页为0、额外数据页为1，总计23页。
退出应用不再恢复执行，保留ecall的sepc即可；会返回用户的普通系统调用才需要推进sepc并准备返回值。
回收前必须先停止使用旧空间并完成地址翻译同步。本批在内核表下回到低地址代码，恢复原Bare并执行SFENCE，
再销毁两个根表及数据所有者。Bare切换本身立即生效；SFENCE处理旧翻译和页表访问同步，不负责释放内存。
长期运行的内核可以改为切到另一张持续有效的页表后回收旧空间，不要求每次退出都进入Bare。

三题已集中讲解；修正版包含协作者补充，不把补充内容计作原答已独立覆盖。
Bare专题及章节整体整理见[章节总结索引](../../docs_from_riscv/rcore/README.md)和
[Bare、Sv39与Trap](../../docs_from_riscv/rcore/BARE_SV39_TRAP.md)。
作者选择先整理当前内容，本次不准备下一批。

## 9. 来源与本批取舍

现场布局与跳板路线对照固定ch4的
[context.rs](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/trap/context.rs)和
[trap.S](https://github.com/rcore-os/rCore-Tutorial-v3/blob/f384222b9b5bf33c4eb6b1a87ad5a7a2b8122c79/os/src/trap/trap.S)。
本批保留独立闭环与可返回启动演示，未照搬官方已经接好调度器的处理函数。

特权/页表语义见[RISC-V Supervisor规范](https://docs.riscv.org/reference/isa/v20240411/priv/supervisor.html)，
机器码写入后的取指同步见[Zifencei规范](https://docs.riscv.org/reference/isa/v20240411/unpriv/zifencei.html)。

## 10. 交付预检

协作者在`.cache/batch19-preflight-z1ndj85c/`临时副本仅补齐A/B/C并移除守卫，
离线构建无警告，一次timeout 8s make run主动退出0。上述五条标记和既定P0通过：
实际U-mode发出exit93/code0，保存SPP=0、用户satp与栈/退出指令正确；
8项CSR恢复、23页归还、堆恢复，B14…B18及旧双应用各三轮/双向timer/各exit0/allExited正常。
日志、补全位置、源码哈希和结果见该目录的qemu.log、patches.json与results.json。
交付时工作区源码配置在预检期间未变，保留三处待补及编译守卫。
当时工作区make build只因预期B19守卫失败，无额外错误或警告。

结构/所有权与指令顺序已由主协作者静态复核；设计另经只读复核。
批前涉及旧文件的对照存于`.cache/batch18-before-paged-user-dn1grw6j/`，
它是按本次精确新增块反向重建的对照，不是修改前实际保存的全量快照。
交付时另对照B18最终验收实际保存的os/user哈希：已有源码只改mm/mod.rs和memory_set.rs的必要接线。
将工作区三处TODO按预检补全后，与已通过的副本逐字一致；核对记录见`.cache/batch19-delivery/`。
临时副本通过只确认交付骨架和路径可用，不代表作者已经完成核心。

**作者实现验收（2026-10-24）：**A正确使用status & !mask；B先切用户表/同步再恢复CSR，
C正确按272/280/288加载内核token、sp及返回入口，再切表/同步/换栈。独立只读复核无必修问题。
一次timeout 8s make run无警告、主动退出0，五条B19标记和全部既定P0通过；
8项CSR恢复、23页归还、堆恢复、原B14…B18及双应用各三轮/双向timer/各exit0/allExited正常。
证据`.cache/batch19-author-review-r7e0xtq3/`，作者os/user源码配置运行前后哈希一致；未代改核心、扩展矩阵或运行C主线。
本批功能通过，第8节三题已集中复核并记录；随后停在文档整理，不准备下一批。
