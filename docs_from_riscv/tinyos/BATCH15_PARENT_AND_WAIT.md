# BATCH15：父子关系、等待收集与孤儿收养

状态：2026-11-11功能与正常主线已接通，DeepSeek首轮私有助手重构已审查应用。
构建、创建27/收集37个局部断言和QEMU正常主线通过；完整B15的孤儿转交、真实拒绝/重试及页面统计P0仍待协作者收尾，不标记整批完成。
本地提交前再次make -j4、make test（CTest宿主测试入口）及git diff --check全部通过。

导航：[批次索引](README.md) · [上一批](BATCH14_BUILTIN_EXEC.md) · [总路线图](../TINYOS_EVOLUTION.md)

2026-11-11当前复验：make -j4成功（无需重编），6秒QEMU实际打印`User task creation failed.`，未进入调度。
configure全体预检、boot创建循环后登记、scheduler_start登记保护已补；仍需修原任务四处接线：
task_create_user在idle=-1时取tasks[-1]，缺boot/运行期分支、SIE0及运行期收养者验证；
syscall_waitpid需允许请求-1；WAITPID case需调用syscall_waitpid并写回frame->a0；
main首轮等待U0被整行注释，应恢复`(void)wait_requested(0);`。
证据`.cache/batch15-current-20261111/{build,qemu}.log`；尚未执行完整B15验收，不扩展既定要求。

2026-11-11随后局部修正：作者修好WAITPID参数/分发和main首轮等待；协作者按授权抽出
`supervisor_interrupts_disabled`、`is_boot_task_context`、`user_creation_parent_id`，
创建与登记复用检查，boot不再访问tasks[-1]，运行期在分配前确认当前父和根收养者。
原参数/空间检查、分配与发布保持。目标重建无诊断，27个实际助手提取断言通过（仅CSR读取模拟），
22秒QEMU跑完ID2/4/5/3退出0/23/37/0、fork isolation、U1 end及后续至少两次S timer。
证据`.cache/batch15-bootstrap-20261111/`；仅主线运行与本次助手验证，不代表完整B15所有P0完成。

## 目标与本阶段安排

作者确定先完成当前进程管理阶段，再拆sched。顺序为：本批父子生命周期 → 不同内置镜像选择与EXEC
→ 本阶段梳理收尾 → 提取TaskTable/用户任务操作 → 文件系统。后续镜像选择的接口到下一批单独确定。
这不是要求逐项复刻rCore整章；阻塞wait队列、COW、多线程、动态内核栈和VFS不并入本批。

本批让“知道一个ID就能收集”变为“当前父进程才能收集自己的孩子”，并能等待任意孩子。
父退出时，活孩子和DEAD但未收集的孩子都交给指定收养者；退出父自己的结果仍留给它的父收集。
不需要父子链表：Process只保存parent_id，四/五槽表扫描推导孩子关系。

保留当前两S任务、boot U0=2/U1=3、SPAWN U2=4、fork子=5→EXEC tag3→退出37的主线。
本批将MAX_TASKS从4改为5：两S＋收养者＋中间父＋孙进程恰需五槽。
只增加一份静态槽存储，不引入动态表；普通主线仍只用四个槽，ID与原35次页面释放统计保持。

## 最小关系模型与不变量

- task_id仍只存在Task，是公开身份；current_task仍为槽索引。Process不增加第二套PID。
- Process增加parent_id_，没有父进程使用-1；0是合法ID数值，不能用作“无父”哨兵。
- 指定U1/ID3为本轮用户收养者，根parent=-1；U0/ID2在启动登记时成为U1的孩子。
- 运行期SPAWN/FORK的新孩子属于实际调用者，填写调用者自己的task_id，不能复制调用者的parent_id。
- EXEC只替换空间和用户frame，保留parent_id；释放用户空间也保留父关系和退出结果。
- parent_id不拥有父对象；不保存跨槽复用的Process*或Task*关系，不引入children数组/双向链表。
- root之外的已发布用户任务都连接到这棵树；收养者parent固定-1，不能被自己或其他进程收集。
- 关系改变在单hart、SIE关闭、无调度的boot/trap安全阶段完成。

例如根A→B→C中B退出：C.parent改A，B自己的parent仍为A，B成为DEAD等待A收集。
不交换A/B/C的ID、槽或Process对象；C即使已经DEAD也要转交。

本批采用直接交给指定收养者的简单策略；不实现任意祖父接管或subreaper标记。
收养者只在没有任何直接孩子时允许沿现有路径终止；DEAD未收集孩子也算孩子。
有孩子时收养者exit/fault是本轮init约束被破坏：打印原因并停机，不强行销毁整棵树。
没有孩子时根可结束，自己的结果留在DEAD槽，和此前最终U1无人收集的终态一致。

## 文件与职责

| 文件 | 本批改动 |
| --- | --- |
| include/process/process.hpp、kernel/process/process.cpp | parent字段、访问与重置语义 |
| include/task/sched.h、kernel/task/sched.cpp | 五槽、启动登记、创建关系、孤儿转交、限父peek/reap |
| kernel/boot/private/boot_stages.cpp | 保存两用户实际ID，创建完后登记U1为收养者 |
| include/task/syscall.h、kernel/task/syscall.c | WAITPID11，新旧收集共用copy后消费逻辑 |
| user/user_api.h、user/syscall.S、user/main.c | 新包装、wait-any演示和局部代码复用 |

SPAWN仍经原task_create_user发布，镜像准备/MM/arch接口保持；本批不拆sched、不新建任务表管理器。

## Process：只保存自己的父关系

在tinyos命名空间定义`inline constexpr int kNoParentId = -1;`，增加：

```cpp
int parent_id() const noexcept;
void set_parent_id(int parent_id) noexcept;
```

私有字段：`int parent_id_{kNoParentId};`。

setter只接受可信管理代码传入的已验证关系，本类不知道自己的task_id和全表，不在getter/setter里遍历任务。
合法父对象、禁止自环/根收养者作孩子等关系由sched管理入口维护；用户不能直接指定任意新parent。

| 方法 | 父关系的处理 |
| --- | --- |
| 默认构造 | parent=-1，空间/结果仍按原规则初始化 |
| resources_empty | 仍只检查空间资源；不要因parent非-1或有exit_info而判资源未空 |
| release_resources / record_exit | 保留parent |
| clone_address_space | 仍只复制空间，不创建关系 |
| adopt | 仍只接管空间；不要清parent，否则EXEC会断开关系 |
| reset | 资源确实为空后，同时清空间、结果和parent=-1；失败对象不变 |

不要给Process增加调度状态、任务查找、自动资源析构或孩子集合。

2026-11-10实现问答：这里的adopt接管UserAddressSpace，不是收养子进程。
user_space_与parent_id_是两个独立成员；只赋值/清空user_space_不会改变parent_id_，
source={}清的也是UserAddressSpace临时描述，不是整个Process。
构造时默认parent=-1；SPAWN/FORK发布孩子时由管理入口验证后调用set_parent_id，
不要求给release/adopt传父ID，也不在每次空间操作时重设父关系。
检查新父合法、非自身、不会形成后代回环属于创建/重新指定父关系的入口；
不能把新父验证与只读/释放地址空间混在一起。本批只改挂到parent=-1的指定根，
核对该根存活且不同于退出者即可维护这一受限策略，不要求现在实现任意进程树改挂算法。
静态核对作者当前parent getter/setter/reset均已写出，release/adopt保持未操作parent，符合此处语义；
尚未完成关系发布/收集实现或运行B15验证。

## boot登记与运行期创建

在sched.h增加C接口：

```c
int sched_configure_user_reaper(int task_id);
```

sched内部增加`g_user_reaper_id`，初始-1，成功sched_init最后重置为-1。
sched_init原资源预检失败时，关系与收养者配置也必须保持；不要提前清parent或配置。

启动登记是一次性的boot操作：要求SIE关闭、current仍为idle、收养者未配置，
所有非UNUSED任务都尚为READY；目标为已创建的合法USER/Process，所有bootstrap USER的parent仍为-1。
先完成全体预检，再统一设置目标parent=-1、其他bootstrap USER的parent=目标ID，最后才提交g_user_reaper_id。
非法目标或重复登记返回-1，零分配且零关系修改；不允许设置一半再失败。

boot保留原创建顺序，将每次返回的user_task_id写入局部user_task_ids数组。
两用户都发布成功后调用`sched_configure_user_reaper(user_task_ids[1])`并处理返回值，
失败沿boot_fail诊断停机。不能把spaces下标或user_tag当作公开ID。
scheduler_start在存在USER时须确认已成功登记收养者，避免无父任务直接进入运行。

task_create_user保持当前四参数接口：

- 收养者尚未登记时，只允许原boot创建阶段（idle/current、SIE0、尚无RUNNING任务）发布parent=-1的bootstrap USER。
- 登记后，仅允许合法当前RUNNING USER作为创建者；S/idle不能再走bootstrap分支发布无父USER。
- 确认运行期收养者仍为合法存活USER。先取得父ID、完成原启动检查，再task_allocate。
- 分配成功后在原无失败发布阶段adopt空间并设孩子parent=父公开ID；失败不改变源space或消耗ID。

fork已有current_user_task(frame)，把它自己的task_id传给私有task_create_user_from_frame的新parent_id参数。
该助手在分配成功后设置孩子parent；不要从snapshot寄存器、user_tag或父parent_id推断身份。
不复制Process对象，不更改子a0=0/父a0=新ID与原sepc规则。

## 退出时转交孩子，收集时仅回收记录

2026-11-10后续复查：作者已补reparent_children及普通退出分支的收养者验证/转交，
g_user_reaper_id已改int。finish_user_task中的B15顺序已具备：当前USER/SIE0→根无孩子保护或普通父转交孩子
→原退出记录/诊断→task_destroy→reschedule，不需要再清当前parent或重置收养者配置。
make kernel/task/sched.o通过且无诊断；这是局部静态/编译结论，尚未执行完整关系与停机路径运行验证。
本函数可暂告一段落；原任务剩余包括task_create_user运行期父身份与发布、登记一次性等预检、限父peek/reap及WAITPID。

以下为此前局部补充的历史记录，旧警告与else误调用已由上述更新替代。

2026-11-10作者请求局部补充孩子查询助手。协作者沿用作者的`_is_have_child(int task_id)`草稿，
扫描高水位内非UNUSED、USER且Process非空的记录，父公开ID匹配即true，遍历结束false；
DEAD但未收集的孩子同样计入。收养者退出分支调用该助手，有孩子时打印原因并wfi停机，
发生在退出结果记录/标DEAD/空间释放之前。没有代写普通父的孤儿转交流程。
目标sched.o编译通过，当前g_user_reaper_id仍为uint64_t，既有ID比较有一条signed/unsigned警告。
临时宿主探针直接提取实际助手并使用真实Process实现，9个场景通过：空表/高水位/活孩子/空资源DEAD孩子/
UNUSED/内核任务/空Process/其他父/后部BLOCKED孩子。证据`.cache/batch15-reaper-guard-20261110/`。
未实际运行停机路径或完整B15验收；普通退出else仍误调用boot登记函数，应由作者接着实现孩子转交。

增加私有表扫描助手即可，仍放sched.cpp；不需要链表或为每个Process再放管理器。

在finish_user_task的既有exit/fault公共路径中：

1. 验证当前USER、SIE0与关系前提。若是收养者，扫描直接孩子；包含DEAD孩子时也诊断停机。
2. 普通父若有孩子，先验证指定收养者存活且不是自己；失效是内部关系错误，诊断停机。
3. 扫描非UNUSED USER，将所有parent==当前task_id的孩子改挂收养者，包含DEAD未reap者。
   预检完成后此循环只有可信字段赋值，不分配、释放、copy用户页或调度。
4. 当前任务自己的parent保持；继续原结果记录→标DEAD/切kernel根/释放旧空间→日志→reschedule。

收养发生在父exit/fault，而不是等父被reap；这样父的DEAD槽一旦被回收，孩子已不再引用旧身份。
转交操作自身零PMM；父退出仍只释放父自己的7页。孩子的页表、数据、frame、状态及旧退出结果保持。
收养者结束后g_user_reaper_id保留原ID，不重新开启bootstrap，也不允许再次登记；安全sched_init才重置。

## peek与reap：只查看当前父的孩子

2026-11-10作者明确将集中复查第4项交给协作者实现。本次已补：
私有current_user_task_metadata统一当前槽/状态/USER/Process/私有root检查，
带frame版本在此基础上检查SPP；收集入口current_user_task_for_collection再读实时sstatus确认SIE0，
不伪造frame，也不改变公开peek/reap签名。
peek支持具体ID与-1任意，按槽顺序扫描直属孩子，跳过活孩子继续寻找DEAD；
只有合法REQUESTED/FAULT且资源已空才同时写info/实际ID，其余返回保持输出。
reap重新检查当前父、SIE0及直接父关系，继续沿reset→清元数据→UNUSED提交，不分配/释放物理页。
本次未接手其余boot/创建/WAITPID实现；目标sched.o已编译通过，无诊断。
独立宿主程序直接提取当前实际函数并链接真实Process.cpp，仅将实时CSR读取替换成可控sstatus，
37个短断言全通过：任意查询跳过活孩子/按最小槽选择、父权限、双输出保持、有效退出原因、
资源未空拒绝、peek后变更父关系时reap拒绝、成功reset及重复收集等；无PMM调用，
高水位/下一ID/静态栈与父对象保持。证据`.cache/batch15-collection-20261110/results.json`，
目标构建日志`.cache/batch15-sched-review-20261110/collection-build.log`。
这是局部语义验证，不代表真实CSR/trap运行或完整B15/WAITPID已验收。

调整现有C接口为：

```c
int task_peek_exit(
    int requested_id,
    TaskExitInfo* result_out,
    int* matched_id_out
);
int task_reap(int task_id);
```

peek只读，不消费。requested_id>=0指定孩子，-1表示任意直接孩子，其他负数拒绝。
检查两个输出指针与合法当前RUNNING USER；无需为此暴露可变Task表。
可将现有当前USER助手的元数据检查提为私有公共部分，带frame版本再检查SPP；不要伪造一个frame调用它。

| peek返回 | 含义与输出 |
| --- | --- |
| -1 | 参数非法、没有匹配孩子或内部结果不一致；两个输出保持 |
| 0 | 有匹配孩子但尚无可收集结果；两个输出保持 |
| 1 | 匹配DEAD、资源已空、退出reason有效的孩子；同时写内部结果和实际公开ID |

任意孩子模式扫描到活孩子不能立即返回0：后面可能有DEAD孩子；若多个DEAD匹配，选槽索引最小者。
内核任务、UNUSED、自己和非孩子不得成为候选。父关系用公开ID比较，不用槽索引或Process地址比较。
对于匹配DEAD孩子，资源/有效reason检查继续保留，不把无效记录当成“还活着”。

task_reap只接受具体ID，重新验证当前父身份及目标直接parent、USER/DEAD/有效结果/空间为空等原条件。
成功reset Process（此时清parent），清Task元数据，最后UNUSED；不再次释放页、不降低高水位或下一ID。
不要只在peek检查权限，否则其他内核调用者可绕过检查消费结果。

## WAITPID11与原REAP兼容

新增`SYSCALL_WAITPID 11`及普通用户包装：

```c
long user_waitpid(long requested_id, TaskExitResult* result);
```

a0是有符号请求ID，a1是用户结果地址；汇编仅li a7/ecall/ret。
保留TaskExitResult原24字节ABI、字段映射和断言，不把内部TaskExitInfo直接copy给用户。

| WAITPID返回 | 含义 |
| --- | --- |
| >=0 | 成功复制并消费一个孩子结果；返回实际公开ID，ID0也属于成功 |
| -2 | 存在匹配孩子，但尚无退出结果；用户可yield后重试 |
| -1 | 参数/权限/无匹配孩子/结果复制失败 |

共享私有收集服务执行顺序：

```text
验证原始long范围[-1, INT_MAX]后再窄化
→ peek(requested_id, &info, &matched_id)
→ -1直接错误，0映射-2（此时不访问用户输出）
→ 将info逐字段映射到局部24字节wire结果
→ user_copy_to(当前父root, result_address, ...)
→ 复制成功才task_reap(matched_id)
→ 返回matched_id
```

等待任意孩子时消费的是matched_id，绝不能调用task_reap(-1)。
普通copy失败不消费，用户可改用合法地址重试；跨页copy允许已有合法前缀，不能承诺所有复制失败都零写入。
peek到commit之间保持SIE0、不yield；若copy成功后reap意外失败，沿原规则诊断停机，不能假装事务成功。

原SYSCALL_REAP7/user_reap保持只接受具体非负ID和-1/0/1返回协议，但也限父。
可在原syscall_reap先检查具体ID范围，再调用共享服务，将实际ID>=0映射1、-2映射0、错误映射-1，
不复制两套wire/copy/消费逻辑。内核peek输出初始化为局部对象，不传空指针或用户指针。

内核WAITPID立即返回，不把父设BLOCKED，不增加等待队列或退出唤醒链。
用户等待循环遇-2调用并检查user_yield，成功返回实际ID。REAP的0和WAITPID的-2不能混用。

## 正式用户演示与代码预算

将reap_requested改为共用等待助手，例如`long wait_requested(long expected_status)`：
调用user_waitpid(-1,&result)，-2时yield并重试，其他负值报错；成功检查REQUESTED/status/cause并返回实际ID。

- U1先wait_requested(0)收U0，保留reaped U0日志；此刻只有U0一个直接孩子，实际应为2。
- SPAWN后wait_requested(23)，返回值必须等于实际spawned_id；保留fresh/父数据检查/reaped U2。
- fork子原快照/修改/yield/exec(3)保持，父wait_requested(37)的返回值必须等于forked_id，保留isolation与end。
- U1全部孩子已消费后才返回0；不能仅因孩子的资源已释放就认为根可以退出。

保留counter/time/data/WRITE边界与原用户输出，不在正式镜像塞孤儿多代额外分支；孤儿由协作者隔离P0验证。
用户侧可作两处局部复用：删除main开头旧重复BSS零检查（保留完整检查并填充的循环）；
tag2/tag3选择各自字符串和长度，共用write检查、yield检查和最终返回23/37。

准备试验使用原-O0/一页代码断言：仅换等待助手和包装RX0xfba=4026；
删重复BSS后0xf82=3970；再合并fresh-worker后0xefa=3834，RW memsz始终0x1038。
最终原型只是预算参考，不要求逐行照抄或增加write helper；不能删功能检查、扩代码页或改全局优化等级来绕预算。

2026-11-10作者接入等待助手后，实际链接map中RX为4098字节，超一页2字节。
在`.cache/batch15-user-size-20261110/`仅删除main开头重复BSS检查的副本，沿用原-O0/链接脚本与包装，
RX降至4034字节并成功链接；后面的完整零值检查及填充循环保持。正式源码未代改，未运行QEMU。
副本编译还报告first_child未使用；若无后续用途，首轮等待直接写`(void)wait_requested(0);`即可。

## 实施顺序与验证分工

### 2026-11-10集中复查（历史问题，以下接线项现已修正）

作者请求检查sched.cpp，两个协作者独立只读审查并核对实际源码，集中发现以下问题；
不是新增验收条件。本轮不代改核心，不执行已知无法启动的QEMU，也不把旧kernel.elf当本版验证。

1. task_create_user无条件拒绝current_task<0，boot尚为idle(-1)，首次用户创建必失败。
   原任务的未登记bootstrap/已登记runtime两分支尚未分开；bootstrap核对SIE0/idle/无RUNNING后父=-1，
   runtime验证当前USER与存活收养者再取父ID，原失败不分配/转移的语义保持。
2. boot把sched_configure_user_reaper(user_task_ids[1])写在创建循环内；第一次第二项还为0，目标变成内核task0。
   保存ID仍在循环内，登记调用必须移到两个用户全部创建成功之后。
3. peek写成matched_id_out!=nullptr时拒绝，判断反了，应为==nullptr；输出ID赋值本身已正确。
   requested_id=-1仍交给具体ID查找，故没有wait-any；需扫描当前父的直接孩子，活者不能阻止后面DEAD匹配，
   成功返回实际ID，非1保持两个输出，并只接受有效REQUESTED/FAULT结果。
4. peek/reap都缺合法当前USER与目标parent==当前公开ID的验证；只有非自身检查不能限制收集权限。
5. configure缺未配置/currentidle检查及全体USER非空Process、parent=-1预检；不能在提交循环边写边发现错误，
   要保持非法/重复登记零修改。scheduler_start还缺“有USER必须已登记”的启动检查。
6. MAX_TASKS仍4，未按本批约定增至5；孤儿三层用户关系连同两S共需5槽。
7. include/task/sched.h/实现已将peek改3参，syscall_reap仍2参，make报too few arguments。
   后续新旧收集共用copy后消费流程，必须传局部matched_id并在成功后消费实际匹配ID。

finish_user_task、reparent_children与fork父ID传递的当前方向可保留，本轮不追加其中的设置。
建议修正顺序：创建/boot登记 → 登记预检和五槽/启动检查 → peek/reap → syscall/WAITPID接线。
证据`.cache/batch15-sched-review-20261110/build.log`及review-results.json；未改产品/未提交推送。

1. 写Process的parent访问、默认值和reset语义；这是本批起点。
2. 改五槽、boot登记与创建/发布关系，boot按实际返回ID登记U1。
3. 在exit/fault公共路径转交孩子，保护根退出约束，EXEC保持关系。
4. 修改peek/reap，接WAITPID11与原REAP共享服务，完成用户包装和wait-any演示。
5. 整批完成后集中检查；协作者承担全部P0，P1/P2是选做，不要求作者写探针框架。

```sh
make -j4
make test CTEST='ctest --verbose'
timeout 25s make qemu
```

不执行make clean，不自动提交推送。timeout结束是外部停止观察，不是客体关机。

| 等级 | 本批范围、数字和学习点 |
| --- | --- |
| P0 | 构建无新增诊断/nm-u空、原82宿主保持、用户RX≤4096；原双U/SPAWN/fork/exec37/父隔离保持；只增加一份静态Task容量 |
| P0 | boot登记后root3.parent=-1/U0的parent=3；非法/重复登记零修改，启动不接受未登记USER；创建失败不提交父关系或消耗ID |
| P0 | 实际SPAWN4/FORK5的parent为3，fork继承父自己的ID而非父parent=-1；EXEC前后仍parent3、同ID5/原frame与内核栈；根收齐孩子后正常退出 |
| P0 | 指定/任意等待的无孩子、仍活着、DEAD三态；任意扫描跳过非孩子与活孩子、选最小槽DEAD；peek非1两个输出不变；WAITPID成功实际ID而REAP仍成功1 |
| P0 | 一次真实非父收集拒绝，目标仍可由父收集；一次坏用户结果地址不消费，合法地址重试成功；重收拒绝，copy后消费保持 |
| P0 | 隔离五槽正常运行中分别覆盖活孤儿/已DEAD未收集孤儿：父exit或fault时转交root，父自身结果仍属root；收养零PMM且孩子现场/空间/结果保持，根可按新关系收集 |
| P0 | 正式链仍五份空间各7页、总35合法free，新alloc21、最终净恢复boot两用户14页；三次收集零PMM；结束后至少两次S timer |
| P1 | 更多创建失败点/宽值/多孩子排序、根带孩子退出的受控停机、关系损坏诊断等；理解不变量，可讨论思路，不阻塞下一批 |
| P2 | 祖先/subreaper策略、显式进程枚举/getpid/getppid、阻塞wait队列、多hart与完整历史矩阵；均非本批前置 |

本批不改loader与fork复制算法，因此不重跑全部历史故障矩阵或要求重造框架。
孤儿P0在独立副本调整演示，不能把5槽运行说成原4槽也容得下；不会改正式用户程序来塞测试。
根带孩子退出及内部清理失败的诊断停机路径须静态审查，更多受控异常试验留P1。

## 原三题（功能完成后集中收尾）

1. 为什么fork孩子的parent应是父自己的公开ID，而不是父的parent_id或所在槽号？EXEC和reset分别怎样处理这个字段？
2. wait-any请求是-1时，为什么必须先得到matched_id、copy成功后才消费它？如果匹配孩子还活着或copy失败，分别保留什么？
3. A→B→C中B退出时，C已DEAD和仍活着各要怎样处理？为什么不能等B被reap再收养，为什么B自己的结果也不能一起丢掉？

原答、集中审查和协作者修正版继续保存在本文件，不以重复作答或增加测试作为收尾门槛。

## 准备证据与限制

`.cache/batch15-preparation-2026-11-09/api/results.json`：拟定Process/parent/reaper/peek头接口按目标C11/C++20语法检查无诊断。
`user-budget/results.json`及各版本源码/构建/布局日志：WAITPID用户原型三种预算均可链接，一页断言保留。
70项正式源码配置哈希保持；本轮仅交付文档与临时准备试验，内核关系、WAITPID与新收养行为尚未实现或运行。
两个内部协作者分别审查关系/事务边界和用户预算；没有代写正式核心或把原型运行等同于新功能通过。

## DeepSeek网页版局部拆解交接提示词（2026-11-11）

首轮转交补丁已审查并应用：创建/退出共用find_valid_user_reaper_slot，
collection/fork/exec/finish复用supervisor_interrupts_disabled；各入口返回/停机处理及原事务顺序不变。
目标重编链接无诊断，创建27/收集37个实际函数提取断言通过（CSR读模拟、链接真实Process），
25秒QEMU主线退出ID2/4/5/3状态0/23/37/0、fork隔离、U1 end及后续S timer通过。
证据`.cache/batch15-deepseek-refactor-20261111/results.json`；未扩展跨文件拆分，未提交推送。

将以下提示词及当前sched.cpp、sched.h、Process头/实现、CODE_STYLE.md一起交给DeepSeek；
网页版没有本地工具时只输出建议补丁，不声称执行了构建或测试。仓库规则以作者明确授权的本轮范围为准。

```text
你参与一个RV64、单hart、freestanding C++20教学内核TinyOS的局部重构。
请先阅读我附上的当前源码和规范，以实际代码为准。本轮只负责sched.cpp内部的小范围职责拆解，
输出可审查的统一diff，由Codex检查并在本地应用。不要直接重写整个调度器。

当前已有三个助手：supervisor_interrupts_disabled、is_boot_task_context、user_creation_parent_id。
本次目标：检查这些职责划分，并将确实重复的SIE读取、当前用户元数据检查、合法收养者查询
整理为少量私有助手。保留调用点不同的错误处理（返回错误、返回frame、诊断停机）。
只有前置条件完全相同的检查才合并，不能把原先没有的验证悄悄扩到其他入口。
当前boot/SPAWN/FORK/EXEC/等待主线已在QEMU通过；行为保持是本轮约束。

必须保持：
1. current_task为槽索引，task_id为公开ID，task_count为高水位；idle=-1不能作数组下标。
2. boot创建与运行期创建分开，SIE关闭；关系和资源预检先于allocate，失败不消耗ID或转移空间。
3. Process仅拥有自身地址空间、退出记录和父ID；adopt/release不改变父ID，reset才清关系。
4. fork父/子a0、新frame/栈、sepc规则保持；exec保留Task身份及父关系。
5. peek只读，wait-any跳过活孩子，错误/待定不改输出；reap再次核对当前父。
6. 先复制用户结果再消费；切根/释放/接管顺序与提交后停机规则不改。
7. 固定收养者、退出时转交活和DEAD孩子的策略不改。
8. 不引入堆、链表、异常、RTTI、动态任务池；不改变ABI、用户镜像布局或优化等级。

完整TaskTable/用户任务操作跨文件拆分已约定等本阶段完成后再做，本轮不要提前实施。
请先用几句话说明准备合并的职责，再给最小diff、行为保持理由、需验证的现有场景。
无法运行构建/测试时明确说明；若发现其他bug，另列出来，不混进重构补丁。
```
