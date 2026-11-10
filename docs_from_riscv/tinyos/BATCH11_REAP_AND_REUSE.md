# BATCH11：收集用户退出结果与静态槽复用

状态：功能、全部既定P0及原三题原答/集中审查/修正版归档完成，本批收尾。修正版由协作者整理，不冒充作者复答。日期：2026-11-07。

2026-11-06接入规范并提取架构上下文后，现场访问改为`task->context.frame`，
初始化使用`riscv_task_context_init(&task->context)`，结果类型位于`task/task_exit_info.h`。
详见[模块边界](../TASK_MODULE_DESIGN.md)。B11的接口、回收顺序与验收条件保持。

后续安排（2026-11-06）：用户要求下次继续写B11时同步拆解`sched.c`并建立`process`模块。
先给出接口与状态/资源归属，再按原收集协议实现；详见[模块边界](../TASK_MODULE_DESIGN.md)。
当时仅记录计划；现已实施Process，具体结果见下面最终验收。此安排替代先完成B11再拆表的旧顺序。

导航：[批次索引](README.md) · [上一批](BATCH10_USER_EXIT_RESULT.md) · [总路线图](../TINYOS_EVOLUTION.md)

## 当前起点与Process拆分落点（2026-11-06）

作者已完成allocate的选槽/高水位/ID草稿。接下来先实现Process基本操作，再把空间/结果从Task移出，
与创建、退出及B11收集接线。完整文件、字段归属、接口、调用顺序及C++迁移注意点见
[Process实现起点](../TASK_MODULE_DESIGN.md#b11实现起点process基本操作与接线)。

采用固定Task数组+同容量Process存储，Task借用可空Process指针；不增加独立PID/分配器。
`sched.c`迁为`sched.cpp`，保持公开C ABI。下文Task中的user_space/exit_info指其关联Process拥有的记录；
按ID找Task、核对调度状态仍在Task侧，空间释放/结果保存与reset集中在Process侧。
原非阻塞返回、wire协议、资源时序、演示及验收条件不变；以下过程草稿按时间保留，最终状态以上方状态和最终验收为准。

## 最终验收：功能与全部既定P0通过，进入理解收尾（2026-11-07）

作者将sched_init改为直接遍历真实g_process存储，先全量预检，再reset全部Process及Task元数据，
idle也通过公共metadata助手清理后准备现场。正式源码未由协作者修改。

源码scope对比证明，相对前次事务/复用/FAULT/宽值验证，仅sched_init函数发生变化，其余67个源/配置中的
其他文件和函数保持。最新正式构建成功/nm-u为空，20秒正常双U完整演示、恰一次收集及后续S tick再次通过；
不因仅初始化变化重跑不受影响的82宿主及整套既定REAP探针，前次证据继续有效。

独立最新init副本验证：

- 已有用户Process持有资源时，以另一个合法根调用初始化，返回-1；完整Task数组、Process数组、idle、
  所有调度global、PMM调用/可用页及satp均保持。较早空Process中预置的status303也保留，
  证明没有在遇到后面的拥有资源对象前提前清理结果。
- 所有资源为空但Task/结果/idle元数据有旧值时，重初始化成功；所有槽（含高水位后的槽）和结果清空，
  ID/count/current重置。idle元数据、初始frame逐寄存器、PC/特权标志及16字节对齐全部正确。
  初始化零PMM alloc/free、可用页和satp不变，静态栈地址稳定。
- 上述初始化探针之后继续真实启动双U，仍收集一次并以0/0退出，无fault/halt。

最新证据.cache/batch11-init-final-2026-11-07/{scope.json,results.json,normal-result.json,run_probe.py}，
构建/正常/探针QEMU日志和前后快照同目录；QEMU均由runner限时停止。正式67源配置哈希保持。
前次完整功能P0（82宿主、wire、21次事务调用、同槽新ID真实执行、FAULT/宽值与7+7）见下一节。
功能及全部必要验证完成，原初始化缺口已关闭；P1/P2仍选做，无新增作者测试任务。

接下来只做功能梳理及本文末尾原三题；尚未收到作者原答，不标理解收尾/整批归档完成。
不自动开始下一批，不stage/commit/push/clean。

## 当前验收：REAP主线与功能P0通过，剩初始化收尾（2026-11-07）

作者已改peek非1分支为return res并删除main旧重复块。协作者保持正式源码不变集中复验：

- 正式make -B -j4完整构建无warning/error，nm-u为空；用户RX0x8d0仍在一页内，RW内存0x1038保持四内容页。
  wire头的实际目标C编译和独立目标C++20大小/偏移断言通过，原82宿主场景及汇总通过。
- 正式20秒QEMU：双tag start/end各一次、counter OK各三次、Time/Data/Hello各两次，
  U0 REQUESTED0后U1恰一次reaped且早于U1 end，再U1 REQUESTED0，后续S tick持续，无fault/halt。
- txn-reuse副本实际21次REAP：10次活目标0（含坏地址及哨兵buffer不变且无copy），7次非法-1
  （S0/S1/self/负/未知/原始INT_MAX+1/原始2^40+2），DEAD坏地址-1保留完整Task/Process，
  合法重试1复制正确wire并消费，两次旧ID拒绝。每次REAP均零PMM alloc/free。
- 同一成功REAP trap、任何reschedule前，临时创建短S实际复用slot2但分发ID4，高水位仍4；
  内部查找4→2、旧2不存在，context/元数据正确且新任务真实运行，创建同样零PMM。
- fault-wide两副本分别真实FAULT13（单次、scause不同sstatus、PC未推进）和EXIT(-2^40)，
  U1完整正常演示后实际收到并收集{FAULT,0,13}/{REQUESTED,-1099511627776,0}。
  各用户七唯一页/先kernel根/根最后，幸存Process字节/页表/四内容页及shared均保持，
  reap零alloc/free且不二次释放；最终十四页归还，结果跨两个后续timer保持。

证据统一.cache/batch11-final-2026-11-07/：build/host-tests/normal日志与结果、txn-reuse/、fault-wide/、summary.json。
各QEMU均由观察计时器停止，不表示客体自行关机；仅当前单hart、四内容页布局。67个正式源/配置哈希保持。
既定REAP功能场景完成，没有追加作者矩阵；理解题尚未收尾。

仍未完成的原定要求是sched_init：当前只清globals/idle，没有全Process资源预检与全槽重置。
独立init-guard副本在已创建U0、Process持有空间而任务尚未运行时调用：实际返回0，task_count从3变0，
应为-1且元数据不变。该失败已记录于init-guard/result.json，不能标整批完成。
修正顺序：root地址检查→先只读确认所有Process无资源（否则零修改拒绝）→reset所有Process→
用task_clear_metadata清全部槽、置UNUSED→清idle元数据并准备其context→提交根/ID/高水位/当前槽初值。
只重置元数据，不清整块栈、不释放用户页；boot无运行任务/SIE关闭前提保持。

## 后续已修主体，剩两处旧代码（2026-11-07）

已保存的syscall局部info/&info/点访问、FAULT宏、commit失败停机以及EXIT兜底正确；
main已有U1的新ID2轮询与结果验证，但后面仍有旧空指针result/user_tag目标ID/旧消息块，
会提前return0并跳过原end，应删除旧块保留新块和end。syscall的peek非1分支仍应return res而非-1，
否则0被吞。make成功且目标已最新，未运行完整验收，正式功能源未由协作者修改。

## REAP接线草稿复验：构建成功，实际收集仍返回-1（2026-11-07）

作者认为已过，协作者核对实际保存文件并复验。编号7/case/用户声明与汇编入口、固定24字节结果头及布局断言已接入；
正式make -j4成功（目标已最新）、ELF无未定义符号。但syscall_reap仍TaskExitInfo* info=NULL传peek，
peek立即拒绝，所有请求返回-1；res!=1也返回-1，错误吞掉活任务返回0。须本地TaskExitInfo info={0}并传&info，
随后if(res!=1)return res，访问改info.field。复制后task_reap异常失败仍返回-1，应按原契约诊断停机。

正式user/main.c尚未调用user_reap，不会走收集路径，旧演示通过不能代表REAP通过。
隔离副本仅在U1 end前插入最小调用：确认U0已REQUESTED0退出后REAP仍返回-1，U1因此退出status91(0x5b)。
副本完整构建成功，正式源哈希保持；日志/结果.cache/batch11-review-2026-11-07/reap-probe/，
16秒runner停止QEMU。此次未改产品、未跑完整B11 P0或无谓重跑旧82宿主。

另有新增接线回归：user_exit的ecall后原兜底1:j1b被移到get_time/reap之后，意外返回时会落入GET_TIME并ret；
应把循环放回EXIT ecall紧后。FAULT宏拼为TASK_RESUT_FAULT，虽定义/使用一致可编译，建议统一TASK_RESULT_FAULT。
peek拒NONE按用户确认的三合法枚举前提保留，不新增白名单或异常枚举测试；sched_init全量预检/槽重置仍为原待办。

## 进入REAP syscall接线（2026-11-07）

作者已新增task_clear_metadata及task_reap，reap按身份/DEAD/有效reason核对后reset Process、清元数据、UNUSED最后，
不改计数或再次free。当前peek简化NONE判断时误写`info.reason != TASK_EXIT_NONE`，须改为`==`才是拒绝未退出结果；
活分支仍宜明确三种合法活状态，末尾异常状态返回-1。make -j4编译sched.cpp及链接成功，证据
.cache/batch11-syscall-prep-2026-11-07/build.log；尚未运行收集或本批P0，不以构建通过证明peek逻辑正确。

下一步沿下文既定wire/syscall任务：共享task_exit_result.h定义24字节固定结果并断言布局；
仅kernel syscall.c和user_api.h包含结果头，避免传入syscall.S的共用syscall.h引入C类型声明。
编号7，a0有符号long目标ID先检查[0,__INT_MAX__]再转int，a1为当前调用者结果VA。
私有syscall_reap依次peek（-1/0直接返回且不碰用户buffer）→逐字段映射wire→当前用户root/copy_to→
复制成功后task_reap→返回1。copy失败保留DEAD结果，复制成功后reap意外失败按内部不变量诊断停机。
case保存参数后调用helper，将返回值写a0并break；sepc由既有入口推进一次。
用户封装user_reap(int,TaskExitResult*)保留a0/a1、设a7=7/ecall/ret；U1原检查后/end前轮询ID2，
0时yield重试，1验证REQUESTED/status0/cause0并输出reaped消息，-1失败退出。
本次仅任务接线说明和文档，未代改产品或新增验收矩阵；完整条件仍以下原任务为准。

## 作者已写ID查找/peek，reap清理复用讨论（2026-11-06）

静态核对：find_task_slot_by_id正确按高水位内非UNUSED槽匹配公开ID。peek已拒null输出/目标/self/KERNEL/nullproc，
但DEAD分支尚缺资源为空及有效reason检查；活分支应明确READY/RUNNING/BLOCKED，剩余状态-1，
末尾重复复制分支在当前查找规则下不可达，应删除。

作者reap草稿已查目标/self并拒非DEAD，尚缺USER/nullproc/有效reason以及实际消费。
建议复用的是私有task_clear_metadata（frame指针、entry、公开ID=-1、kind=KERNEL、sleep=0、proc解绑），
不触碰state/实际栈/全局计数，不调用allocate。调用前必须保证Process已安全reset或本来无绑定。
reap先核对目标/USER/proc/DEAD/有效reason，再调用Process::reset并处理失败；reset自身已检查完整资源为空，
因此无需在同一稳定路径再单独resources_empty扫描。reset成功后clear metadata，state=UNUSED最后，返回0。
allocate则保留同槽Process reset、调用共同metadata助手，再准备context/分发新ID/设READY。
本次只读解释及归档，未代改产品或运行未完成reap的验证；原B11要求不变。

## Process基本接线正常，下一步ID查找与peek（2026-11-06）

作者已删allocate残留的空proc reset、移除change_page旧解引用，修正current_user_task的guard顺序及
当前root getter的索引/条件。make -j4返回成功（目标已最新）；正式16秒QEMU两U均end及REQUESTED0，
末次退出后S timer继续，无user fault/kernel halt。证据.cache/process-b11-next-2026-11-06/。
本次未跑完整B11 P0或宣称初始化/收集功能全部完成；sched_init全槽预检/重置仍为原待办。

下一步在sched.cpp新增私有`static int find_task_slot_by_id(int task_id)`，负ID返回-1，
遍历[0,task_count)匹配非UNUSED且公开ID相等的槽；返回槽索引或-1，不能直接用公开ID下标访问。
随后实现已有声明`task_peek_exit`：先检查内核输出指针及目标、拒绝S/自己/null proc；
READY/RUNNING/BLOCKED返回0且不改输出；DEAD须Process资源为空、reason为REQUESTED/FAULT，
最后才写快照并返回1；其余-1。只读取，不reset、不释放、不置UNUSED。
完成后沿原任务接task_reap与用户复制，既定先copy后消费协议保持，无新增验收要求。

## 后续故障定位：空this调用reset（2026-11-06）

作者已补Process数组/绑定/USER、汇编C链接、lambda初始化、process.o和公开ID日志，随后报告
scause13、sepc0x8020731e、stval0。当前ELF映射到process.cpp:14的resources_empty，
实际指令ld a0,0(a0)。根因为allocate完成真实g_process槽reset后，仍在proc=nullptr之后
调用proc->reset；删除这次残留调用，保留对实际存储的reset。

其他残留：change_page旧的无条件root访问先于新guard；current_user_task的null判断晚于root解引用；
当前root getter误用task_count下标（满槽越界）且proc条件反向。按真实current_task边界检查后
再验证kind/proc/root。USER缺root时应终止该错误路径。sched_init完整预检/重置仍待作者补。
本次仅定位，不声称正式修复或运行通过；地址证据见同一审查目录fault-address.txt。

## Process接线草稿集中检查（2026-11-06）

作者已实现Process基本成员、将sched改为cpp并以proc替换原字段，尚缺存储/绑定。
本次仅审查和编译，正式功能源码未修改。

- 尚无固定Process数组，静态Task.proc默认nullptr；sched_init的idle.proc->reset和allocate中的proc->reset都会解引用空指针，且忽略reset结果。应使用同容量实际Process存储，idle/内核Task保持nullptr。
- sched_init需先全量确认Process资源为空，再重置Process与全部槽元数据，最后提交内核根/计数等初始化；不能先清计数或抹掉仍有资源的描述。
- allocate选槽后先reset对应存储并处理拒绝，再改Task/ID/高水位，proc设nullptr；用户创建成功后按同槽绑定，adopt，设USER；adopt已清source，删除外层重复清空。
- change_page需处理内核/idle无proc，活USER的proc/root缺失属于内部不变量错误；current_user_task和当前用户root getter需在解引用前验证USER kind及proc非空。
- record_exit处无捕获lambda引用reason/status/frame且return缺分号，实际编译6错误；可直接传TaskExitInfo指定成员初始化。
- task_enter需extern C；仅修lambda的隔离副本目标编译通过，nm实际得到未定义_Z10task_enterP10trap_frame，证明当前C++名字与汇编符号不符。
- Makefile OBJS缺kernel/process/process.o；退出日志仍用槽索引current_task，应改为task->task_id。
- Process成员实现目标C++语法检查通过；Process不拥有内核栈，头部禁移动注释应改为本类资源所有权/稳定关联的理由。

证据.cache/process-sched-review-2026-11-06/，含真实构建错误、隔离编译和符号证据。
未运行当前草稿QEMU或整批P0，不以之前拆context的通过覆盖本次未完成状态。

## 目标与可观察结果

B10已做到用户空间释放后保留DEAD任务结果。本批让另一用户非阻塞收集该结果，成功后将原用户槽标为UNUSED；已有内核创建接口随后能分配该槽。用户空间仍在退出时释放，收集不会再次释放7页。

正式演示保留两S、两U和同一ELF，MAX_TASKS仍4。U1完成原三轮计数/时间/数据检查后，收集U0的结果，确认REQUESTED/status0/cause0，再输出一次：

```text
[U1] reaped U0
```

原U0/U1的start/end各一次、counter OK各三次、时间/数据消息各两次保持。U1的reaped消息在自己的end之前。目标尚未退出就yield后重试，不要求全局日志顺序或额外yield的精确次数；本批不再要求yield恰好六次。

作者写核心，协作者集中审查与P0验证。不要求作者另写测试框架，也不增加每轮buffer扫描。

## 一条完整生命周期

```text
UNUSED → READY → RUNNING/BLOCKED
→ DEAD（结果保留、用户空间已空）
→ 成功复制结果给收集者 → 清元数据 → UNUSED
→ 分配给新任务（获得新task_id）
```

DEAD不能直接被分配器复用，否则结果会在收集前丢失。copy_to失败也不能消费结果；完成复制后才提交槽回收。

本批允许收集任意已结束的其他用户任务；没有父子关系或等待权限模型。S任务、自己、无效ID都不能收集。没有阻塞wait、fork/exec、用户创建任务syscall、动态内核栈或COW。正式演示不新增第三个用户；同槽新任务的真实执行由协作者临时探针验证。

## 先区分槽索引与公开任务ID

current_task继续表示tasks数组下标，供当前现场/调度/栈定位使用。task_count保留，但含义明确为“已初始化过的槽前缀长度”，范围[0,MAX_TASKS]，不是当前存活任务数量。

在sched.c私有task中新增：

```c
enum task_kind {
    TASK_KIND_KERNEL,
    TASK_KIND_USER
};

/* struct task的新字段 */
int task_id;
enum task_kind kind;
```

新增私有uint64_t g_next_task_id；sched_init置0。每次成功分配分发一个非负int公开ID并递增，失败不得消耗ID。先检查g_next_task_id是否大于INT_MAX，耗尽则零修改拒绝，不能有符号溢出或绕回旧ID。当前freestanding工具链用编译器`__INT_MAX__`，避免`limits.h`引入不可用的目标libc头。

初始两S和两U仍得到0/1/2/3。回收槽2后，新任务可继续使用tasks[2]，但公开ID应为4，旧ID2必须查不到它。整个内核初始化可重置ID计数，本批不跨内核启动保存ID。

公开的task_create/task_create_user返回task_id。外部目标查找遍历[0,task_count)，匹配非UNUSED槽的task_id，不能写tasks[task_id]。退出日志改用task->task_id；调度、唤醒和当前root getter继续按current_task槽索引工作。

kind需要保留到收集完成：DEAD用户的user_space.root已空，不能再靠它识别用户/内核身份。task_allocate将kind初始设KERNEL，task_create_user在完成创建时设USER。

## 分配与槽回收

task_allocate的流程：

1. 先检查task_out及ID可分发；所有拒绝都在写入前完成。
2. 扫描[0,task_count)找TASK_UNUSED槽。
3. 找不到才判断是否可追加tasks[task_count]；已满则失败。不要先用task_count==MAX_TASKS拒绝，前缀内可能已有空槽。
4. 在选中槽重置原有entry/state/sleep/user_space/exit_info，分配新task_id，准备frame。只有追加新槽时增加task_count。
5. task_create和task_create_user继续完成各自的入口/寄存器/空间接管；返回公开ID。

回收不减少task_count，避免后面的活槽落到调度扫描范围之外；现有按READY/BLOCKED筛选的循环会跳过UNUSED空洞。固定静态栈仍属于槽，可供下一任务使用；不需要清整个4096字节栈，下一次riscv_task_context_init会重新初始化现场。

sched_init仍只在无用户拥有资源的boot阶段使用。初始化全部MAX_TASKS的元数据为UNUSED、id=-1、kind=KERNEL、空space/结果/entry/frame/sleep；idle保留原有独立初始化。不是用sched_init代替资源销毁。

## 内核接口：先看结果，再提交回收

include/task/sched.h声明，保留C链接：

```c
int task_peek_exit(int task_id, TaskExitInfo* result_out);
int task_reap(int task_id);
```

task_peek_exit只向内核指针写快照，不接收用户VA：

| 返回 | 含义与副作用 |
| --- | --- |
| -1 | result_out为空、无效/已收集ID、S任务、自己或不一致状态；不改任务/result_out |
| 0 | 目标为READY/RUNNING/BLOCKED用户；不改任务/result_out |
| 1 | 目标为DEAD用户，reason为REQUESTED或FAULT，完整user_space已空；写出TaskExitInfo快照，但不消费 |

task_reap返回0成功、-1失败。只接受非当前、DEAD、结果有效且完整user_space已空的USER目标，所有校验在修改前完成。成功清该槽的结果、entry/frame/sleep、公开ID（置-1）及其他元数据，最后置UNUSED；不改current_task、task_count或ID分发计数，不再次调用task_destroy/release，不做PMM分配/释放。内核S任务的DEAD槽本批不回收。

两接口可复用私有ID查找/状态检查助手，Task仍留在sched.c，不向syscall模块暴露Task指针。

## 调用上下文：一次syscall内完成提交

本批创建/peek/reap的修改操作只在单hart且SIE关闭的安全上下文调用：boot尚未运行任务，或正在处理当前用户syscall且本次尚未发起重调度。现有boot和新的REAP分支满足该条件，不为普通可抢占S任务新增运行时创建支持。

syscall从peek到copy_to再到reap不能yield、sleep、打开中断或重调度。当前trap入口自动进入SIE关闭的处理阶段；这不是自动获得多hart锁。

尤其不能在sched_reschedule返回后、trap汇编真正切换sp之前回收/复用旧任务槽：current_task可能已指下一任务，但CPU仍在旧内核栈上执行。正式U1实际运行后再发REAP是安全路径；所有临时复用探针也必须挂在该REAP分支内、任何重调度之前，不能挂在U0退出的post-reschedule回调。

## 用户结果格式：固定24字节

新增include/task/task_exit_result.h，供用户API和kernel syscall共同包含；不要把含enum/padding的内部TaskExitInfo直接复制到用户。

```c
#define TASK_RESULT_REQUESTED UINT64_C(1)
#define TASK_RESULT_FAULT     UINT64_C(2)

typedef struct TaskExitResult {
    uint64_t reason;
    int64_t status;
    uint64_t fault_scause;
} TaskExitResult;
```

该头包含stdint.h/stddef.h和include guard，C/C++分别用_Static_assert/static_assert确认sizeof==24以及三个字段偏移0/8/16，不在汇编中包含。字段为固定宽度，不传内核指针。REQUESTED时status保留完整64位、fault_scause=0；FAULT时status=0占位、fault_scause保留原因。显式从内部reason映射到上述稳定常量。

## syscall与用户接口

task/syscall.h新增SYSCALL_REAP=7，其他编号不变。user_api.h包含结果格式头，增加：

```c
long user_reap(int task_id, TaskExitResult* result);
```

user/syscall.S沿用现有封装：a0目标公开ID、a1用户结果VA、a7=SYSCALL_REAP，ecall后ret。内核先把a0按有符号long读取，拒绝负值或大于INT_MAX后再转int，不能先截断再找目标。

内核私有syscall_reap按顺序组织：

1. task_peek_exit取内核局部快照。返回-1/0则原样返回，不检查/写用户buffer，因此活任务加坏地址也返回0。
2. 返回1时，将快照逐字段转换为TaskExitResult，不直接暴露内部结构字节。
3. 获取当前收集者的用户root，经user_copy_to写24字节。失败返回-1，目标仍DEAD且结果存在。
4. 复制完整成功后才task_reap。检查返回值；成功后给用户返回1。在上述单hart/不重调度阶段中，peek认可的同一目标此时仍应可回收；若task_reap意外失败，作为内部不变量错误诊断停机，不能静默报告成功，也不归咎用户buffer。两个内核接口必须认可同一DEAD终态。

既有copy_to在后页失败时可能已写合法前缀。REAP返回-1承诺不消费结果，不承诺用户buffer原子不变；返回0则完全不碰buffer。整个流程不新分配用户页面。

## 用户演示

只在user_tag==1的正常路径中、原检查通过后且end之前收集初始U0的ID2：

```c
TaskExitResult result = {0};
/* 循环user_reap(2, &result)：
 * 0：检查user_yield()==0后重试；
 * 1：核对REQUESTED、status0、fault_scause0，然后结束循环；
 * -1：fail("reap U0 failed\n")。
 */
```

核对后一次user_write输出完整[U1] reaped U0行并检查长度，接着原end和return0。ID2是本演示boot先创建两S的明确策略，不是由任意user_tag通用推导PID的方法。正常源保持0/0退出；fault和宽值仅在协作者副本验证。

## 文件范围与建议顺序

| 文件 | 作者工作 |
| --- | --- |
| kernel/task/sched.c | kind/公开ID、高水位空槽分配、ID查找、peek/reap、日志ID及初始化 |
| include/task/sched.h | 两个内核接口及安全调用上下文说明 |
| include/task/task_exit_result.h | 无指针、无padding的固定结果格式与布局断言 |
| include/task/syscall.h | 编号7 |
| kernel/task/syscall.c | 参数范围、快照→序列化→copy_to→reap的事务顺序 |
| user/user_api.h、user/syscall.S | 用户声明及a0/a1封装 |
| user/main.c | U1非阻塞收集并验证结果、整行消息 |

先完成ID/槽分配和两个内核接口，再接用户格式/syscall，最后演示。保留B10的fault/退出结果与7页释放规则，不重写MM/loader或同时迁移整个调度器语言。

```sh
make -j4
timeout 20s make qemu
```

timeout的124仍只是观察结束。常规观察预期U0退出结果、U1 reaped U0、U1退出结果及后续S tick，U0的结果已复制给U1，内核原槽结果在消费后清空。

## 验收与分工

P0由协作者完成；作者只需实现和普通观察，不新增测试矩阵。P1/P2选做，不阻塞收尾。

| 等级 | 场景与明确预期 |
| --- | --- |
| P0 | 默认完整构建无新增警告/未定义、原82宿主组通过、wire偏移/大小目标断言通过、用户代码仍一页；默认双tag/原时间数据通过，U1成功收集一次、后续S tick正常 |
| P0 | 临时在U0仍活着时调用REAP，返回0且buffer/目标/资源不变；S任务、自己、负/未知ID拒绝，不消费 |
| P0 | U0 DEAD后坏结果地址返回-1且任务结果保持；随后合法地址返回1，复制内容正确、目标变UNUSED；再用旧ID返回-1。确认copy之前不提交，收集不增加alloc/free |
| P0 | 收集成功后的同一REAP trap内、任何重调度前，临时调用task_create创建短S任务：slot2复用但公开ID4、task_count仍4；内部ID查找确实以4找到槽2、以2找不到，不只靠新kind KERNEL不可收集来推断ID正确。新任务真实运行，元数据/frame/结果重新初始化，收集+分配均0 PMM alloc/free |
| P0 | 另两份临时用户场景分别收集FAULT/cause13和REQUESTED/负2^40，wire/产品状态保真。fault不循环；仍各用户退出7页，收集不二次free，shared/幸存者保持，最后14页归还 |
| P1 | 24字节结果跨页且后页无写权限，返回-1并允许合法前缀，结果仍可重试；ID耗尽零修改；不同结束顺序 |
| P2 | 父子权限/阻塞wait、用户动态创建、S任务回收、PID计数回绕、多hart或普通可抢占S创建 |

P0探针都在临时副本，正式源不加入演示工厂或额外任务。不能把旧B10的task_id==slot假设照搬到复用后的检查；旧文档保留历史，测试按本批最小适配。

## 功能梳理与理解收尾

完成后梳理“用户退出释放页→DEAD保留结果→另一用户复制结果→提交槽回收→同槽新ID”。默认三题：

1. 为什么task_count==4仍可能成功创建？task_count、槽索引和task_id各表示什么？
2. 为什么必须先copy_to成功再reap？坏结果地址之后，目标任务及用户buffer分别保证什么？
3. 为什么槽2复用后ID4不能继续用旧ID2访问？为什么SIE0和current_task已变化仍不足以允许任意post-reschedule回收？

## 三题原答、集中审查与修正版（2026-11-07）

作者提交三题合并原答。以下原文保留，修正版由协作者结合真实实现整理；
不代表作者已重新作答，不要求反复复答或增加测试作业。功能、验证和本轮理解审查已集中归档，本批收尾。

### 第一题：高水位、槽与公开ID

作者原答：

> C不对越界数组进行保护，水位应该是task_count,槽位slot_index，公开ID是current_task

集中审查：task_count/slot_index识别正确；current_task仍是当前运行任务的槽索引，
公开ID是Task内的task_id。裸数组无自动越界检查不是count==4仍可创建的原因；
实际代码先在高水位前缀内找UNUSED，只有找不到空洞才追加新槽。

修正版：task_count是已初始化槽前缀长度（高水位），不是活任务数。容量4/highwater4且slot2已UNUSED时，
仍可使用tasks[2]，没有越界，也不再增加高水位。新任务在slot2取得公开ID4；
若U1仍在自己的syscall里，current_task仍为3。调度使用槽索引，外部目标查找匹配task_id。

### 第二题：复制与消费的顺序

作者原答：

> 因为当时我们的result的数据对于reap来说不可见，int target = find_task_slot_by_id(task_id);这里找到的是目标task，我们要从这里才能转入获得得result，但是对于其他文件来说，此时的task它获取不到，无法直接传入，而是需要先进入目标地址再交给读取

```cpp
    if (target_task->kind != TASK_KIND_USER ||
        target_task->proc == nullptr ||
        target_task->state != TASK_DEAD) {
        return -1;
    }
```

> 并使用到reap之后的过程

集中审查：查找/guard确实确定是否允许收集，但不能解释为何copy必须先于reap。
find_task_slot_by_id只是查内核数组，不切入目标用户地址空间；目标退出时该用户空间已释放。
peek读取内核Process保留的exit_info，copy_to写当前收集者的用户空间。

修正版：先reap会清结果并允许槽复用，如果随后用户地址无效、copy失败，收集者得不到完整结果，
也无法用原ID重试。先peek取快照、copy成功后reap，才能保证复制失败不消费目标。
返回0完全不碰用户buffer；copy失败返回-1时目标仍DEAD且结果保留，但buffer可能已写入合法前缀，
不承诺buffer原子不变。reap清的是结果/元数据并置UNUSED，用户物理页此前已在退出时释放，不再free一次。

### 第三题：旧ID与真正切栈的时间点

作者原答：

> 原来的ID2名字被抛弃了，不然会干扰接下来的task事件循环与切换

集中审查：认识到旧身份需要失效，但调度循环使用槽/state，不依靠公开ID轮转。
更准确的目的，是防止持有旧ID的请求误操作占用同槽的新任务。原答尚未覆盖切栈时间点。

修正版：slot2是存储位置，新ID4代表另一任务；旧ID2不能重新指向它。
current_task=next只更新内核记录，sched_reschedule返回的也是下一个frame指针；
此时CPU仍可能在旧内核栈执行C调用链。trap_handler返回后，汇编执行mv sp,a0才切到新frame。
SIE0只阻止中断，不停止当前代码用栈，因此不能仅凭关闭中断/更新索引就复用旧槽并重建现场，
否则可能覆盖仍在使用的栈内容。当前允许在已实际运行的U1进入自己的REAP trap、且该次尚未reschedule时，
回收已结束的U0；此时U1执行链不占用U0栈。

## 准备记录

2026-11-05：作者在B10收尾、询问task复用后要求下一步。已核对真实task_count/分配/调度/退出/用户复制与syscall接口，独立设计审查确认高水位空洞可兼容现有调度筛选，补齐公开ID、持久kind、固定wire及真实内核栈调用边界。仅交付完整任务，没有代写正式产品或声称本批通过。B10仍是当前已完成基线。

接口准备仅在.cache/batch11-task-preparation-wvf39a6k/构造临时共享头：实际RV64 GCC C编译和Clang C++语法检查均验证TaskExitResult大小24/偏移0/8/16及user_reap声明，无诊断；61正式源配置哈希保持。证据results.json/task_exit_result.h/probe.c/probe.cpp/probe.s。未实现新syscall/槽分配或运行本批QEMU，不以布局检查替代功能验收；未clean/stage/commit/push。
