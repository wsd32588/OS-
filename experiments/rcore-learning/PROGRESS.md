# 当前进度

最后更新：2026-10-25。

## 交流方式

作者新增括号/符号交流约定：{}文字解释、[]纠错、()直接局部补代码、
单独?解释与写法/句后?重点讲该句、!缺思路需拆解、->顺序推进。
已记录根AGENT.MD，借用Shell结构但以本次聊天定义为准，不改变Shell项目规范。
仅交流外层标记生效，代码内部符号仍按代码处理；BATCH05仍按原范围推进。

## 当前状态：BATCH22功能与问答完成，第四章本轮收尾，回TinyOS

2026-10-25作者回答最后三题，原答/审查/修正版已存入B22第7节。
明确普通ecall跳过指令并返回a0，timer保留继续执行所需的sepc/a0；中断位不表示主动触发，异常也不必然退出。
deadline重新武装避免旧到期事件重复触发，不保证严格相等的用户CPU时间片；U-mode忽略sstatus.SIE，
S timer仍需相应委派/pending/STIE。结束先停源，再恢复CSR/Bare并同步，最后释放借用映射与页面。
修正版包含补充，不把补充计作原答独立覆盖。本轮第四章按既定范围收尾，不再准备第五章或追加rCore测试矩阵。
TinyOS主线恢复，rCore保留源码、批次与总结供按需回看；下一任务由作者完整恢复TinyOS受限ELF装载和一条学习测试。
今日只读定位TinyOS现有草稿与测试入口，没有改功能或重新运行；详细任务见根docs_from_riscv/USER_ELF_LOADING.md。
章节总结补齐到B22，功能通过依据仍是已记录的作者验收，本次整理不重跑QEMU。
以下保留本批功能验收与交付历史。

2026-10-25作者完成A/B/C并修正匹配模式、Option转换与Exit返回后回复“好了”。
主审及独立只读复核无必修问题；A精确分类，B先重新武装后调度，C普通返回/退出路径正确。
实际工作区一次有界运行退出0，全部既定功能P0通过：观察5次timer/5次timer恢复、4次U ecall/2次普通恢复、
两个write/exit0、双向timer与最后1→1、私有进展与完整现场保持、cause5拒绝，
35页/8CSR/Bare/堆恢复与原B14—21/旧Bare双应用正常。timer次数不固定为5。
保留一条TaskStatus::Exited未使用导入警告，属于可清理项，不阻塞功能验收。
证据`.cache/batch22-author-review-l9k_umtl/`，54项源码配置运行前后哈希一致，未代改作者实现。
当前待三个理解题；随后整理第四章收尾并返回TinyOS，不再准备rCore第五章功能或新增作业矩阵。
以下保留骨架交付与之前批次历史。

B22完整目标与三处核心说明已写入BATCH22.md：精确分类scause、timer先重新武装再Ready调度、
ecall复用B20分发/返回并处理退出。复用B21拥有者与管理器，分页ASM不变，仍35页。
固定P0限定无yield真实双向抢占与最后自抢占、一次原始cause5拒绝、用户现场保留和资源/CSR恢复。
样例停止标志仅用于确定性观察所需timer事件，不是新系统调用或生产调度策略；不依赖用户读time。
timer作用域退出先清STIE/撤定时事件，再恢复CSR/Bare，随后旧Bare timer重新初始化。
骨架已交付，作者三处核心保留，尚未验收作者实现。
临时预检`.cache/batch22-preflight-hlnngywu/`仅副本补齐A/B/C并移除守卫，一次有界运行无警告退出0。
观察5次timer/5次timer恢复、4次U ecall/2次普通恢复、两个write及exit0，双向timer/最后自抢占、
私有计数/完整现场保留、35页/8CSR/Bare/堆恢复与原B14—21/旧Bare双应用均通过。
timer次数按事件条件及有限边界验证，不固定要求恰为5。作者源码配置前后哈希一致。
独立只读复核无必修问题；B19/B20/B21作者核心和原演示、ASM、旧timer/SBI/调度器与用户运行库均保持。
只扩展启动作用域内的timer与共享校验/拥有者入口，不迁移整个旧调度器，不增加分页故障矩阵或第五章功能。
实际工作区构建仅因预期B22守卫失败，无其他错误/警告；三TODO没有在作者工作区代填。

2026-10-25作者回答三个理解题，原答/审查/修正版已存入B21第7节。
第一题需区分“两个任务对应区域VA相同”和“同任务代码/数据/栈同址”；实际隔离靠独立根表映射不同PA，
切换包括satp与完整用户寄存器/状态/sepc，sp仅是其中x2。第二题已理解yield与exit区别，补正自调度理由为当前仍Ready，
与尚不存在的未来新进程无关。第三题保留数据/现场供恢复的理由正确，补正同内核循环成立的原因是每次trap已返回，
没有分别挂起的内核调用链；TaskContext与完整用户Trap现场职责不同。
按作者先前“问完题到下一步”的安排，准备第四章最后功能B22：独立页表下的timer抢占。
只完成分页多任务必要闭环，结束后回TinyOS，不进入第五章或添加扩展习题。
批前真实快照`.cache/batch21-before-paged-preempt-wicyhil0/`保存52个原有源码配置，尚未交付B22核心。

2026-10-25作者已将B扫描范围修正为1..=TASK_COUNT。
实际工作区一次timeout 8s make run无警告、主动退出0，全部既定P0通过：
同VA不同PA、五条round各一次、unknown返回-1后继续、最后yield1→1、两个exit0，
13次陷入/11次普通恢复、35页与8项CSR/Bare/堆恢复及原分页/旧Bare双应用正常。
证据`.cache/batch21-final-review-tbqbqpyb/`，运行前后52项源码配置哈希一致，未代改核心或扩展测试。
本批功能完成，按作者请求先进行三个理解题，答完再推进下一步；第四章收尾后回TinyOS的安排不变。
以下保留修正前审查和交付历史。

2026-10-25作者完成A/B/C并移除守卫后请求审查，主审与独立只读复核均确认只有B的一处功能问题：
find_next_ready使用1..TASK_COUNT，漏掉当前任务作为最后候选；A的构造/资源顺序及C的状态转换正确。
实际源码无警告构建，运行到task0退出、task1最后一次yield时，因只扫描Exited的task0而返回None，
触发`a yielding task must remain runnable`，QEMU错误255、make退出2，尚未通过本批功能验收。
临时副本仅将该范围改成1..=TASK_COUNT，一次有界运行无警告退出0：
五条round各一次、unknown返回-1后继续、yield1→1、两exit0、13次陷入/11次普通恢复、
35页与8项CSR/Bare/堆恢复、原分页演示和旧Bare双应用均通过。
证据`.cache/batch21-author-review-y7r2812u/`，作者源码配置前后哈希一致；交付时52项源码中仅paged_task.rs被作者修改。
未代改核心、扩展测试矩阵、准备下一批或恢复TinyOS；待作者修正这一行后复验。

2026-10-25作者确定本轮第四章结束后先回TinyOS，rCore作为遇到具体理解缺口时的补充，
不自动进入第五章或推进整套教程。回到TinyOS后，完整功能与有学习价值的测试由作者主导，
协作者帮助明确问题/接口/验收、解释和审查并承担例行复验，避免默认只留几个框架TODO。
当前B21仍待作者完成，分页timer抢占尚未接入；第四章按必要功能闭环收尾，不扩展习题。
本次只更新学习安排，未提前恢复TinyOS、改功能、补B21核心或运行测试。

2026-10-25作者请求继续。下一步组合第四章的独立用户空间与任务选择：
同一个task_demo ELF装载两次，用户VA相同、数据PA及现场页不同，复用B19/B20汇编和公共内核返回循环。
本批只做yield协作调度，原第三章Bare的TaskContext/__switch与timer调度保留在演示之后。
三处作者核心为任务构造、Ready环形轮询和Yield/Exit状态转换，框架提供受限现场映射与驱动。
既定P0为双任务交替与私有数据保留、只剩自己时yield、一次未知调用拒绝、13次ecall/11次普通恢复、
35页与8项CSR/Bare/堆完整恢复，旧演示在同次启动回归；不扩展测试矩阵。
批前真实快照`.cache/batch20-before-paged-tasks-nk4ynfd7/`保存50个原有源码配置。
临时预检`.cache/batch21-preflight-lfyqr10h/`只在副本补齐A/B/C并移除guard，一次有界运行无警告/退出0。
五条round消息各一次、跨yield私有计数保留、unknown返回-1后继续、yield1→1、两个exit0，
13陷入/11普通恢复、35页逐轮恒定、8项CSR/Bare/帧/堆恢复及B14—20/旧双应用均通过。
实际工作区build只有预期compile_error，无其他错误/警告；核心留给作者，不将副本成功计作作者完成。
独立只读复核无必修问题，核对了构造错误路径、字段释放顺序、两现场槽映射和页内容引用边界。
50个原文件只有五处预期接线修改；分页ASM、B19初始化和旧两演示函数体、B20作者核心、
旧调度器/用户运行库与paged.ld均保持原样。原CSR块只抽为等价私有助手供新旧驱动共用。
完整目标、三处核心、固定P0与三题见BATCH21.md，不追加测试矩阵；未改C主线、clean或提交。

## 已完成：BATCH20 分页系统调用与用户恢复，功能与问答已完成

2026-10-25按作者请求将三题原答、审查与修正版存入B20第8节。
明确用户VA须按用户空间定位、当前MMU仍用内核表；V/U/R叶权限与数据拥有者的归属/生命周期分别检查。
失败write也已处理请求，推进sepc并写x[10]后恢复寄存器继续执行，不重载ELF。
23页在整个循环中保留复用，不是每次回收重建；页内引用需在用户/汇编可能写入前结束，
Copy快照与保活所有者不等于页内引用。exit后才结束活动作用域、恢复Bare/CSR并同步，最后回收。
补充讲解不计作原答独立覆盖。本次只更新学习文档与状态，不改代码、不重复运行或准备B21。

2026-10-25作者已修正B并清理未用导入，独立只读复核A/B/C无必修问题。
实际作者工作区一次timeout 8s make run无警告/QEMU主动退出0，全部既定P0通过：
RO23+栈22正常输出各一次，用户返回23/-1/22、坏write原始输出字节数0、4U ecall/3恢复，
23页复用与完整归还、堆基线及8项CSR/Bare恢复，原B14—19与双应用各三轮/双向timer/各exit0/allExited正常。
证据`.cache/batch20-final-review-c1firoxy/`，运行前后作者源码配置哈希一致。未代改核心、扩展矩阵、运行C主线或准备B21。
本批功能通过，B20第8节三题随后已集中复核；以下保留修正前审查与原交付历史。

2026-10-25作者已填写三处并移除guard，集中只读复核确认A完整预检后复制、C检查加法后修改现场均正确。
B未按id分发，把write检查套到所有调用；成功Return值被分号丢弃，最后一律Exit(93)，且把调用号当退出值。
实际作者运行编译有三项未用导入和id未使用警告，第一条rodata输出后在traps==4断言失败（实际1），make退出2/QEMU错误255。
临时副本只修B为64/93/未知分支并去掉三项未用导入，A/C保留；一次有界运行无警告退出0，
23/-1/22三返回、4陷入3恢复、坏write字节数0、23帧/堆/CSR恢复及旧双应用完整正常。
证据`.cache/batch20-author-review-bwo2ltz9/`，原作者源码配置哈希保持，独立只读复核结论一致。
本批仍待作者修正B，临时副本成功不计作作者验收；没有代改核心或增加测试场景。下文保留原交付记录。

2026-10-24作者在章节复习后请求继续。本批复用B19全部汇编，增加单个分页ELF多次ecall/恢复：
write只读段23字节→跨guard拒绝-1且零输出→write用户栈22字节→exit0，共4次U ecall、3次恢复。
作者只补新paged_syscall.rs的完整用户缓冲区校验/复制、write/exit分发、sepc/a0修正三处。
原B19初始化及单退出演示保留，ActiveRun仍私有，新增驱动和上下文Copy快照读写不暴露通用切表接口。
独立syscall_demo ELF沿用原paged.ld与6数据页，仍11+5表页/6数据/1现场=23页；原用户程序与ASM不改。
FrameTracker仅将注释前提扩展为Bare或经核验的内核页池恒等映射、用户暂停且无并发引用。

批前真实快照`.cache/batch19-before-syscall-f654g7w3/`保存48个原有源码配置。
临时`.cache/batch20-preflight-l4948_mj/`只副本补齐三TODO并移除守卫，一次timeout 8s make run无警告、退出0。
45字节正常输出、返回值23/-1/22、合法QZ前缀存在但坏write输出字节数为0、目标哨兵不变、
4陷入3恢复、逐轮内核根/SPP/栈/指令和页数/堆不增长、最终8CSR/Bare/23帧与堆恢复均通过。
同次启动B14—19及旧双应用各三轮/双向timer/各exit0/allExited正常，工作区源码配置哈希未变。
独立只读复核无必修问题；原ASM/B19核心/旧用户文件与批前相同，实际guard构建只有预期错误且无警告。
最终交付哈希与既定五个旧文件变更范围、B19核心逐字保持、FrameTracker仅注释变更、48条本地文档链接均核对通过；
记录`.cache/batch20-delivery/checks.json`，只有三个作者TODO，未在工作区填入参考实现。
临时副本成功不计作作者完成；完整路径、既定P0和三题一次给齐于BATCH20.md，不追加测试矩阵或推进B21。
本轮未运行或修改TinyOS C主线、未clean/提交。

## 已完成：章节复习整理，BATCH19功能与问答归档

2026-10-24作者要求先保存B19答案，暂不准备下一步，并将当前关键机制及之前各章整合为章节总结。
已将三题原答、审查和修正版存入B19第8节：明确trap不换satp/sp、U0跳板/现场的访问范围、
当前用户sp与128字节内核续点帧、最后恢复用户sp的寻址原因、只多1现场页、exit不恢复、
Bare立即生效但回收仍需地址翻译同步；修正版补充不计作原答独立覆盖。

沿用既有目录docs_from_riscv（作者口述docs-from-risvc指此资料目录），新增rcore/README索引、
第一至第四章各一份整体总结，以及Bare/Sv39/Trap与页表生命周期专题；原BATCH01—19与TinyOS资料保留。
总结串联目标、模块、完整路径、关键状态/所有权、易错点和已验证边界，明确第二章批处理循环略过、
第三章get_time未实现、第四章单ELF分页闭环已通过而旧双应用仍Bare，避免宣称完整分页调度已经完成。
TinyOS_EVOLUTION仅同步rCore学习进度，不改变原TinyOS草稿/验证日期或启动C++迁移。
本次只整理文档、检查本地链接/格式/功能源码哈希；不重复构建或QEMU、不改代码、不准备B20。
整理后检查134个本地链接全部存在、代码围栏成对且无尾随空白；114个功能/构建配置文件哈希保持。
第一/二章与Bare专题另经只读交叉复核，第三/四章由主协作者对照批次与源码审阅；
核对结果在`.cache/chapter-summary-153dwp9e/results.json`，本轮未更改历史验收结论。

## 已完成 BATCH19

2026-10-24作者已修正A的位运算和C的字段加载，独立只读复核无必修问题。
实际作者工作区一次timeout 8s make run无警告/QEMU主动退出0，全部既定P0通过：
23页及RW/U0现场、唯一未映射入口拒绝、真实U执行/ecall93/code0/SPP0、token/栈/指令检查、
8项CSR/Bare恢复、free3008/in_use0/堆基线恢复；原B14…B18及双应用各三轮/双向timer/各exit0/allExited正常。
证据`.cache/batch19-author-review-r7e0xtq3/`，运行前后os/user源码配置哈希一致，未代改核心或扩展矩阵。
本批功能通过，第8节三题随后已集中复核，不准备B20。以下保留此前诊断与交付历史。

2026-10-24作者已填A/B/C并移除guard，运行在enter U-mode之后得到instruction page fault，sepc/stval均0。
只读集中审查发现两组必修：A仍为status | !mask；C把288的trap_handler读入t1当sp，并漏读t2，jr t2因用户初始x7=0跳到0。
B的指令顺序正确，无操作数sfence.vma合法；nop及BEGIN/END位置仅整理问题，不计新功能要求。
临时`.cache/batch19-author-diagnosis-0etpb216/`只改A为status & !mask、C为t1=280/t2=288，
一次timeout 8s make run无警告/退出0，B19退出与资源恢复标记和旧allExited均通过；两份作者核心文件哈希不变。
独立只读复核结论一致。作者工作区尚未修正，不将副本通过计作本批作者验收；未代改核心或扩展矩阵。

2026-10-22按作者请求保存B18答案后继续。本批新增私有PagedTrapContext和单ELF启动演示：
内核表→高跳板→用户表/sret→真实U-mode→exit ecall→高trap保存→内核表/启动续点→Bare/回收。
旧272字节TrapContext、调度器、syscall及用户源码不改；单个应用首次trap即结束演示，不做普通syscall恢复或timer调度。
PagedTrapContext为37字/296字节，专有现场页位于跳板下方；两根同页RWAD/U0，沿用高L0，
总计11内核表+5用户表+6用户数据+1现场=23页。启动续点在原启动栈保留128字节ra/gp/tp/s0…s11。
作者三处核心为initial、恢复用户root/CSR、保存后恢复内核root/sp；其余寄存器保存及安全接线已提供。

临时`.cache/batch19-preflight-z1ndj85c/`仅副本补三处并移除guard，无警告构建，
一次timeout 8s make run退出0：五条B19标记、唯一未映射入口拒绝、U ecall93/code0/SPP0、
用户token/栈/指令检查、八CSR/Bare恢复、free3008/in_use0/堆恢复均通过。
B14…B18及旧双应用各三轮/双向timer/各exit0/allExited正常，预检前后工作区源码配置哈希一致。
设计经过独立只读复核，实际骨架/补全副本由主协作者静态核对；工作区构建只因预期guard失败，无其他错误/警告。
旧文件对照`.cache/batch18-before-paged-user-dn1grw6j/`由精确新增块反向重建，不是事前全量快照。
另对照B18最终实际保存的os/user哈希，只有mm/mod.rs和memory_set.rs两处必要接线变化；
工作区三处TODO补全后与通过的副本逐字一致，交付核对见`.cache/batch19-delivery/`。
本批仍待作者完成，不将临时副本成功计作作者验收；固定P0与三题见BATCH19.md，不追加矩阵。
本次未运行TinyOS C主线、clean或提交。

## 已完成 BATCH18

2026-10-22按作者请求将三题原答、审查及修正版存入B18第9节。
集中区分CPU特权级、satp选中的页表与PC/sp的地址数值；同VA共享跳板用于切表期间连续取指，
不是两个程序争用问题。satp不是访存地址寄存器；切表不自动切U。
共享代码源自静态内核镜像，两根各新增L1/L0才是额外4表页的来源；
高PC先回内核表、再回低PC/Bare并同步，最后销毁资源。补充讲解不计作原答独立覆盖。
作者现已请求继续B19，下面原交付和验收记录保留供追溯。

2026-10-22作者已将C2的SFENCE移到csrw satp,a0之后，两次写satp后均完成全量同步。
独立只读复核无必修问题；修正后一次timeout 8s make run无警告/QEMU退出0，全部既定P0通过：
同VA同PA/RX/U0、内核11表+用户5表/6数据共22页、唯一越页入口拒绝且不变、
S-mode实际kernel→user→kernel往返与satp观测、恢复Bare和全部CSR、帧/堆基线均正确。
原B14/B15/B16/B17及双应用各三轮/双向timer/各exit0/allExited正常。
证据 `.cache/batch18-final-review-soz8pt61/`，运行前后源码配置哈希一致；未代改核心、扩展矩阵或运行TinyOS C主线。
本批功能通过，三题随后已按下述记录集中复核。

以下为修正前审查与原交付记录。

2026-10-22作者已填写三处并移除guard；当前A/B正确，C2的SFENCE误放在csrr a2,satp之后、写回内核satp之前。
实际一次make run无警告/退出0，既定运行标记及双应用各三轮/双向timer/各exit0/allExited均通过，源码哈希不变；
但当时静态指令顺序不满足切回内核表后的同步要求，故未判通过；该位置问题现已按上段修正。
证据 `.cache/batch18-author-review-n5wkiszy/`，results区分runtime通过与最终静态未通过；未代改核心或扩展矩阵。

2026-10-22作者请求准备下一批。B18只推进公共trampoline和S-mode的跨页表往返，
不进入新ELF的U-mode、不扩展旧TrapContext或接调度器地址空间切换。
两表最高页映射同固定内核代码PA，RXA/U0，不新增数据owner；各新增2表页。
作者核心为MemorySet::map_trampoline、virtual_address和probe中两组satp/SFENCE指令。
新增trampoline.rs/S，linker预留独立整页并断言大小，mm::init在B17后接演示。
原Bare应用/loader/task/用户代码及依赖保留，B15低fatal作为失败诊断基础设施复用。
本批讲义一次给齐执行路径、别名换算、无栈约束、CSR过渡、既定P0和原三题。
批前快照 `.cache/batch17-before-trampoline-7ieb3irs/`。
临时 `.cache/batch18-preflight-lngxr7sm/` 仅副本补齐A/B/C并移除guard，无警告构建、QEMU主动退出0：
最高VA同PA/RXA/U0、内核11表/用户5表+6数据共22页、越页入口Err且不变、
真实kernel→user→kernel的S-mode往返/用户阶段satp观测、Bare与全CSR/帧/堆恢复均通过。
旧B14/B15/B16/B17和双应用各三轮/双向timer/各exit0/allExited正常，作者源码配置前后哈希一致。
独立只读复核无功能/教学必修问题；当前作者构建只因预期B18 guard失败，无额外错误或警告。
讲义明确ASID0切根后同步前可有旧翻译，不声称下一取指必重新走新根；
高fatal先恢复内核表再经页内绝对地址跳低B15入口，绝不在高PC直接Bare。
不把预检当作作者完成，不扩展故障矩阵，TinyOS C主线本次未修改或运行。

## 已完成 BATCH17

2026-10-22按作者请求将三题原答、审查和修正版存入B17第9节。
字段含义基本正确，补齐BSS由大小记录和零填充实现；明确ELF/PTE权限位位置不同、
区域向外取整不改变原始复制VA，否则内容错位。纠正guard用于程序段与用户栈布局、并非“用户和程序”隔离，
一页guard不保证捕获所有越界，ELF初值复制到物理数据页而非内核栈。
新表已有用户映射但未激活，直接启用缺少内核取指/栈/trap与切换安排；不把补充讲解计作原答已独立覆盖。
本次仅更新文档，不修改代码、不重复验证、不准备下一批。

2026-10-21作者完成segment_permissions/plan_layout/from_elf并移除guard。
作者提供五条ELF成功标记、all applications exited及退出码0：3LOAD、6数据+3表页，
初值/BSS零填充/权限/guard与栈、截断头Err且不变、帧和堆恢复/Bare均通过。
协作者只读核对当前源码并补一次make build确认无警告；不重复已有成功QEMU，运行证据来自作者。
相对交付骨架，os/user仅user_image.rs实现和elf_demo.rs移除guard，其余基础设施原字节保持。
证据 `.cache/batch17-author-review-gj613wx2/`，保留作者输出、构建日志和源码哈希。
本批功能通过，沿用第9节原三题收尾；不扩展矩阵或自动准备下一批。

以下为原交付与预检记录。

2026-10-20按作者请求先将B16原答、集中审查及修正版补入第9节，再准备B17整批文档和骨架。
本批将ELF的PT_LOAD转换为Framed区域，复制文件初值并保留零填充，添加guard与两页用户栈，
返回MemorySet/entry/栈界限；只在Bare构造查询，不激活新空间，不接trampoline/任务或更换旧应用。
新增受限安全ELF解析helper，不引入依赖；原有MemorySet/PageTable/FrameTracker/paging/loader/task核心不改。
作者三处为user_image.rs的segment_permissions/plan_layout/from_elf；elf_demo.rs保留编译guard。
独立image_demo ELF与paged.ld通过user/target/paged构建，旧hello/peer的源码及原链接布局保留。
旧ELF的text/rodata共享页，而本批不做权限合并，故另用三段页对齐示例；此限制不表示旧ELF非法。
本机readelf：entry40000000，LOAD=3，RX文件/内存116/116，R17/17，RW8/5016。
用户映射共6数据页+3表页；guard40004000，stack[40005000,40007000)。

批前快照 `.cache/batch16-before-elf-5l55xaab/`。
临时 `.cache/batch17-preflight-jwhfd3ab/` 仅副本填三TODO并移除guard，无警告构建/QEMU退出0：
所有段初值、BSS零填充、精确权限/入口/guard/全零用户栈、9页生命周期及帧/堆恢复通过；
唯一拒绝为63字节截断ELF头，Err且原内容/资源不变；satp仍Bare。
原B14/B15/B16及双应用各三轮/双向timer/各exit0/allExited正常，预检前后作者源码配置哈希一致。
不把副本通过当成作者实现验收；不追加矩阵，TinyOS C主线本轮未修改或运行。
独立只读复核未发现功能/教学必修问题。作者实际make build只因预期B17 guard失败，无额外错误或警告。
最终骨架填入预检三处实现后与通过版本逐字一致；批前38项已有源码配置仅Makefile/mm接线/用户manifest与build跟踪预期变化，
其余旧代码原字节保留。交付核对见 `.cache/batch17-delivery/delivery-checks.json`。

## 已完成 BATCH16

2026-10-20三题已集中复核，并按作者请求将原答/审查/修正版写入B16第9节。
纠正Framed是分配与所有权而非保证VA!=PA，U是页权限而非CPU模式；MapArea是数值描述而非借用；
活动页表不能仅靠释放阻止硬件访问，释放不等于清零；复制按3+5重新找owner，guard由软件完整预检拒绝，
防止合法前缀被部分修改。不把补充讲解计作作者原答已独立覆盖。

2026-10-20作者完成new_framed/map_framed/copy_data三处核心并移除guard。
独立只读审查未发现功能必修问题；实际timeout 8s make run无警告构建、QEMU主动退出0。
既定P0全部通过：两套同VA不同PA/内容，各3表页+3数据页，共12页；偏移4093的8字节跨页复制正确，
权限/初始清零/偏移保持，越过guard整段Err且合法前缀未写、已有内容/页数/堆不变。
销毁第一套剩6页且第二套内容保持，全部销毁后free3008/in_use0、堆基线恢复，satp仍Bare。
原B14/B15演示、两应用各三轮/双向timer/各exit0/allExited均正常。
证据 `.cache/batch16-author-review-37qc4oe2/`，37项os/user源码配置前后哈希一致。
未代改作者核心、扩展测试矩阵或运行TinyOS C主线。功能通过；沿用B16第9节三题收尾，不追加任务。

以下为本批交付与预检记录。

2026-10-20按作者请求先将B15三题原答/审查/修正版补入第9节，再准备B16整批文档与骨架。
MapType增加Identical/Framed，MapArea保留Copy描述；MemorySet新增Vec<FramedPage{vpn,FrameTracker}>拥有数据页。
new_framed/map_framed/copy_data三处留TODO，framed_demo.rs保留compile_error，mm::init在B15恢复Bare后接演示。
提供总Framed owner元数据容量预留（checked_add/try_reserve）、映射分发、完整拷贝范围/owner预检、读回和计数。
旧Identity循环原样保留为map_identical，构造器仅接map_type字段；MemorySet整体构造接新owner容器，
字段与局部变量顺序保证正常/错误销毁先表页、后数据页。PageTable/FrameTracker/paging/loader/task/用户代码和依赖不改。
copy_data只在一个Framed区内进行Bare初始化，允许偏移跨页、零长度成功；不直接解引用VA，不要求目标叶带W。
两套仅含用户区域的表暂不激活，也不执行模拟字节，不接ELF/trampoline或更换旧用户栈。

批前快照 `.cache/batch15-before-framed-4ospu_pn/`。
临时 `.cache/batch16-preflight-f3wrnxlw/` 只副本补齐三TODO，无警告构建、QEMU退出0：
两space同VA不同PA/内容，每套两页RXU代码+一页RWU栈、guard未映射，各3表+3数据，共12页；
从偏移4093跨页复制8字节，owner读回/边界零值/翻译偏移/权限通过；
唯一拒绝为复制跨guard，返回Err且合法前缀也未写、已有内容/页数/堆不变；
销毁第一套还剩6页且第二套内容保持，全部销毁后free3008/in_use0、堆基线恢复，satp仍Bare。
原B14/B15及双应用三轮、双向timer、各exit0/allExited正常；预检前后作者os/user源码配置哈希一致。
基础设施独立只读复核无必修问题；只按既定P0，不追加矩阵，不把副本通过算作作者实现已完成。
最终讲义复核后将B15答案明确为写satp改变MODE立即生效，SFENCE负责同步，不能当作启用分页的开关。
其余讲义无必修问题；作者实际make build仅因B16 guard失败、无警告，文本/链接检查通过。
与批前36项源码配置比对，已有文件仅memory_set/mm接线预期变化；Identity循环函数体逐字保留，
PageTable/paging/FrameTracker/loader/task/main/user原字节不变，副本仅补三TODO并移除guard。

## 已完成 BATCH15

2026-10-20三题已集中复核并将原答/审查/修正版补入B15第9节。
修正map可在Bare下构造PTE、计算token不启用分页；区分TLB加速与SFENCE失效/同步，
解释先离开旧satp再刷新后释放，PC/sp跨模式依赖恒等映射，旧U-trap栈交换不适用于启动S fault，
以及旧应用缺U映射所以临时试验后恢复Bare。不将讲解补齐内容记作作者已独立答对。

2026-10-20作者恢复学习，实际两处核心已填写并移除guard。独立只读审查无功能必修问题。
实际timeout 8s make run退出0，既定P0通过：完整satp token/MODE8/ASID0、真实取指/volatile栈与堆/rodata读取，
活动期重复进入Err且satp/stvec/资源不变；守卫恢复Bare/SFENCE/stvec，sscratch与中断状态保持，
最后表页与堆恢复基线；原B14检查、两应用各三轮/双向timer/各exit0/allExited均正常。
首轮仅有main.rs和paging.rs两条未使用导入警告，已提前说明后删除这两行，核心不改；
随后make build无警告退出0，仅导入清理不重复QEMU。功能运行期间作者源码哈希不变，
最终源码对比也确认只有这两条导入删除，没有其他修改。
证据 `.cache/batch15-author-review-vx6sj7a4/`，保留初始运行/清理前结果/最终构建及哈希。
本批功能通过，收尾沿用B15第9节三题，不追加任务或矩阵，TinyOS C主线本次未运行或修改。

2026-10-19按作者请求先将B14三题原答、审查和修正版写入第9节，再准备BATCH15整批讲义与骨架。
新增mm/paging.rs和paging_trap.S；PageTable::token、write_satp_and_flush留TODO，paging.rs保留guard。
提供MemorySet的token转发及完整内核映射预检查、CSR读取、私有ActiveKernel借用守卫与正常/拒绝演示，
mm::init在B14后接线。原作者PageTable/MemorySet核心、trap/task/main/应用和依赖不改。
本批只在启动封闭作用域临时开Sv39：Bare/SIE0/sie0→核对完整映射→临时S-fatal stvec→satp/SFENCE→
真实取指/栈/堆/rodata访问→守卫恢复Bare/SFENCE/stvec→释放表页→原Bare双应用。
不直接持久开启：当前用户槽未映射、用户栈U0；用户地址空间与切换方案随后推进。
当前U-trap不能处理S fault，提供不碰旧sp/sscratch、先回Bare再建8KiB应急栈的fatal-only诊断入口；
仅作为接线保护，仍依赖入口可取指，不宣称通用S异常恢复。

批前快照 `.cache/batch14-before-activation-fi607cym/`。
临时 `.cache/batch15-preflight-d29qonf8/` 仅副本补齐两处核心，无警告构建、QEMU退出0：
完整satp token/MODE8/ASID0、分页下真实取指和volatile栈/堆/只读字节访问通过；
唯一拒绝为活动期再次enter返回Err，satp/stvec/页数/堆不变；随后恢复Bare/stvec/sscratch和中断状态，
最终堆与帧统计回到基线，原B14演示及双应用三轮/双向timer/各exit0/allExited正常。
作者os/user源码配置在预检前后哈希一致。基础设施独立只读复核无必修问题；
只按既定P0，不增加真实权限故障矩阵，不把副本通过算作作者实现验收。
讲义及B14答案独立复核无必修问题；当前实际make build仅因B15 guard失败、无警告。
文本/链接检查通过，与批前快照核对已有os/user仅两类转发/检查辅助和mm接线发生预期变化，
去掉新增方法后PageTable/MemorySet与原文件逐字相同；副本仅补两TODO并移除guard。

## 已完成 BATCH14

2026-10-19三题已集中复核并保留原答/修正版：明确恒等映射仍受S/U翻译与权限约束，
3008映射页不等于3008分配页；区分text/rodata/data/BSS及本项目堆栈布局，解释sdata边界；
?提前返回后由局部Drop清理，活动页表问题取决于硬件引用而非是否报错，释放前必须先安全切换/刷新。
不将补充讲解记作作者已独立答对。

2026-10-19作者完成四处核心并移除guard，独立只读审查无必修问题。
实际timeout 8s make run无警告构建、主动关机退出0，完成既定P0：
四区所有页首/末字节恒等映射、精确PTE权限、bootstack/当前sp/heap/根表页RW覆盖；
仅新增9表帧、未取得被映射数据页所有权；保留间隙/应用起点/pool_end未映射。
区域重叠在分配前返回Err且页数/堆不变；销毁后free3008/in_use0、堆回到基线，satp0/SIE0前后保持。
原双应用各三轮、双向timer抢占、各exit0/allExited正常。
证据 `.cache/batch14-author-review-sypusfsa/`，作者os/user源码配置前后哈希一致。
未代改核心实现、追加矩阵或运行TinyOS C主线；本批功能通过，区域映射路径与第9节原三题现已集中复核。

2026-10-19作者恢复学习并要求开始下一批。BATCH14.md一次给齐四区映射规划、模块/资源关系、
四处TODO、构造失败语义、既定P0、命令和原三题。当前只构造/查询，不激活satp。
新增mm/memory_set.rs，MapArea::new/map、MemorySet::new/new_kernel留给作者，compile_error保留。
提供KernelLayout/边界读取、查询包装和演示；MemorySet整组构造先检查overlap，局部PageTable失败自动Drop。
只支持低VA、严格页对齐半开区间、恒等映射，不引入Framed/单值MapType/通用ceil/unmap。
四区为textRX、rodataR、[sdata,ekernel)RW和pool_boundsRW，均U0；合并可写区包含bootstack/heap/静态任务栈。
补链接无点stext/etext/srodata与erodata/sdata，保留原sbss清零边界；mm::init接新演示。
PageTable仅新增leaf_flags只读检查与开放同级权限校验，作者四处原函数体不改，无Cargo依赖变化。

批前快照 `.cache/batch13-before-memory-set-bfdsxunz/`。
临时 `.cache/batch14-preflight-o57bimxb/` 只副本补齐四处核心，离线无警告构建、QEMU退出0：
四区所有页首/区间末字节恒等查询、精确PTE flags、bootstack/当前sp/堆/根表页RW覆盖通过。
当前布局仅占9表帧（root+L1+7个L0），未额外分配数据页；内核后间隙/应用起点/pool_end无映射。
重叠区域Err发生在分配前，页数/堆不变；销毁后free3008/in_use0、堆基线恢复，satp0/SIE0前后保持。
原两应用各三轮、双向timer抢占、各exit0/allExited正常。P0按既定范围，不追加矩阵。
早期预检发现相邻链接边界别名被Rust按不同extern static对象折叠为不同地址，assert输出相等却失败；
kernel_layout对边界数值增加black_box后复验通过，修正前日志保留。该接线细节不交给作者处理。
基础设施独立只读审查无必修问题；当前作者仍有四TODO和guard，不能把副本通过记为作者功能验收。
最终讲义独立复核无必修问题，作者make build仅因B14 guard失败、无警告；文本/链接检查通过。
与批前快照核对，已有os/user只有linker/mm接线/PageTable新增只读查询发生预期变化；
去掉新增查询和权限检查可见性后PageTable与原文件逐字相同，副本相对当前骨架仅补四TODO并移除guard。

## 已完成 BATCH13

2026-10-19按作者要求将三题原答、审查和修正版写入BATCH13第9节。
集中补充PPN<<10是PTE字段布局、PPN<<12是物理页首地址，flags与RSW的区别；
明确[1,0,1]/[1,0,2]共用三张表、末级数据页PPN加offset得到PA；
修正“不能单独释放数据页”，说明PageTable只拥有表页、数据页归各自Tracker，以及提前归还造成旧映射指向复用页。
查询有效映射不等于硬件启用，satp仍0/Bare。不将补充讲解记作作者已独立答对。
今天停止推进；下次从第四章内核地址空间映射规划与构造继续，先明确区域/权限再安排启用Sv39。
本次只更新文档，沿用已通过P0，不重复构建/QEMU，不提前创建下一批实现；TinyOS C主线继续暂停。

2026-10-19作者修正translate_va局部变量并移除guard。实际make build无警告退出0，
四处核心只读复核未发现功能问题。作者核心保持原样的临时副本完成既定P0，QEMU主动关机退出0：
固定PTE编码、3表帧+2数据帧、两地址偏移查询、缺失None、重复映射Err且原映射/页数不变全部通过；
drop表与数据拥有者后free3008/in_use0，堆恢复演示前基线，satp0/SIE0。
原两应用各三轮、双向timer抢占、各exit0及allExited正常。
证据 `.cache/batch13-author-p0-a9sm_av0/`；仅副本mm::init增加观察，作者os/user源码配置前后哈希一致。
本批功能通过，不追加测试矩阵；整条页表路径和第9节原三题现已集中复核。

2026-10-19作者请求检查。PTE编码/PPN解码/map静态逻辑正确；translate_va遍历/叶项检查/偏移合成
逻辑方向正确，但声明index/root后实际使用indexes/table，需统一为let indexes和let mut table。
初次构建被顶部compile_error阻止；检查期间作者已移除guard，再次make build因上述变量名产生5处E0425，
退出2，未进入QEMU验收。独立只读复核确认没有其他必修功能问题。
本轮不代改作者源码；先集中列明编译阻塞，作者修正后再执行既定P0，不扩展测试矩阵。

2026-10-18按作者请求将BATCH12三题原答、审查与修正版补入第11节，随后交付BATCH13。
BATCH13.md一次给齐PTE布局、三级路径、表页/数据页所有权、四处TODO、错误处理、既定P0与三题。
新增os/src/mm/page_table.rs，保留compile_error：PTE编码、PPN解码、map、translate_va由作者实现。
已提供拥有者校验后的表项读写、中间表分配/失败清理、完整演示；mm::init接线已完成。
FrameTracker仅新增bytes(&self)只读借用，原alloc/dealloc/Drop不改；不新增依赖或修改应用/调度。
只处理4KiB叶项，satp仍Bare；本批查询只返回PA数值，不让CPU使用新页表。

批前快照 `.cache/batch12-before-page-table-37fuik_r/`。
临时 `.cache/batch13-preflight-_fr6ydjr/` 仅副本补齐四处核心，无警告构建、QEMU退出0：
固定PTE编码、两映射共用3表页、两地址偏移查询、未映射None、重复映射Err且原映射/页数不变通过。
共用5页，drop(table)仅归还3页，再归还2数据页恢复free3008/in_use0；堆恢复演示前基线。
satp0/SIE0，原两应用三轮、timer双向抢占、分别exit0及allExited正常。
预检前后作者os/user源码哈希一致，不把临时完成副本视为作者功能验收。
基础设施独立只读复核无必修问题；P0由协作者承担，不追加故障矩阵或作者测试作业。
最终讲义与B12修正版独立复核无必修问题。作者make build按设计仅因B13 guard失败，无警告。
文本/链接检查通过；与批前快照核对，已有os/user仅mm接线与FrameTracker只读访问器发生变化，
移除该新增访问器后frame_allocator.rs与作者原文件逐字相同，四处TODO与guard保留。

## 已完成 BATCH12

2026-10-18三题已集中复核并将原答/审查/修正版写入BATCH12第11节。
修正ekernel已对齐但后方仍有应用/DTB保留区域、recycled为空闲页号而非在用页；
current是首次分配边界，全部归还无需回退；区分数字复制与拥有者移动，补充Drop重入锁死锁及锁外清零。
保留原答，不将集中补充的讲解记作作者已经独立答对。

2026-10-18作者完成alloc/dealloc/Drop并报告运行正确。独立只读审查无必修功能问题。
作者原实现的三页元数据场景host -D warnings构建运行均0，顺序/耗尽/优先复用通过；
重复归还Err且current/recycled/统计保持不变，后续分配正常。
实际timeout 8s make run无警告构建、主动关机退出0，页池3008页，80440/80441分配，
80440归还后优先复用且4096字节清零，全部释放free3008/in_use0；
原heap/address检查及两应用各三轮/双向timer/各exit0正常。
证据 `.cache/batch12-author-review-_8_s5n45/`，作者os/user源码及配置哈希前后一致。
不扩展矩阵或代改核心实现，功能通过，第11节原三题现已集中复核。

2026-10-18作者要求下一步，进入第四章物理页帧分配。BATCH12.md一次给齐范围/模块/生命周期、
三处TODO、锁与unsafe前提、观察输出、固定P0及原三题；不建立页表或改应用地址/调度。
新增mm/frame_allocator.rs：StackFrameAllocator.alloc/dealloc与FrameTracker::drop留TODO，保留compile_error。
其余已提供：pool_bounds、锁外预留Vec、一次初始化、锁外整页清零、私有所有者与借用绑定页访问、统计和演示。
mm::init在原heap/address后接frame，main仅将mm失败日志文字统一为memory initialization failed。

不能照抄官方[ekernel, RAM_END)：第三章两应用仍在固定窗口，当前APP_COUNT2/每槽0x20000，
窗口结束80440000。选择[max(页对齐ekernel, 应用窗口末尾),81000000)，当前3008页，
避开固件/内核/两app及87e00000附近DTB；这是固定virt128MiB的小池，不称完整RAM发现。
直接依赖spin精确0.7.2用于Mutex，原本已由buddy传递引入；只更新根包依赖列表，无包升级或新下载。
元数据recycled预留至少3008个usize，使用静态内核堆，归还时不扩容；该堆占用持续存在。

批前快照 `.cache/batch11-before-frame-8k3htbxk/` 保留B11源码与Makefile/来源锁。
临时 `.cache/batch12-preflight-0vdl0a0z/` 补齐三处，纯元数据host -D warnings正常分配/耗尽/复用，
重复归还Err且current/recycled/统计不变，继续分配通过。内核无警告离线构建，
QEMU页号80440/80441，归还后80440优先复用且4096字节清零，全部释放free3008/in_use0；
元数据堆占用演示前后不变、satp0/SIE0，原双应用三轮/双向timer/各exit0正常，最终QEMU0。
预检前后作者os/user源码及配置哈希一致；只在副本补核心，P0由协作者承担，不追加矩阵或作者测试代码。
作者实际make build按设计仅因BATCH12 guard失败，无警告；不能将临时预检标为作者已完成。
基础设施已经独立只读复核，无必修问题；官方整个ch4仍未构建验收。
独立讲义复核修正物理页帧管理章节链接，其他算法/范围/生命周期要求无必修问题。
文本/骨架检查通过，原有源码与批前快照比对仅Cargo依赖、mm接线和main错误日志发生预期变化；
address/heap/trap/task/user保持原字节，所有依赖包版本/校验未变，三处作者TODO和guard保留。

## 已完成 BATCH11

2026-10-18按作者请求将三题原答、审查及修正版写入BATCH11第9节。
前两题主线正确，明确VA/PA各自的floor/to_addr只做表示转换、偏移位数来自页大小、
三级索引数组按根到叶排列。第三题作者表示不清楚，已补充bit38与高25位一致的canonical规则、
高VA先mask再取27位VPN及页首符号扩展，不将讲解记作作者已独立答对。
本次只更新文档与进度，不改代码、追加题目或重复构建/QEMU。
文本与示例数值检查通过，独立只读复核无必修问题。

2026-10-18作者提交预期地址输出。实际源码已完成六处核心并接入mm::init，独立只读复核无必修问题。
canonical校验、27位VPN提取、页首符号扩展及根→叶三级索引正确，无CSR写入或页表访问。
实际timeout 8s make run无警告构建、主动关机退出0；低/高地址与非法输入拒绝断言通过，
堆释放统计归零，原双应用各三轮计算、双向timer抢占、各exit0一次及allExited正常。
只执行本批既定P0，不追加矩阵；作者os/user源码及配置哈希前后一致。
记录 `.cache/batch11-author-review-8tuicwgt/`；功能通过，原三题现已集中讲解，当前satp仍为Bare。

2026-10-17作者请求准备下一阶段文档。BATCH11.md一次给齐地址空间概念、VA/PA/VPN/PPN、
Sv39 canonical规则/27位VPN/三级9位索引、表示约定、六处TODO骨架与两处mm接线、
可观察输出、既定P0和三题。仅新增讲义并更新进度/入口，作者os/user源码不改。
本批不写satp、不建页表、不解引用物理页；ceil/范围端点留到明确语义后的后续任务。

固定官方ch4和书章节已独立核对，并核对RISC-V规范。与官方mask构造有意区分：
字段私有，new检查非法输入，VA存完整canonical usize，VPN只留低27位并在还原时符号扩展；
indexes统一按规范[VPN[2],VPN[1],VPN[0]]根到叶，避免书中不同编号方式混淆。

文档骨架提取至 `.cache/batch11-doc-preflight-7a2u32ms/` 临时补齐六处，
固定rustc1.98.1/edition2024/-D warnings宿主构建运行均0，低/高地址拆分与还原、构造范围检查、
非法Sv39地址拒绝断言通过，三条演示输出符合文档；未将临时实现复制到作者文件。
当前内核仍保持BATCH10已通过状态；B11尚未实际接线，不将文档预检称为作者实现验收。
独立文档复核后修正教材小节名称，其余接口/数值/边界与交付状态无必修问题。

## 已完成 BATCH10

2026-10-17作者已填写两个函数并移除guard，独立只读审查无必修功能问题。
实际make build无警告退出0，timeout 8s make run主动关机退出0；
Box/Vec同时存活4104字节、释放后统计归零，原双应用双向timer、三轮计算/各exit0一次正常。
只在临时副本添加既定重复初始化拒绝探针：活Box值/地址/统计不变，继续分配释放正常，最终QEMU0。
作者os/user源码和配置前后哈希一致，日志与副本 `.cache/batch10-author-review-nm8lhfbx/`。
功能和原三题已集中验收，不追加测试作业。

2026-10-17按作者请求将三题原答、逐题审查及修正版写入BATCH10第10节。
集中修正：物理页也需回收；本批固定堆大小不等于所有堆不能扩展；Vec::new无元素分配，
capacity是当前容量、len由操作维护；drop消费所有权并销毁元素/释放缓冲区。
重复init是重复登记空闲内存；锁守卫自动drop释放，区别于TaskManager的独占借用与unsafe别名责任。
同时修正旧参考解答中固定扩容规律、每块必有额外块头、锁必须显式释放等过度断言。
本次只更新讲义与进度，未改作者代码或重复构建/QEMU；理解题复核不代表实际实现已验收。
文本检查通过；独立复核指出len为0的Vec仍可能保留容量，已明确为capacity为0且尚未分配的本例。

2026-10-17作者询问Rust库分配与回到C后的对应方式，并希望恢复C主线时将小型辅助库与内核核心目录分开。
该后续偏好已记入根MEMORY；当前只解释接口/分配器/内存来源的分层，不迁移文件或恢复C主线。

2026-10-17作者要求继续并询问第三章位置。BATCH06..09已完成第三章核心主线，
终点为分时多任务与抢占；get_time169和task_info等练习未做，不追加为前置作业。
按固定教材顺序进入第四章第一节动态分配，整批目标/模块/路径/注意点/验收与三题见BATCH10.md。

新增mm/mod.rs与heap_allocator.rs；256KiB、4096对齐的静态堆放普通BSS，
extern crate alloc和global_allocator已接好。main在clear_bss、trap::init后、
加载应用和timer启动前调用mm::init。init_heap/demo_allocations两个函数留给作者，保留compile_error。
既有任务管理器、trap/switch汇编、用户程序和链接边界未改；不建立页表，satp仍为Bare。

依照固定ch4提交核对API，新增精确依赖buddy_system_allocator0.6.0，Cargo实际解析spin0.7.2；
两者MIT许可证保存在项目缓存，Cargo.lock已更新。只补项目缓存，无全局工具升级。
当前stable1.98.1默认no_std分配失败路径可链接到panic_handler，无需nightly alloc_error_handler。
作者make build已检查：新依赖正常编译，按设计仅因BATCH10 guard失败，不能宣称作者功能通过。

批前快照 `.cache/batch09-before-heap-4k1zbhrj/` 保留原os/user、Makefile及来源锁。
临时 `.cache/batch10-preflight-5h1vfeu9/` 仅副本补齐作者核心，固定两条P0均无警告构建、QEMU退出0：
正常Box42/Vec512内容、完整堆范围、对齐及释放统计通过；heap在普通BSS、低于应用窗口、
不与任务栈重叠，satp0/SIE0，原双应用timer/退出正常。
重复初始化在有活Box时被拒绝，原值/地址/统计不变，之后还能分配释放并回到基线。
实际观察capacity512、存活requested/actual4104、释放后均0、total262144，不把容量视为接口保证。
预检前后作者os/user源码哈希一致；P0由协作者承担，不追加OOM/压力矩阵或作者测试作业。
官方仅核对ch4相关参考，未构建验收完整ch4工程；sources.lock仅增加inspected_chapters。
骨架与讲义经独立只读复核无必修问题；新增/修改文本检查通过，原main.rs历史空白保留。
与批前快照比对，已有os/user仅Cargo.toml、Cargo.lock和main.rs接线发生预期变化，另新增mm模块。

## 已完成 BATCH09

2026-10-17作者完成四处核心并移除guard，实际构建无警告，独立只读复核无核心错误。
工具普通输出/伪终端两次运行均timer跨任务、自身抢占、各三轮/exit0一次，最终正常关机0。
缓存 `.cache/batch09-author-review-yjy0gr0n/` 保留作者核心仅加P0观察：
唯一Running、frame/sp/sepc归属、保存SPP/SIE0、实际SIE0/STIE1、timer前后全34字段不变；
正常0，注入timer编程Err后无task start/用户输出且SIE/STIE0、失败关机255。
全部os/user源码哈希前后一致。功能验收通过，原三题已作答并在BATCH09第10节集中复核。

2026-10-17按作者请求修正第10节原参考解答，保留三题初答并逐题排列：
区分time速率/间隔/绝对截止值；重装timer清pending并设置下一事件，不能声称不重装就无中断；
ecall请求已处理后按ABI推进sepc，不称其正常执行退休；timer保持原恢复位置与a0。
修正U态sscratch保存内核栈顶、交换后才保存用户sp；SIE0只阻止相应S级嵌套中断，不能阻止同步异常。
仅改文档与进度记录，没有改源码、追加测试或重复构建/QEMU。
文档文本检查通过；独立只读复核确认四处概念问题修正，并将sepc推进时序明确为分发syscall前。

本次定位长期作者终端timeout而工具不复现的环境差异：.bashrc123将
/home/asus/src/Sys/qemu/build前置PATH，终端裸QEMU是11.1.50开发版(c1c18d1e64)，
工具环境是/usr/bin系统10.2.1。同一kernel bin SHA256
f588e894d83bbf037222c184c5b04af2d0a187abcff00161621099925ea57012与固定固件，
开发版在allExited后挂起timeout124，系统版正常0。此前DTB尾13b0/13a4差异也随版本复现。
进一步只改开发版DTB的/soc/syscon@100000为旧名test@100000，开发版也正常0，
确认当前旧RustSBI关机与DT节点命名的兼容点；不直接归因为作者Rust/timer错误。
Makefile默认QEMU改绝对/usr/bin，仍允许QEMU=显式覆盖；模拟开发版优先PATH后make run退出0。
仅修改本项目Makefile和文档；不改作者核心、全局PATH/开发版源码，不升级固件或安装工具。
对照版本/hash/修改前后DTB/默认命令与诊断运行日志保存在上述缓存；ENVIRONMENT.md和BATCH09第11节已记录。

2026-10-16作者要求下一步，进入第三章S timer用户态抢占。整批见BATCH09.md。
新增timer.rs，read_time/set_next_trigger/enable_timer_interrupt及trap的S timer分支留TODO。
timer.rs保留compile_error；其余接线已提供：非legacy SBI TIME探测/返回值检查，
main在trap/任务初始化后arm首事件→STIE→run_first；初始frame清SPP/SIE置SPIE；
task包装复用作者suspend状态转换，timer/yield分开标日志。manager.rs/TaskContext构造/switch/trap汇编未改。
用户各三轮固定20_000_000次减一/分支计算，不调用yield，不需要U读time权限；保留旧yield接口。
内核SIE始终0，U态可接STIE事件；不实现S嵌套、内核抢占、Blocked/sleep或页表。

备份 `.cache/batch08-before-timer-p1edw15w/` 保留批前os/user源码及Makefile；
同目录virt.dtb由当前QEMU直接dump，解析/cpus timebase-frequency=10000000。
本地取100Hz、间隔100000ticks约10ms；固定官方ch3已动态读DTB，此处只用已验证平台常量。
sbi-rt0.0.2 set_timer(u64)->SbiRet与TIME probe由本地缓存核对，相关官方ch3/架构/固件由子代理只读核对。

临时 `.cache/batch09-preflight-p1glgvfm/` 仅副本补齐核心并加入观察，两条P0无警告构建：
正常默认QEMU无yield，双方完成前timer跨任务0→1/1→0，三轮/退出各一次，最终全Exited退出0。
本次app0=3/app1=2次timer，数量不固定；每U trap检查frame/sp/sepc归属、保存SPP/SIE0、
实际SIE0/STIE1、唯一Running，并比较timer切换前后全34字段不变。
拒绝场景仅临时SBI包装注入编程Err，SIE/STIE仍0，无start/用户输出，诊断后失败关机255。
作者os/user源码哈希前后一致；没有重跑旧矩阵、写新测试作业、升级依赖、提交或清理。
make user无警告；当前作者make build按设计仅因B09 guard失败，尚未验收作者核心。
新文档文本检查通过；内部子代理最终只读复核无阻塞问题。
作者manager.rs、TaskContext构造、switch.S、trap.S与批前快照逐字节一致；
临时内核反汇编gp/tp仍只见于trap保存/恢复。
完整章节官方工程仍未经本地构建验证，不与作者局部预检混淆。

## 已完成 BATCH08

2026-10-16作者修正首次Running状态和1..=APP_COUNT范围后，实际timeout 8s make run
无警告构建、完整17条调度日志严格符合预期：5次yield（跨任务4/自身1），
app0退出后app1继续完成，两任务各exit0一次，最终QEMU主动关机退出0，无panic。
此前临时副本的唯一Running/frame归属与拒绝路径/累计失败验证仍适用，不重复扩展测试。
本批功能验收通过；核心实现保持作者版本，原三题已回答并集中讲解，下一步进入timer抢占。

2026-10-16按作者请求将三题复核写入BATCH08.md第10节：TrapContext是内核栈顶部的用户现场，
TCB.task_context是内核ra/sp/s续点；本批yield为主动让出而非时间片耗尽；
当前只有短作用域&mut管理器引用，无exclusive_access或运行时锁，需区分别名约束与锁死锁。
一并补齐动态write归属检查、self-yield和无主动让出/退出时另一任务无法运行的说明。
仅更新文档，不改源码或重复构建/QEMU，不追加理解题或测试。

2026-10-16作者报告current_task_id的Running断言失败。实际make run复现：
start0后第一次用户write查询任务时leftReady/rightRunning，内核失败关机255（make2）。
start_first未把Ready改Running，反而把非Ready强制置Ready；应保留Ready前置断言再设置Running。
另find_next_task的1..APP_COUNT漏查自己，应为1..=APP_COUNT；suspend的兜底分支暂时掩盖遗漏。
其余suspend/exit转换未发现功能问题，独立子代理只读复核一致。作者核心未代改。
缓存 `.cache/batch08-author-review-h3afdl_j/` 只改上述两处并加既定观察：
两次构建无警告，精确调度顺序/唯一Running/frame与用户sp、sepc归属均通过；
正常退出0，app1跨任务write拒绝-1且app0返回7/随后app1返回0的累计失败场景退出255。
全部os/user源文件哈希前后相同；作者当前实现仍待自行修正复验，不标记完成。

2026-10-16作者要求继续。BATCH07三题已集中修正：换sp仍需保存寄存器；
普通调用ABI与任意用户执行点trap的保存要求不同；保存的ra/sp决定续接；
mv a0,sp是a0=sp；ret跳ra且结果通常在a0，sret跳sepc并按SPP恢复特权级。

BATCH08.md一次给齐协作调度目标/模块/执行链、四函数算法、注意事项、两条P0和原三题。
task/manager.rs新增TaskStatus(Uninitialized/Ready/Running/Exited)、TCB及TaskManager；
find_next_task/start_first/suspend_current/exit_current四函数留TODO，其余接线已提供。
__switch和作者TaskContext构造未改；撤下B07独立演示，改为全体任务初始化后run_first。
syscall124与用户yield_now、两个用户2/3轮循环已接；sys_write改查动态当前任务。
trap_handler在syscall前复制参数/推进sepc并结束临时frame借用，切回后写原frame.x10。
manager引用在__switch前结束，只取真实静态TCB字段raw指针；自身yield不调用__switch。
exit只结束当前任务，直到全部Exited才按累计非零退出状态关机；无timer/Blocked/页表。
快照 `.cache/batch07-before-scheduler-lto_ouj5/` 保留B07内核/用户源码及Makefile。

临时 `.cache/batch08-preflight-ha7wu_q1/` 仅补齐四函数和验证观察：
正常精确17条日志、5次yield（跨任务4/自身1）、各exit一次、最终QEMU退出0。
每次状态更新后恰一Running且匹配current；每次U trap的frame/sp/sepc归属正确、SPP0。
第二次app1原始write读app0镜像1字节得到-1，随后正常调度；app0临时返回7，
app1继续并返回0，最终QEMU退出255，验证软件范围检查及失败累计。
两次构建无警告，作者os/user源码哈希前后不变，未额外添加测试框架或寄存器矩阵。
make user无警告生成新镜像；作者内核故意保留BATCH08 compile_error，尚未验收作者实现。
make build确认仅因该guard失败；内部子代理只读复核文档/骨架无阻塞问题。
新文档文本检查通过，作者TaskContext/switch.S/trap.S与批前快照逐字节一致；
临时内核反汇编gp/tp仍仅见trap保存恢复。
固定官方ch3相关task/syscall路径已git show核对，不切参考分支或宣称完整ch3验证。
未升级依赖/安装工具/提交/清理或修改TinyOS主线；根MEMORY不堆教程日常进度。

## 已完成 BATCH07

2026-10-15作者已将rp修正为ra。实际timeout 8s make run无警告构建，
五条boot/demo日志顺序正确且各一次，随后app0 Hello/write/exit0，QEMU退出0。
同一bin直接复跑也退出0，完整日志在 `.cache/batch07-confirmed-run.log`。
本批功能验收通过；此前临时副本的frame/sp不变量及非法id拒绝/app1结果仍适用，未重复矩阵。
保留一项观察：首跑application exited后出现不完整的`[rustsbi-panic] hart 0 panicked at "`尾片，
进程仍退出0；同一bin直接复跑未出现。未据此推断原因，也不将首跑称为无固件异常输出。
若该固件尾片复现再单独排查，不改动已通过的切换实现。
下一步先按BATCH07原三题梳理两类现场/续接/首次入口，再接yield协作调度。

2026-10-15作者请求检查错误：已完成两个构造函数与五条汇编并移除guard。
实际make build在switch.S第27行报invalid operand：ld rp,0(a1)，rp应为ra。
作者源码未代改；其余核心布局/保存恢复顺序审查无功能问题。
缓存 `.cache/batch07-author-review-2iettnv2/` 只修正此拼写并加既定观察：
两次构建无警告，boot/demo两轮顺序、两份frame不变、保存sp范围/对齐均通过；
非法id2先Err且BOOT/DEMO及frame不变，分别进入app0/app1，QEMU均主动退出0。
全部os/user源码哈希前后相同。作者工作区仍需把rp改为ra再构建，不提前标记本批完成。

作者2026-10-15要求继续下一步，按第三章接TaskContext/__switch。
整批任务见BATCH07.md：两个构造函数与五条汇编指令留作者，重复s寄存器槽、
FFI、两轮boot/demo往返、首次用户启动接线与__task_bootstrap已提供。
main改为装载/准备两份frame→演示→task::enter_first；移除旧trap::enter_user包装，
实际trap.S/handler/syscall未改。现有restore要求a0=frame，故入口适配mv a0,sp;j __restore。
交付时task/mod.rs保留compile_error，完成核心后移除；当时make build按设计因guard失败，make user独立可用。
未加入调度器、任务状态、yield或timer；APP_STATE/TaskStatus随下一批任务管理再接。

预检 `.cache/batch07-preflight-8_burpyd/` 仅临时副本补齐核心：
两次无警告构建、QEMU主动退出0，五条往返日志严格有序。
第一条保留两份frame全部34字段，核对保存boot/demo的sp范围及demo的16字节对齐，再进入app0。
第二条先run_switch_demo(id2)返回Err，BOOT/DEMO记录及两份frame不变，再合法演示并进入app1。
反汇编核对ra/sp/s0..s11的112字节布局、入口适配及原sret链；gp/tp仅见trap汇编保存恢复。
作者os/user源码哈希前后不变；预检通过不等于作者TODO完成，不追加测试框架作业。
当前作者工作区make build确认仅因预设guard失败，用户两镜像构建正常；
内部子代理只读复核文档/骨架/恢复接口，无阻塞问题。新文档UTF-8、空白和代码围栏检查通过。
首个预检构建只因临时观察代码多余unsafe告警停止，修正观察后上述两条通过，核心无需改动。
改动前快照 `.cache/batch06-before-switch-9fb6yvpa/`；未清理、提交或改TinyOS主线。

## 已完成 BATCH06

2026-10-15作者报告构建/输出正确。集中静态审查无功能阻塞，两条P0通过：
实际timeout 8s make run无警告构建，两个ELF入口分别80400000/80420000，
主工作区默认app0正常Hello/exit0，QEMU主动退出0。
缓存 `.cache/batch06-author-review-cljk603v/` 保留作者算法，只改副本START=1并加观察：
两槽全部镜像/零尾、两组独立8192字节栈、两份272字节frame的字段及对齐均通过；
load_image/prepare_app输入id2都Err，原两镜像和两frame保持不变，再启动app1正常退出0。
全部os/user源文件哈希前后相同；内部子代理只读复核一致，没有扩测试矩阵或代改核心。

作者随后已自行删除main旧单app准备/日志及提前enter日志，现为完整双app装载/准备链。
日志enrty拼写等不阻塞。完成点是两app同时驻留、分别可启动，尚未进行CPU轮流调度。
原三题已回答，主要理解正确：独立装载地址需匹配链接布局，栈/现场保存独立状态，尚无调度器。
集中补充：不仅是尚无页表，还因为没有重定位；以后有页表时仍需匹配链接虚拟地址，
物理位置才可由映射变化。独立栈保留局部变量/调用链，TrapContext保存用户寄存器。

2026-10-14作者明确“继续下一章”，按此进入第三章；第二章顺序替换应用的批处理循环略过，
不宣称该功能已完成。第六批范围为两应用共存装载/独立栈/现场，执行选定的一份后仍关机。
TaskContext/__switch/yield/调度与timer留后续批次。完整任务在BATCH06.md。

协作者已准备hello_peer与按bin的USER_BASE_ADDRESS链接参数、动态窗口ASSERT，
user/build.rs跟踪链接脚本；make user无警告生成两镜像，各1013字节，ELF entry分别80400000/80420000。
loader中APP_IMAGES表、两静态栈数组、id校验与边界getters接好；作者已完成app_base_address/load_apps。
保留作者load_image/prepare_app主体，增加app_id、各自目标/栈；prepare不再复制镜像。
write已机械改用START_APP_ID的槽/栈，不能访问另一应用区域；作者已接main全体加载/两frame循环。
START_APP_ID默认0，guard已移除，make build/run/user可用。此前源码检查点在
`.cache/batch05-before-ch3-xm7y6xw2/`，没有清理/提交或改TinyOS主线。

临时 `.cache/batch06-preflight-0kgl7avr/` 补全后无警告，P0预检两个QEMU均退出0：
先逐字节核对两镜像及各自完整零尾、两套8192字节栈/272字节frame字段/对齐，再启动app0；
第二次副本START=1，load/prepare输入id2均Err，原两镜像与两frame不变，再运行app1退出0。
两次都只出现选定app的Hello，不能称为轮流调度。地址仅代表临时布局，不应硬编码：
app0 user_sp=0x80218000 frame=0x8021bef0；app1 user_sp=0x8021a000 frame=0x8021def0。
仅阅读固定ch3提交9ca4aedb66f626dcc72f835a5946742a82b61c7c，未切参考分支或验证官方完整ch3。

## 已完成 BATCH05

2026-10-14按作者 `[笔记] -> (嵌入BATCH05.md)` 请求纠正并加入完成后机制梳理：
区分栈顶K与现场K-272、call建立返回地址、SPIE/SIE/委托边界、a0两种用途、
实际restore尾部和用户exit链。核对Makefile无timeout，8秒限制来自终端外层命令。
仅改文档，未修改核心或重复运行测试；原任务段中尚未完成的历史表述同步校正。

BATCH05 作者已填写核心并移除guard/TODO，2026-10-14集中静态审查与两条P0通过。
作者修正syscall helper/溢出检查/exit失败标志、handler取x17作为编号、init残留TODO后，
当前make run无警告构建，用户Hello一次、application exited code0一次，主动退出0。
缓存 `.cache/batch05-author-review-ibioht33/` 保留原内核算法，只在临时用户main加入原始ecall：
buffer=usize::MAX、length2得到-1，随后从栈内缓冲区成功输出、Hello一次、exit0，QEMU退出0。
作者全部os/user源文件哈希前后相同，未扩测试矩阵；内部子代理只读核对无核心阻塞。
作者终端日志仍见from(timeout)，本次当前源码复跑未复现，不能将其算作者终端主动关机，
也不能据此认定D段错误；若需追查，应核对作者运行环境/产物及SBI是否返回。
执行链已梳理：rust_main→restore/D sret→用户_start调用main→main返回0→用户exit93/ecall→
内核alltraps/handler/syscall→内核sys_exit打印并关机。exit路径不再执行D，write返回会执行D。

trap.S全部整数槽含x4、x0槽零；用户sp通过sscratch保存，handler返回frame地址，
syscall结果写x10；load/user_stack_bounds等接线保留。保持单hart、sie=0、只支持来自U的trap，不实现嵌套S trap、
浮点/向量现场、多应用管理或定时器。未知用户异常先诊断后失败关机；未知syscall返回-1。

2026-10-13缓存 `.cache/batch05-preflight-_8afzkdw/` 补全副本无警告构建，两个QEMU场景退出0：
正常路径出现U ecall（scause8/保存SPP0），用户Hello一次，write返回后main0→exit0；
临时用户先发buffer=usize::MAX/length2的write，收到-1，再从用户栈缓冲区输出、Hello、exit0。
核对正常反汇编sscratch交换/272字节槽/CSR/sret，gp/tp仅在汇编保存恢复使用，
未把临时补全副本计为作者完成。P0固定正常+一条坏write，不增加故障矩阵。
完整任务、运行前提、必要Rust/汇编语法、约三个理解题已一次给齐，见BATCH05.md。
系统库问题仅作讨论：作者所指是适用于项目的小型辅助系统库，现有user_lib/内核模块可按需增长；
未授权另建库或切换实现语言，不应将其误解为要求完整libc++。

## 已完成 BATCH04

2026-10-13作者已修正重叠条件为source_start<target_end && target_start<source_end。
协作者复核后正常构建/QEMU关机退出0；此前镜像/现场/超长拒绝验证仍有效，本批收尾。
仅校正装载安全注释：应用窗口来自QEMU物理RAM约定，链接断言防内核覆盖，并非链接器分配；
fence.i保证本hart写后取指可见性，不承诺具体缓存刷新实现；补栈内现场写入的对齐/独占前提。
核心算法未代改。三题理解练习保留在BATCH04.md，可随下一批执行路径一起回看。
作者运行日志含signal15外部终止；协作者timeout 8s make run无警告构建并正常关机退出0，
复现相同entry/栈/上下文地址，没有signal15。作者后来完整日志明确from(timeout)，
说明那次到8秒仍未结束后由timeout终止；超大镜像返回Err、assert通过本来就不打印错误。
当前实际user_sp=0x80215000、context=0x80216ef0、ekernel=0x80217000。
缓存 `.cache/batch04-author-review-xt3odd4n/` 仅增加验证探针，未修改作者算法：
RAM全部131072字节的镜像/零尾、两栈/272字节现场/字段/对齐检查通过；已有现场准备完成后，
传131073字节镜像得到Err且窗口与现场全保持原值，QEMU退出0。作者核心哈希前后相同。
main中的作者临时超长探针已自行移除；不追加测试矩阵。内部子代理只读审查一致。

已建立 os/src/loader.rs、trap/context.rs 与模块导出，main 接prepare_app/打印；
Makefile 的 build 先构建 user，链接脚本增加 ekernel<=0x80400000 断言。
应用窗口 [0x80400000,0x80420000)，两栈各8192字节、4096对齐，现场272字节。
所有尺寸/源目标重叠拒绝必须发生在写RAM前；本批没有 sret、trap入口或用户执行。

2026-10-13缓存临时副本 `.cache/batch04-preflight-9eb1p2se/` 补全后无警告构建。
正常 QEMU：镜像1013字节逐字节相等、整个尾部清零，所有现场字段及对齐检查通过，
entry=0x80400000、user_sp=0x80217000、context=0x80218ef0、SPP=0，正常关机退出0。
另一个临时路径先准备现场，再传128KiB+1字节镜像，Err且原镜像/尾部/现场均不变，退出0。
该临时副本的 ekernel=0x80219000；地址仅代表该布局，不能硬编码为作者栈地址。
内部子代理只读审查骨架/完整任务通过；预检不等同于作者实现完成。未改TinyOS主线/清理/提交。

## 已完成 BATCH03

BATCH03：第二章用户执行环境，作者核心完成，独立 ELF/bin 静态验收通过。
2026-10-13最终复核：ELF64小端RISC-V，入口/_start=0x80400000，无未定义/gp，
write64/exit93寄存器接线及main->exit正确，bin1013字节，BSS为空且边界均0x80400400。
作者已修正hello文本与负值判断；协作者只把仍残留的失败判断 && 改为 ||，
并补syscall接口/同步读取缓冲区的SAFETY说明。当前负值或短写返回Err，全长/空串返回Ok；
缓存宿主探针直接包含作者console.rs，四种返回值分类均PASS，未要求作者追加测试。
sys_exit改为->!并内部循环兜底有效，不要求搬回lib；BSS使用isize在本固定地址可工作，
推荐usize但不作为功能阻塞。用户代码尚未执行，不能称为U-mode运行验证。
作者核心范围：user入口/BSS、syscall寄存器包装、write/exit参数及console后端、应用main。
协作者已建user Cargo/链接脚本/空依赖lock、Makefile用户构建/检查目标和VSCode manifest关联。
用户入口0x80400000、应用窗口128KiB，与固定官方ch2一致；本批不执行用户代码。
源码依据从现有完整克隆git show读取ch2固定提交，未切分支或改参考克隆。
stable版用外部Rust ABI main，没有旧linkage/weak默认main；暂不引入riscv依赖。

临时副本 `.cache/batch03-preflight-jyp6cxxx/` 补全后构建无警告，ELF64小端RISC-V，
entry/_start=0x80400000；无未定义符号/gp依赖，反汇编含write/exit的ecall及a7=64/93。
预检bin1013字节仅代表当时临时实现；作者当时骨架尚未完成，后来状态见上方集中检查。
本批P0为构建与静态检查，不增加QEMU/故障矩阵。原os核心与TinyOS主线未改，未清理/提交。

## 已完成 BATCH02

BATCH02 作者实现已完成，2026-10-12 集中检查与 P0 通过，第一章功能收尾。
console 的字节输出、Arguments/write_fmt、两个宏和 panic 诊断正确。
协作者修正 main.rs 栈范围次序/减法（原先无符号下溢输出大数）、BSS半开区间文本，
并补任务约定的 value=42/hex=0x2a 演示行；其余作者实现保留。
当前正常路径输出正确 65536 字节、范围与本次 ELF 符号一致、Hello一次、数字格式化正确，退出0。
临时副本在正常输出前触发panic，消息/实际文件与行号匹配，Hello零次，退出255。
记录在 `.cache/batch02-final-3230yvaw/`；没有扩展验证矩阵或运行TinyOS主线。
非ASCII输出的额外转码仍为已有兼容限制，不作为本批作业。

## Cargo 清理与 VS Code 诊断（2026-10-12）

用户明确授权检查、清理Cargo、排查rust-analyzer不显示诊断。
按顺序验收后在作者 os/ 执行局部 cargo clean，删除39文件/2.1MiB，依赖缓存保留。
随后编辑器 cargo check 生成约636KiB检查缓存，正常通过。

根因证据：VS Code WSL 扩展自带 rust-analyzer 0.3.3081 可启动，实际 Language Server日志
写 failed to find any projects in /home/asus/tinyos。Rust包嵌套在experiments/rcore-learning/os，
旧配置linkedProjects为空。已在根.vscode/settings.json保留原键并增加明确manifest关联、
RISC-V目标、局部CARGO_HOME/离线检查与allTargets=false；开启保存检查和诊断。
stable缺标准库源，已从安装工具链manifest读取固定Rust1.98.1 rust-src URL/SHA256，
校验后解压在 `.cache/ide-rust-src/`，配置sysrootSrc；没有升级或改全局工具链。
原配置备份在 `.cache/vscode-before-wjddu5b0/settings.json`。

验证：当前源码cargo check和扩展自带RA无界面诊断扫描通过；缓存临时副本删除unsafe块后，
RA扫描与保存检查命令均准确报告write_volatile的E0133。作者源码未留错误探针。
此版本仅addr_of!(外部static)无需unsafe，不能把这种不报错当故障。
实际VSCode界面需重启rust-analyzer并保存文件应用新sysroot配置，界面红线尚未直接观测。

## 已完成 BATCH01

BATCH01：概述、基本执行环境、最小启动。作者完成实现，2026-10-12 P0 已通过。
三题答案已交，启动与特权级的主线正确；集中概念修正在 BATCH01.md 答案复核段，
无需重做或扩展作业。

本次当前作者实现的验收结果：

- make build 退出 0，无编译警告；timeout 8s make run 目标 Hello 一次，正常关机退出 0。
- ELF64 小端 RISC-V；entry/_start=0x80200000；先建立 sp，再调用 rust_main。
- 启动栈范围 0x80201000..0x80211000，65536 字节，16 字节对齐。
  sbss=ebss=栈顶，普通 BSS 当前为空；清零算法静态检查正确，运行时此区间不执行写入。
- 仅缓存临时副本在输出前触发 panic，构建无警告，Hello 为零次，QEMU 退出 255。
- 作者 os 源码/Cargo/config 哈希验证前后完全一致；没有改核心实现或执行 TinyOS 主线。

## 初次问题排查（历史）

2026-10-12 集中检查：实际 make build 报四条 Rust 错误：sbi::rt 应为 sbi_rt、
Shutdown 没有引入、main 的 console_putchar 没有引入、两种 reset reason 类型不能
放在同一个 if 表达式结果中。另发现 linker.ld ALIGN 赋值缺分号、两个正常路径 todo!
会触发 panic、汇编栈底 global 名字多点。BSS 循环与 str.bytes 输出本身正确。
SAFETY 注释需明确 volatile 不提供并发保护，设置 sp 不等于证明不会栈溢出。
协作者只在缓存临时副本中修正接线/收尾，实际构建和 Hello/正常关机退出 0；
作者三个源文件与链接脚本没有代改，原工作区仍未通过。下一步作者集中修正所列问题。

## 编辑器显示与安全注释（2026-10-12）

本次处理作者确认的两个问题：Kate中文方框来自Ubuntu缺少中文字体，main.rs仍是合法UTF-8；已安装fonts-noto-cjk并刷新字体缓存，系统中文字符/等宽字体匹配通过，Kate当前窗口的实际重绘尚需作者保存后重开确认。clear_bss仅修正SAFETY注释：边界来自链接脚本与QEMU RAM布局，排除整个启动栈，单hart早期初始化负责无并发；volatile不提供并发保护，设置sp不等于证明不会栈溢出。循环实现未修改。内部子代理只读核对链接/汇编及Rust官方volatile文档。此次未构建或运行QEMU，不改变BATCH01未验收状态。

## 初始准备与验证（2026-10-11）

- 完整阅读根约定、记忆和原入口，保留所有既有修改；主线 ELF 草稿继续暂停。
- 固定书与 ch1…ch9 来源；只下载并验证 ch1，其余未构建。已有完整 main 克隆保留。
- 复用 Rust 1.98.1/RISC-V target、QEMU 10.2.1/GNU binutils。
  Cargo 下载放 `.cache/cargo/`，未升级/安装全局环境。
- 官方 ch1 缓存副本通过 Cargo 构建、binary 转换、QEMU Hello 和正常关机，退出 0。
  固件版本与入口由实际输出核实，参考依赖锁保存在 versions/。
- 作者 Cargo 包、链接布局、局部构建入口和核心 TODO 已建立。
  作者工作区尚未运行验收，参考通过不等同于作者完成。
- bash 语法、版本 JSON、下载/固件校验与本目录文本空白检查通过。
  作者 make build 离线解析依赖成功，按设计仅因未完成 guard 失败（Cargo 101 / make 2）。
  缓存、target 和已有完整 clone 确认被忽略；开始时记录的 126 个既有文件中，
  只更新本目录 README，其余保持原字节内容，未暂存/提交/清理。

## 已知限制

- 官方 book 标题仍为 alpha；固定提交是学习基线，不宣称正式稳定发行。
- 作者已完成首章、第二章单应用U-mode/write/exit及第三章双应用共存装载，BATCH06两条P0通过。
- 作者终端曾选中开发版QEMU而关机超时，已定位DT节点命名兼容问题；Makefile默认系统版已复验退出0。
- 后续章节依赖/API/QEMU 兼容性仍到章核对，不把固定来源当作完整验证。
- Rust 复用现有 stable 的精确版本核对，而非独立安装副本；全局升级后拒绝漂移。
- 缓存被忽略，丢失时按 ENVIRONMENT.md 恢复；当前可离线构建。

## 下一步

BATCH12功能已通过，集中讨论第11节原三题，再推进页表构造与查询。
页池只提供物理页所有权，本批不安装映射；当前作者源码可运行，未开启Sv39。
历史固件关机尾片观察见BATCH07记录。
第二章顺序批处理按作者选择略过，不再自动安排为第三章前置作业。
第一章理解题可简答或阅读时回看，不作为新批次的额外前置作业，不重发已有测试。
