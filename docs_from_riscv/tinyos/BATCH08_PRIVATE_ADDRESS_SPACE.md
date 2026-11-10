# BATCH08：独立用户根页表与显式地址空间生命周期

状态：功能、全部既定P0及三题原答/集中审查/修正版归档完成；P1/P2选做，不阻塞。任务日期：2026-10-30，最近验证：2026-11-03，理解收尾：2026-11-04。

导航：[批次索引](README.md) · [上一批](BATCH07_COPY_TO_USER.md) · [下一批](BATCH09_TWO_USER_TASKS.md) · [总路线图](../TINYOS_EVOLUTION.md)

2026-11-01：[C++最小静态对象运行时](../CPP_STANDARD_LIBRARY.md)已完成实际验证和机制解释，作者确认继续BATCH08。随后已进入实现，进度与结果见下方修正记录；完整任务范围与整批验收保持。

## 我们处于rCore什么位置

TinyOS当前具备ELF装载、Sv39、抢占调度、用户内存双向复制和显式回收，但用户任务仍与内核共享一个root，只有一个用户演示、静态任务槽。按能力看接近第四章后段，尚不能认为已具备第五章进程机制。

rCore[第四章](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter4/5kernel-app-spaces.html)已讨论应用独立地址空间与ELF；[第五章](https://rcore-os.cn/rCore-Tutorial-Book-v3/chapter5/0intro.html)进入进程创建/替换、父子关系与退出等待等机制。两项目进度分别记录：独立rCore学习线已按约定完成第四章，本批补齐TinyOS自己的地址空间边界，不自动恢复rCore第五章。

本批只推进一个概念：**用户有自己的root，内核映射仍可达；任务切换时选择root，结束时安全销毁私有资源。** 保留三个静态任务槽、S任务A/B和一个用户演示，不同时增加fork/exec/wait、堆、ASID、双用户程序或调度器C++重写。

## 为什么现在需要独立root

当前所有用户VA都放进kernel_root。换一个用户镜像想使用相同VA时，会与原映射冲突；U位也不能区分同一root中的两个用户任务。下一步需要先让不同地址空间可以对同一VA作不同解释。

本批的真实闭环是：

```text
kernel_root作为永久内核模板
    → 新建一张私有root，借用内核分支
    → 原ELF装进私有用户分支
    → 调度选U任务时启用私有root，选S任务/idle时启用kernel_root
    → syscall在用户root下通过共同内核映射运行
    → exit/fault先启用kernel_root，再释放用户内容、私有中间表和私有root
```

作者实现核心；协作者负责解释、集中审查和必要P0。正式用户程序继续BATCH07的两次普通采样与原输出，不增加buffer扫描或边界探针作业。

## 已核对的真实起点

- `UserMemory`拥有内容页、借用root；release成功会将整个描述清空。因此私有root的拥有指针不能只存在其中。
- boot先建立并启用kernel_root，再装载ELF和创建三个任务。现有内核恒等映射集中在根槽0（UART）与2（内核、内核栈、PMM可分配RAM）；用户默认VA集中在根槽1。
- `sv39_activate()`已检查root并执行切换前后全局sfence.vma，使用ASID=0；不需要另写一套CSR操作。
- trap入口当前不切satp，用户trap先经sscratch切内核栈，再调用C；所有这些指令和内存必须在用户root下仍可达。
- scheduler_start和sched_reschedule目前只选择frame，不切root；sched_on_exit直接release，私有root接入后必须调整顺序。
- 当前75个宿主运行组和正式/临时QEMU已通过，详细证据见BATCH07。本次准备任务不重新执行这些验证。

## 固定范围：借用内核分支、私有用户分支

本批先采用受限布局，根槽号是Sv39的VPN[2]：

| 根槽 | 范围与用途 | 私有root中的关系 |
| --- | --- | --- |
| 0 | 当前UART所在1GiB窗口 | 浅复制模板非叶PTE，下级表借用 |
| 1 | `[0x40000000, 0x80000000)`，用户代码/栈/数据 | 初始为0；用户内容和下级表独占 |
| 2 | 当前内核、内核栈、PMM的恒等映射窗口 | 浅复制模板非叶PTE，下级表借用 |
| 其余 | 本批未支持 | 必须为0 |

模板的槽0/2必须是有效非叶项，沿用当前构表产生的仅V标志；槽1及其他槽必须为0。不满足就创建失败、零分配、输出不变。模板及其下级表来自受信的boot构表，所有内核叶映射均无U；本批不新增扫描任意未知页表树的通用验证器。

新root只复制槽0/2，不能整表复制后忘记原用户分支，也不深复制所有内核下级表。模板root和共享下级表一直存活；私有空间存活期间不修改这些共享映射，不从私有空间的释放路径回收它们。内核代码/数据/栈/PMM/UART在两个root中的VA、PA和权限相同，所以C调度函数切表后仍能继续执行，不需要改trap汇编或加跳板。

本批loader明确只支持用户槽1。代码、栈和完整数据区的首/末映射字节均须在槽1，先完成原checked范围、容量与重叠校验，再在第一次内容页分配前拒绝落槽0/2、其他槽或跨槽的布局。栈顶是one-past，验证栈最后字节/SP-1，不把边界栈顶误判为栈外页面。可在新头中用 `USER_ADDRESS_SPACE_ROOT_INDEX` 表达1，避免loader与归属层各写不同策略。

此限制也适用于当前公开loader入口，是本批明确的用户布局收窄。原UserMemory宿主fixture仍可构造其他测试VA，因为它们不经过loader；不为新限制重写原75个场景。

## 拥有对象与公开接口

新增 `include/mm/user_address_space.h`，保留C兼容布局和条件式C链接保护：

```c
typedef struct UserAddressSpace {
    Sv39PageTable* root;
    const Sv39PageTable* kernel_root;
    UserMemory memory;
} UserAddressSpace;

int user_address_space_create(
    const Sv39PageTable* kernel_root,
    UserAddressSpace* output
);

int user_address_space_release(UserAddressSpace* space);
```

头文件显式包含 `mm/user_memory.h`，它提供表和内容描述类型；实现放 `kernel/mm/user_address_space.cpp`。不需要为两个C入口再新增.hpp或通用容器。内部检查/辅助函数留在.cpp。

三个成员的职责分别是：

- `root`拥有一张私有根页；`kernel_root`借用永久模板，二者非空且不同。
- `memory`拥有内容页，非空时 `memory.root == root`；该内部root仍然借用，不拥有根页。
- 外层root独立保留，使内部memory释放清空后，外层仍能重试根页free。这里 `space.root` 与 `space.memory.root` 可以数值相同，但前者拥有、后者借用；`space.root` 与 `space.kernel_root` 则必须不同。

不改变UserMemory布局、原user_memory_release或user_memory_load_elf签名。创建成功后仍由boot调用原loader，以 `space.root` 为装载root、`&space.memory` 为输出。暂不加“创建+装载+转移”巨型工厂，避免混入新的失败事务。

create先判NULL和空输出约定、核对模板及根地址合法/页对齐，再调用现有页表创建入口分配一张清零root，填入共享项并一次提交。成功0；模板/参数非法返回INVALID_ARGUMENT，耗尽返回NO_MEMORY。非空输出拒绝且保持，调用者正常使用 `{0}` 初始化；不能覆盖原有地址空间。

完整空对象是root/kernel_root为空、内部memory为空。合法存活状态包括：刚create、尚未load的root-only对象；装载成功的对象；内容释放部分失败的对象；内容已释放但根free失败的root-only对象。普通按值复制不自动创造第二份所有权；仍显式转移后清源，无析构、new/delete或自动资源吞错。

## 显式释放及重试

release不自行切表。它应遵守以下顺序和边界：

1. 判NULL；完整空对象重复释放成功。先验证外层root/模板指针、数量界限与内部状态关系。内存对象须真实有效；原始指针非空不能证明指向合法页表。
2. **任何unmap/free之前检查真实satp。** 当前MODE=Sv39且根PPN等于待释放私有root时，返回INVALID_ARGUMENT，整个对象、页表和页数保持；不要把ASID位当PPN。此检查必须覆盖root-only状态。
3. 核对模板和私有root的共享槽0/2保持一致，其他未支持槽为0；核对内部非空描述确实借用私有root，全部有效VA都在槽1，非空内容PA不等于私有root或模板root。不能先调用内层release，再检查是否误碰了借用分支。
4. 调用原 `user_memory_release(&space->memory)`，保留它的全部归属检查、阶段顺序、成功free清PA和失败重试语义。失败直接返回对应错误，外层root和模板继续保留。
5. 内层整体成功后，检查私有root槽1确实为0，再归还私有root。若槽1仍非零，返回INVALID_ARGUMENT并保留外层root，不递归强制清理未知映射；它们不属于本批正常装载路径。
6. 根页free失败返回PAGE_FREE_FAILED，保留root/kernel_root，即使memory已经全空也能再次release。只有根free成功后才整体清空外层描述。共享下级表与kernel_root永不free。

普通运行只允许装载器在私有槽1创建映射，释放由完整外层对象负责，不绕过它直接丢弃root。根free失败的重试仅需保留根页状态，不能为了重建旧memory而恢复已经释放的PA。调度器遇释放失败仍沿用打印并停机策略，本批不新增自动重试调度。

loader自身回滚再次失败而丢失局部内容归属的历史限制保持；新外层对象至少保留私有root，但不宣称已修复这一旧事务缺口。没有已知归属的存活叶映射时，禁止仅为“清空对象”而释放根页。

## 调度与启动接线

`sched.h`包含新归属头，将任务内部 `UserMemory user_memory` 替换为 `UserAddressSpace user_space`。这是任务私有实现变化；原UserMemory本身保留。公开接口定为：

```c
int sched_init(const Sv39PageTable* kernel_root);

int task_create_user(
    uintptr_t entry,
    uintptr_t user_stack_top,
    UserAddressSpace* space
);
```

sched_init在boot受控调用一次，保存借用kernel_root；检查参数非NULL、地址为合法PA且页对齐，有效返回0，非法返回-1且不重置调度状态。“没有存活旧任务、kernel_root仍活动、模板映射有效”是本批boot调用前提，不额外要求在初始化时新增一套活动性或整树扫描检查。内核模板不是调度器拥有的资源。

task_create_user在分配槽位前确认外层root独立、模板等于已登记kernel_root、内部描述已成功装载且借用同一root，再保留原entry/SP对齐、entry与SP-1记录和PA存活检查。成功按值接管完整外层对象后清空源；失败完整源描述保持。内核任务与idle的user_space保持空。当前用户root getter保持原签名，只从当前RUNNING用户任务借用内部root，不分配、不切satp。

新增一个调度私有辅助函数即可集中选择root：U任务选择其私有root；S任务与idle选择kernel_root。调用现有 `sv39_activate()` 并检查返回值；切换失败打印并停机，不能返回另一个任务的frame却仍使用旧root。

接线只放在三个明确位置：

1. `scheduler_start()` 找到首个任务后、进入其frame之前，激活它对应的root。
2. `sched_reschedule()` 保存原frame、决定next/idle后，在返回目标frame之前激活目标root。当前S栈、目标frame及代码都通过共享内核映射可达；可以在C函数内完成，无需改trap_frame或汇编偏移。
3. `sched_on_exit()` 的用户正常退出和选定用户fault共用路径：先成功激活kernel_root，再release当前完整地址空间，再切下个任务。即使下个任务还是用户，也不能在旧用户root活动时释放旧root。保持当前失败停机策略。

普通WRITE/GET_TIME不触发调度，继续在当前用户root下处理并返回该用户。`sscratch`仍只负责内核栈，U/S切换本身不会替软件更换satp。单hart、ASID=0和当前activate的全局刷新保持，不新增按ASID优化。

boot在kernel_root已建立且活动时create私有space，将原ELF装入其中，调用新的sched_init和task_create_user；普通A/B创建保持。装载或创建失败、源对象尚未转移时，在kernel_root下检查外层release结果；成功转移后源为空，由任务拥有。不要把空源再次当作任务根的拥有者。启动仍由原scheduler_start进入第一个S任务，不在boot永久启用用户root。

## 文件范围和分工

| 文件 | 本批工作 | 负责 |
| --- | --- | --- |
| `include/mm/user_address_space.h`、`kernel/mm/user_address_space.cpp` | 新外层归属、模板借用、活动拒绝和显式释放 | 作者 |
| `kernel/mm/user_loader.cpp` | 用户完整区间限制槽1，保留原分配/复制/回滚 | 作者 |
| `include/task/sched.h`、`kernel/task/sched.c` | 接管外层、登记kernel_root、选择satp及先切回再释放 | 作者 |
| `kernel/boot/private/boot_stages.c` | 创建私有space、原loader装载、初始化/转移/失败清理接线 | 作者 |
| `Makefile`、`test/CMakeLists.txt` | 新实现对象/宿主算法源接入 | 作者，协作者复核 |
| `test/host_support.hpp/.cpp`及少量新测试 | 模拟satp和新归属P0，保持原75组语义 | 协作者 |
| 本批文档、索引和记忆 | 集中审查、结果、理解原答与修正 | 协作者 |

新实现可以加入宿主算法库：satp通过外部C函数读取，宿主提供桩，真实CSR仍只编目标vm_arch.c。宿主模拟默认无活动root，活动拒绝场景显式设置并恢复模拟值；不能把桩当作真实硬件验证。不把sched.c、vm_arch.c或含fence.i的loader加入宿主库。

user/main.c、syscall.c、timer.c、trap入口/恢复、trap_frame布局及kernel_vm恒等构表算法均不要求重写。保留BATCH07演示输出；最多在boot打印一次kernel/user根地址，观察二者不同，不添加用户buffer矩阵或常驻资源追踪框架。

## 可观察结果与分级验收

默认资源数应从6变为 **7 = 1私有root + 4内容页 + 2用户中间表**，不包括原kernel_root及共享内核下级表。相对“kernel_root已建好、私有space尚未create”的基线计数；退出恢复这7页。内核根从头到尾不出现用户槽1映射，用户进程能运行是因为调度选择了私有root。

作者实现并跑通常的make/qemu，以下P0由协作者保证和记录；P1/P2对作者均选做，不阻塞下一批。不以新增测试数量为目标：

| 等级 | 场景与具体预期 | 学习点与分工 |
| --- | --- | --- |
| P0 | make -B无警告，原75个宿主运行组语义保持，新归属最小用例通过；C入口完整，既有构造/析构运行时正常解析，无未实现的新运行时依赖 | 协作者例行验证，作者无需重写旧fixtures |
| P0 | 宿主创建两张私有root，借用同一模板槽0/2，在同一槽1用户VA映射不同PA，改一方内容不影响另一方；分别释放后借用表及另一方保持 | 相同VA和不同根；协作者少量fixture即可，不要求双用户调度演示 |
| P0 | 一个带用户槽1的模板create在0次alloc拒绝、输出不变；另一个用户完整区间跨槽1→2的loader调用在0次内容alloc拒绝、私有root及空memory保持 | 保护共享分支；root可能已由create分配，装载的零分配从loader调用前计，不误计那1页 |
| P0 | 模拟活动私有root时release零修改拒绝；恢复非活动后正常释放。另在私有根free处失败一次：内层已空、外层保留，重试仅归还剩余根；模板及共享表无free尝试 | 安全销毁与root-only重试；协作者完成必要失败场景，不扩矩阵 |
| P0 | 正式QEMU原Time OK/Hello/Data OK/exit正常、无fault/halt，退出后两次S timer；临时观测确认U执行/syscall持私有satp，S任务与idle持kernel_root，yield/sleep和已有timer选任务时选择root正确 | CSR/调度闭环；协作者临时记录，不要求作者编写探针或再写用户程序 |
| P0 | 实际消耗7页；exit先切kernel_root，释放前确认私有用户分支已空，再归还根；7页恢复、拥有对象全空、kernel_root与共享分支保留且活动 | 根生命周期；探针在free前观察待释放表，不能释放后再用旧指针query来宣称无映射 |
| P1 | 私有root分配失败、内部内容free失败后经外层重试、受控用户fault复用退出销毁 | 原异常路径组合；协作者可协助，作者可只讨论思路 |
| P2 | 双用户真实抢占、更多模板/槽边界、旧QEMU脚本全适配 | 后续扩展，可能牵动演示结构，不列本批前提 |

默认A常驻，idle通常不会实际进入；需要观测idle时由协作者只在临时副本让普通任务有限结束，确认idle持kernel_root。这是P0目标root选择的最小观测，不增加作者产品任务。用户运行时的timer事件若未自然出现，协作者临时延长用户计算，验证原抢占选择，而非要求作者改正式演示。

共享表保持的核对既要看根槽0/2值，也要看对应下级表与内核映射；不只证明“root指针还没清”。宿主模拟只验证归属/拒绝策略，真实satp和flush必须在QEMU验证。新类不承担未知PTE树的通用回收。

常用命令（仓库根目录）：

```sh
make -j4
timeout 8s make qemu
```

整批后由协作者运行 `make -B -j4`、`make test CTEST='ctest --verbose'`，并核对ELF符号和临时QEMU计数/切换。保持不clean/stage/commit/push或建worktree；现有B07产品、测试与文档未提交改动完整保留。旧Q1/Q2等脚本需要时仅做最小适配，不将全套历史矩阵变成新批门槛。

## 实现顺序与理解收尾

建议先写外层create/release，再补loader的槽边界，再接task归属和三个切表点，最后改boot接线。它们共同完成一个地址空间生命周期，不并行启动进程创建或语言迁移。作者整批实现后集中列全问题，再修正复验；协作者承担上述P0。

功能完成后梳理“借用哪些表→拥有哪些页→切表时哪些地址仍可达→退出先停用谁→失败留下谁”，再用原答/审查/修正版归档三题：

1. 两张私有root复制了相同的槽0/2，为什么它们仍是不同用户地址空间？共享项能否随任一用户退出一并释放？
2. 从用户trap进C时satp尚未切换，为什么代码和内核栈仍可用？为什么exit必须先切kernel_root再释放私有root？
3. 内层UserMemory已释放，但私有root free失败，外层该保留什么？默认恢复7页里，哪部分是本批新增的拥有资源？

本批功能及P0已通过，功能路径和三题原答/集中审查/修正版均见下方。rCore资料只用于解释具体原理，远期GitHub文档网站安排保持原里程碑。

## 草稿集中审查（2026-11-03，历史，生命周期问题已在下方修正）

作者已新增外层头/实现，选择值成员名`user_memory`（下文早期接口示例的`memory`与它职责相同）；boot已create私有root、以space.root装载并写入space.user_memory。当前审查范围为外层create/release及必要调用关系，未代改实现。

1. release入口必须先拒NULL，再接受完整空对象。当前NULL在空检查helper中解引用，完整空对象返回非法；两个根非空而内层空的root-only对象却直接返回成功、不归还根。root-only仍须经过活动/归属检查和根释放。
2. 根槽检查须互斥分类：0/2要求模板有效非叶且与借用项一致；1要求模板零、允许私有用户分支存活；其余要求双方零。当前释放前要求私有槽1零，且随后要求非零私有项和零模板项同时有效，正常已装载空间必被拒。内层释放成功后才要求私有槽1零。
3. 活动拒绝须比较真实satp的MODE与根PPN，位于任何修改前且覆盖root-only。当前satp非零仅决定是否调用内层，既不拒活动root，Bare时还跳内层直接free根。
4. 补外层约束：合法/对齐且不同的两个根、内部非空时借用同一私有root、数量界限、有效VA全属槽1、非空内容PA不等于两根。允许部分释放重试的null PA，不重复内层已有通用检查。
5. 内层失败原样返回；根free失败返回PAGE_FREE_FAILED并保留外层。当前内层错误统一改-7，根free结果忽略后清指针并报成功。内层成功自行清空，无需额外抹记录。
6. create须在首次分配前拒非空输出、验证模板根地址及完整槽策略；当前只检0/2的V位，会接受叶项/额外槽并覆盖存活输出。
7. 外层两个函数声明补条件式extern C保护。当前boot为C++所以此调用可匹配；目标nm确认导出改编名，后续C调度器调用原名会链接失败。

后续原定接线仍未完成：task只转移内层，sched_init尚未保存模板、三个切表点未接；boot失败路径仍释放内层且有失败后清空外层语句。保持原批次范围，不另加作者测试作业。

验证证据`.cache/batch08-current-review-omk3grsf/`：真实RV64单文件编译无诊断，target_symbols.log记录改编符号；probe.cpp以正式外层源码和隔离桩运行，results.json复现上述入口、槽位、活动判断及失败状态问题，NULL由UBSan诊断。模拟satp/free不是硬件验证，未跑整仓/QEMU，也不代表内层真实算法被重验。6个正式源配置哈希保持，独立静态审查结论一致；只更新本批文档/记忆，未修改产品、clean或提交。

## UserMemory内部职责整理（2026-11-03）

作者澄清需要将同一类的页记录校验与释放流程分开。基于作者已抽出的validate_pages，局部改为validate_pages() const（数量界限、有效记录VA/PA范围和重复校验，使用只读span），再将原query/PA/U归属检查提取为validate_mappings() const。两个成员分别验证记录与映射；release检查二者结果后依次unmap、flush、free、reclaim，空状态和部分失败重试规则保持，未新增校验循环或用户槽策略。此前讨论的release_for_address_space(expected_root,kernel_root,slot)提议撤回，不把外层模板策略塞入内层。

只改include/mm/user_memory.h和kernel/mm/user_memory.cpp；make test的CTest 1/1通过、真实RV64单对象编译无诊断，内部独立审查无阻塞。未修改外层草稿或推进整批接线，未整仓链接/QEMU；上节外层问题仍待修。未新增作者测试作业。

## 地址空间生命周期修正及验证（2026-11-03）

作者明确要求协作者直接修改当前错误，本次完成创建/校验/显式释放模块及必要回归，保留作者字段名、C ABI和UserMemory的校验成员划分，未代接完整调度器。

实现位于kernel/mm/user_address_space.cpp，辅助函数各自负责根地址、模板、共享布局、内部记录与活动状态：

- valid_root检查非空、物理地址编码范围和4KiB对齐，位于页表解引用前；这不承担验证任意裸指针实际分配来源的工作。
- 模板只允许槽0/2包含PPN与V，其他槽必须零；create先拒绝非空输出及非法模板，再分配一张清零根，复制两条共享分支。
- 外层校验要求两根不同、共享项保持、私有未支持槽为空；内部记录借用同一私有根，数量受限、VA全属槽1、非空内容PA不等于两个根。内层通用地址/映射校验仍由UserMemory负责。
- release区分完整空对象、root-only和已装载状态。MODE=8且satp根PPN匹配时，在任何修改前拒绝，ASID不参与PPN比较；不自行切根。
- 释放流程为外层校验→内层release（错误原样返回）→确认用户分支空→根free→整体清空。根free失败保留外层，内层已空允许只重试剩余根。
- boot_stages.cpp两处失败清理改为user_address_space_release(&space)，失败时不抹掉拥有描述。task成功后的完整外层转移仍属后续调度接线。

必要回归已由协作者完成，无新增作者测试作业：

1. 原宿主75具体组保持，新6组覆盖NULL/空/root-only、create拒绝零分配、正常释放与借用表保持、带ASID的活动根拒绝、根free失败重试、错误归属拒绝。修正前基线先实测root-only返回-1而非0；最终81具体组+1汇总通过。
2. make -j4内核链接成功，nm确认两个外层入口为C符号，kernel.elf无未解析；临时完整目标构建无警告。
3. 真实QEMU临时替换main，使用正式启动/PMM/VM/loader/runtime。root-only实际激活私有根后拒绝释放，描述/页数/全部相关表哈希/satp保持；切回模板后回收1页。正式嵌入ELF装入4内容页，总7页，活动拒绝后切回回收全部7页，描述清空。
4. 共享根/L1/L0哈希及text、rodata、data、根页所在PMM页、UART五项映射保持。61个正式源配置哈希在探针前后相同。

证据.cache/batch08-lifecycle-fix-27vq9_oh/：host-final-ctest.log为最终绿；host-baseline-run.log和host-after.log为修正前红。qemu/含可重跑run_probe.py、probe_main.cpp、results.json及构建/运行日志。脚本8秒后SIGTERM停止QEMU，返回0不代表客体主动关机。该探针没有启动调度器或进入U态，失败注入仅在宿主完成，不能宣称B08完整用户运行已验收。

下一步维持原任务：补loader槽范围前置检查、让task接管完整外层、sched_init登记模板和三个切表点。未执行clean/stage/commit/push，其他作者改动保留。

## 作者调度接线与正常链路复验（2026-11-03，历史阶段）

作者完成task持有整个UserAddressSpace、创建成功后清空来源、sched_init登记模板并检查地址、idle空间初始化、首次/普通next/idle三处切根、exit正常先回内核root再释放；boot已检查sched_init返回及清理外层。此次协作者只读审查及例行复验，不代改产品。

make -j4无警告、make test的81具体组通过。正式QEMU Time OK、Hello from user、Data OK、exit各1次，exit后两次S-mode tick继续，无创建/释放失败或fault，124为8秒timeout。证据.cache/batch08-scheduler-review-7f9fvqs9/的build.log/qemu.log/results.json。此轮默认观察没有新增CSR记录或强制idle，不把它当作全部切换场景的独立证明。

集中审查只有以下两项原定功能待补，不新增作者测试作业：

1. user_loader.cpp在三个完整PageRange构造成功后，仍缺槽1限制；需在首次内容页分配前验证首、末映射字节都属于USER_ADDRESS_SPACE_ROOT_INDEX。end是one-past，检查end地址减1，包含完整数据/BSS映射范围和栈最后字节。
2. sched_on_exit中sv39_activate(g_kernel_root)失败仍return原frame；需沿用打印并停机策略，不能让已标DEAD的任务继续返回运行。

其余关键接线经独立只读复核无新阻塞。task_create_user的组合条件虽可简化，但与find_page合用已经拒绝空/数量非法/内部root不匹配，不作为新增作业。两项完成后由协作者集中补齐原P0的切表/idle、同VA隔离和范围零分配拒绝证明，再进入原功能梳理与三题。

## 最终功能验收（2026-11-03）

作者已完成上一阶段两项：三个完整范围的首、末映射字节在首次内容页分配前限制为槽1；exit切回内核根失败打印并停机。最终只读审查未发现新阻塞，协作者仅补原定宿主隔离用例、临时P0探针及文档，没有代写本轮正式产品改动。

证据统一位于`.cache/batch08-final-6ubwkiz6/`：

- `make -B -j4`无警告，最终ELF无未定义。正式默认QEMU Time OK、Hello from user、Data OK、exit各1次，exit后2次S tick，无fail/fault；124为8秒timeout。日志build.log/default-qemu.log和default-results.json。
- `make test`共82具体组及1汇总通过，原75保持；新归属测试共7组。最后一组同时持有两张私有root，在同一用户VA映射不同PA，修改第一方时第二方完整快照保持；第二方保持模拟活动时释放第一方，第二方与共享表保持；最后共归还8个私有页。日志host.log/host-ctest.log和host-results.json。此前活动拒绝/根free失败重试组继续通过。
- 临时真实QEMU使用正式启动、loader、调度及用户逻辑。跨槽负例同时将布局和匹配的data LOAD VA改为0x7ffff000，保留原多页memsz；正式loader返回INVALID_ARGUMENT，pmm_alloc_page调用为0，root/模板/描述/页数/satp保持，随后继续正常装载。
- 实测完整外层转移且boot源清空；首次S启动1次、S选择2次、U选择4次、idle选择4次、真实U timer中断1次。返回目标frame之前的satp与独立记录的预期根匹配；WRITE/GET_TIME/yield/sleep/exit的syscall入口保持用户root。
- 真实exit先切内核根再释放；free wrapper在归还根页之前确认用户槽空、前6页已释放，4内容+2下级表+1根页精确各free一次，7页恢复、任务拥有对象空，模板及全部共享表哈希保持。没有释放后解引用旧root来证明清理。退出后至少2次idle timer保持。

临时探针位于qemu/probe.cpp、probe.h、run_probe.py，结果为qemu/results.json和qemu.log。为暴露idle与用户timer，只在副本使A跑一轮、给U增加约1.5个timer周期的计算和GET_TIME采样；正式演示未改，61个正式源/配置哈希一致。12秒由runner SIGTERM停止，不代表客体主动关机。保持单hart/ASID0、不扩通用ELF矩阵或多用户正式演示；根free失败注入由宿主完成，先前真实生命周期拒绝证据仍保留。

全部原定P0已满足，当时进入既定功能梳理与三题理解归档，现已在下方完成集中审查。P1/P2仍选做，未新增作者测试任务，未clean/stage/commit/push。

## 当前功能路径梳理

1. boot在内核根活动时创建私有root，只浅复制槽0/2的内核分支。它们保证UART、内核代码、数据、栈与PMM在私有root下仍可达，借用分支不随用户退出释放。
2. loader先验证代码/栈/完整数据BSS范围位于槽1，再分配内容页、建立私有用户下级表。UserMemory中的root借用同一张私有根，外层root保留根页所有权。
3. task_create_user成功后按值接管完整UserAddressSpace并清空boot来源；失败保留来源供boot清理。原始指针按值复制本身不会自动转移所有权，清源是显式约定。
4. 首次进入任务，以及timer/yield/sleep选择next或idle时，调度器先激活目标根再进入/返回目标frame。U选择私有根，S与idle选择内核模板根。
5. U-mode陷入syscall时CPU不会自动更换satp。trap通过sscratch切内核栈，执行仍借助私有root中的共同内核映射；WRITE/GET_TIME使用当前用户根校验用户buffer。
6. 用户exit先激活内核根，再释放内层用户内容和下级表，确认用户槽空后归还私有根。默认归还7页，模板与共享分支持续存活。根free失败时保留外层root，内层已空，下次只重试剩余根。

## 三题原答、集中审查与修正版（2026-11-04）

作者用一段话合并回答三题，原文保留：

> 因为槽位1才真正指向用户数据，其他两个槽位存放的是槽1其上下游，而恰好这俩槽已经复制了与内核相同的地址，所以可以直接访问内核代码和栈，退出释放前不是内核根的话，会残忍地直接把这块地方杀掉，下一步直接找不到页，释放之后外层是存系统根和私有根，user_memory已经被清空，只会到释放space->root

### 第一题：共享内核分支与用户隔离

审查：认可槽1为私有用户分支、槽0/2使用共同内核映射的方向。但三个根槽是并列的虚拟地址分区，槽0/2不是槽1的上下游；槽1本身指向用户L1页表，最终叶PTE才指内容页。

修正版：两张私有root的槽0/2都借用同一组内核下级表，槽1分别指向各自的用户下级表。相同用户VA从不同根进入不同私有分支，最终可映射不同PA，因而数据隔离。槽0/2及其下级表属于内核模板，用户空间退出不能释放。

```text
私有root A                    私有root B
  [0] ──→ 共同UART分支 ←──────── [0]
  [1] ──→ 用户表A → 内容PA_A    [1] ──→ 用户表B → 内容PA_B
  [2] ──→ 共同内核分支 ←──────── [2]
```

### 第二题：trap仍可执行与销毁前切根

审查：共同映射使内核代码和栈可达、退出必须先切内核根的主线正确。需把“下一步直接找不到页”理解为活动翻译依赖失效的风险，而非保证下一条指令立即fault；已有TLB条目可能暂时掩盖问题。

修正版：U-mode trap进入S-mode时不自动更换satp，sscratch切换的是内核栈。当前私有根借用的槽0/2仍映射内核代码/栈/PMM/UART，S-mode可继续执行。释放用户空间会撤销用户映射、回收下级表并最终归还根页；若satp仍指向该根，后续翻译可能访问已清空或被复用的页表，造成页故障或错误映射。因此调度器必须先成功激活内核根，再开始释放；release自身则拒绝活动私有根。

### 第三题：最终根free失败的重试状态

审查：内层已清空、外层保留私有根与模板指针、重试只归还space->root，回答正确。这里的“释放之后”特指内层成功而最终根free失败的中间状态；完整释放成功会清空整个外层。

修正版：内层成功后user_memory为空，用户槽1为零；最终根页free失败时，space->root保留尚未归还的私有根，space->kernel_root保留借用模板。重试仍先验证非活动与布局状态，内层空释放无副作用，然后只尝试归还剩余根页；成功后整个space清空。内核模板永不随此路径释放。默认7页为4内容+2用户下级表+1私有根，最后这一张根是B08新增拥有的资源。

此次完成一次集中纠错，修正版由协作者解释整理，不要求作者重复作答。功能、P0与本次理解归档均完成；未修改产品/测试或重跑已通过验证。随后交付的下一批任务见[BATCH09](BATCH09_TWO_USER_TASKS.md)，不在本批继续扩展。

## 准备记录

2026-10-30：完整核对约定、当前记忆、B07完成结论、TinyOS根表/loader/释放/调度/trap源码与rCore章节边界；内部子代理只读讨论并复核根分支借用、活动销毁和root-only重试设计。仅交付完整任务与索引/简短记忆，未写产品代码、未运行新构建/测试或QEMU。文档自检及源码/暂存区保持核对在 `.cache/batch08-task-preparation/`。
