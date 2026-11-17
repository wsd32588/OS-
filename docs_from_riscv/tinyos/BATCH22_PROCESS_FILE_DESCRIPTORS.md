# BATCH22：进程FD与用户open/read/close

日期：2026-11-15。状态：功能、全部既定P0及原三题原答/集中澄清归档完成，本批收尾，不新增测试作业。

B21的内核打开文件模型已完成。本批把它接到真实Process和用户调用，核心由作者实现，协作者集中审查、接宿主测试并承担第8节固定P0。
本文件一次给齐接口、生命周期、失败状态、接线和三题，不在修正过程中追加矩阵。

## 1. 目标与边界

用户可执行：

```c
long fd = user_open(path, path_length);
long first = user_read(fd, buffer, 7);
long second = user_read(fd, buffer + 7, 11);
long result = user_close(fd);
```

当前位置保存在OpenFile中；用户read不再传offset。两次独立open位置独立，fork继承共享位置，exec保留FD，退出统一关闭。
只读内置文件、8个FD/40个池对象继续沿用B21；fd0合法，没有stdin/stdout预留。
不做写文件、dup、seek、CLOEXEC、目录树、磁盘、管道、路径EXEC或argv/envp。原1—14号ABI保持，READ_FILE14仍是显式offset接口。

## 2. 一份文件服务、一份池

新增`include/fs/file_service.hpp`与`kernel/fs/file_service.cpp`，namespace tinyos::fs。
将file_syscall.cpp里的私有g_filesystem迁到服务；服务另拥有一个私有`constinit OpenFilePool g_open_files{};`。
不把池、任务表、current槽、root变成公开全局，也不提供返回池的getter。
文件服务管理自己拥有的私有状态，风格与PMM/内置程序registry一致；跨模块传入实际FD表，不在文件层读取scheduler状态。

完整服务接口：

```cpp
// <span>及必要前置声明由本头自行提供；实现包含完整类型。
int configure_file_service(const BuiltinFileSystem& filesystem) noexcept;
bool file_service_ready() noexcept;
const BuiltinFile* lookup_file(std::span<const char> path) noexcept;
int open_file(FileDescriptorTable& table, std::span<const char> path) noexcept;
int close_file(FileDescriptorTable& table, int fd) noexcept;
void close_all_files(FileDescriptorTable& table) noexcept;
int inherit_files(const FileDescriptorTable& source,
                  FileDescriptorTable& output) noexcept;
```

- configure拒绝未初始化FS和重复绑定；全预检后保存借用指针，失败不改变配置/池。FS和底层字节必须长期存活。
- lookup未配置/非法/未知路径返回nullptr，否则返回FS的借用记录。
- open先lookup再table.open(g_open_files,*file)；失败不新增FD或引用。
- close委托table.close；未配置返回-1。inherit委托output.inherit_from(g_open_files,source)，未配置返回-1。
- close_all用于拥有者清理，空表直接返回；未配置却持非空表是内部错误，实际panic，不静默丢弃引用。
- 所有发布进Process或用于fork暂存的FD表都来自这一服务；B21 boot局部演示池保持独立，不将其指针移入Process。

只保留这一份“已配置”状态：删除旧configure_file_syscalls声明/定义，boot迁为configure_file_service。
已有READ_FILE14内部也改用file_service_ready/lookup_file，保持原错误顺序、长度/EOF/零长/复制语义。
file_syscall.cpp负责用户内存边界，file_service.cpp负责文件记录和打开对象资源，不复制两份g_filesystem。
主Make加入kernel/fs/file_service.o；宿主CMake与旧测试配置调用迁移由协作者负责。

## 3. Process拥有FD表

process.hpp显式包含fs/file_descriptor_table.hpp及<span>，新增唯一成员：

```cpp
fs::FileDescriptorTable files_{};
```

Process不保存pool指针，不复制/移动Process，不暴露可变FD表。新增窄接口：

```cpp
int open_file(std::span<const char> path) noexcept;
fs::OpenFile* find_open_file(int fd) noexcept;
const fs::OpenFile* find_open_file(int fd) const noexcept;
int close_file(int fd) noexcept;
void close_files() noexcept;
int clone_file_descriptors(fs::FileDescriptorTable& output) const noexcept;
void adopt_file_descriptors(fs::FileDescriptorTable& source) noexcept;
bool files_empty() const noexcept;
```

open/close/close_all/clone分别委托服务并传自己的files_；find/get、adopt和empty委托自己的表。
clone只复制表项引用并共享OpenFile，语义与复制用户物理页不同；output必须空，失败保持源和目标原状态。
adopt只移交已由同一服务准备的表，空目标/非自身前置条件和B21一致。

把原`release_resources()`明确改名为`release_address_space()`，仍只调用user_address_space_release。
同步头/实现及exec、exit两处原调用；不要在这个方法里close_files，否则exec会丢FD。
resources_empty在原地址空间全空检查之外增加files_.empty()；reset仍先全预检再清元数据，FD非空时返回-1且所有状态保持。
表已空不必再赋值`files_ = {}`（它禁止复制/移动）。TaskTable/reset/reap继续复用resources_empty，不偷偷关闭文件掩盖泄漏。
adopt(UserAddressSpace&)只移交地址空间，不改FD、父ID和退出信息，也不能要求整个resources_empty（exec允许已有FD）。

## 4. 新ABI与用户内存边界

新增：OPEN15、READ16、CLOSE17；`SYSCALL_READ_MAX=256`，READ_FILE_MAX可以别名到它，原上限保持。
用户声明：

```c
long user_open(const char* path, size_t path_length);
long user_read(long fd, void* destination, size_t length);
long user_close(long fd);
```

| 调用 | 输入寄存器 | 返回 |
| --- | --- | --- |
| OPEN15 | a0用户path地址，a1长度（不含NUL） | fd0..7或-1 |
| READ16 | a0完整宽度fd，a1用户目标地址，a2长度 | 实际字节数/0/-1 |
| CLOSE17 | a0完整宽度fd | 0或-1 |

用户syscall.S仍仅设置a7、ecall、ret。长度和fd先用完整宽度验证，再转size_t/int；例如0x100000000不能截断成fd0。
每次READ最多256，先验证合法已打开fd，再处理零length；非法fd即使零length也-1。合法零length/EOF返回0，不访问目标。
超256在读取/拷出前拒绝；目标只需容纳实际count。拷出失败可改变合法前缀，但打开位置保持。

### C分发与C++桥接

不提供sched_current_process或任务表getter。在sched.h增加纯C类型的三项操作：

```c
long sched_open_file(struct trap_frame* frame,
                     uintptr_t path_address, uint64_t path_length);
long sched_read_file(struct trap_frame* frame, long requested_fd,
                     uintptr_t destination_address, uint64_t length);
long sched_close_file(struct trap_frame* frame, long requested_fd);
```

sched.cpp复用current_user_task(frame)及supervisor_interrupts_disabled；任一不满足返回-1，
通过后仅把当前`*task->proc`和请求参数传给下列C++桥接，不在scheduler写路径复制/文件操作逻辑：

```cpp
// 声明在file_syscall.hpp；前置声明tinyos::Process，自行包含stdint.h。
namespace tinyos::fs {
long syscall_open_file(Process& process, uintptr_t path_address,
                       uint64_t path_length) noexcept;
long syscall_read(Process& process, long requested_fd,
                  uintptr_t destination_address, uint64_t length) noexcept;
long syscall_close(Process& process, long requested_fd) noexcept;
}
```

file_syscall.h继续保留原C桥接fs_syscall_read_file，纯C++类型不放进C链接块。
实现文件明确包含本模块.h与.hpp、Process完整定义及服务头，不依赖hpp顺带导入C声明。
syscall.c新增三case取参数、调用sched操作、写回a0后break；沿用统一sepc+=4，不新建/替换frame。

### OPEN/READ/CLOSE顺序

OPEN：服务ready/root合法/路径长度1..127→copy_from到真实内核path数组→Process.open_file(path span)→返回fd。
路径语法/NUL/未知名由lookup处理，不扫描用户NUL；路径复制失败/未知名/满表/满池均不发布半成品。

READ：ready/root合法、完整fd满足0<=fd<8、length<=256→find_open_file非空→零长返回0→
read_current到内核256字节数组→检查count非负且<=length→count0返回0→user_copy_to实际count→成功后commit_read(count)→返回count。
不能先commit再复制，也不预扫描整个请求length的目标页。拷出失败返回-1不提交，允许用户前缀改变。
完整拷出后commit若失败属于内部不变量破坏，实际panic，不将它当普通输入错误返回。
调用链SIE关闭且中间不调度，避免共享位置在prepare/commit间被另一任务推进。

CLOSE：ready/当前Process合法、完整fd范围检查→Process.close_file(int fd)→返回0/-1。

## 5. fork、exec、退出与复用

| 事件 | 本批行为 |
| --- | --- |
| boot创建/运行期SPAWN | 新Process空FD表，不继承创建者的FD |
| FORK | 同fd编号共享OpenFile/offset，每表项增加一个引用 |
| EXEC成功 | 同Process保留FD，旧用户栈/内存替换不影响FD |
| EXEC准备失败 | 旧FD、位置、引用及旧空间/现场保持 |
| EXIT/选定U fault | 在统一终止路径关闭本Process全部FD，孩子的共享引用继续有效 |
| REAP/RESET | 只允许已经空的FD表，不替退出路径补关引用 |

### fork保持原失败事务

user_task_create.cpp在task_allocate之前准备好地址空间和文件引用：

```text
当前用户/pc/sp检查
→ clone_address_space(candidate)
→ parent.proc->clone_file_descriptors(pending_files)
→ task_create_user_from_frame(..., candidate, parent_id, pending_files)
```

pending_files是局部FileDescriptorTable。继承失败清候选地址空间；Task分配失败时通过fs::close_all_files(pending_files)撤销新增引用，再清候选地址空间。
继续处理原地址清理失败的fatal分支，不丢失败资源或改成普通错误。
私有create_from_frame助手分配成功后，仅在原无失败发布阶段adopt地址空间、adopt_file_descriptors、设置现场/kind/parent。
分配失败不接管两个候选、不消耗公开ID；父表槽/位置不变，成功仅共享对象引用增加。
不要复制Process对象，不改变子a0=0/父a0=新ID/sepc和独立内核栈规则。

### 退出与exec释放分开

user_task_exit.cpp的task_destroy统一服务正常EXIT和fault：沿用切回kernel_root，再close_files，再release_address_space的顺序；
全部成功后FD表/用户空间均空，保留TaskExitInfo直到reap，其他父子关系与状态规则不变。
exec.cpp只改原释放方法名，不调用close_files，不清FD表；候选准备失败与不可逆提交失败规则保持。

## 6. boot/构建/正常演示

boot仍拥有长期g_builtin_fs，调用新的configure_file_service并检查结果；B21局部FD model演示保留，不发布其局部池引用。
boot配置/原两S两U/reaper顺序保持，Process/Task接口不新增公开身份。
主Make加file_service.o；其余产品对象原已接好。宿主库和旧配置调用/必要测试入口迁移由协作者完成，不要求作者添加测试reset接口。

正常演示放user/worker.c，DEMO接近一页上限，不往user/main.c塞新逻辑、不扩大装载布局。
保留原READ_FILE14的7+11验证，再用新接口：

1. 两次user_open同一路径，要求都非负且fd不同，不硬编码0/1（exec可能带已有FD）。
2. 第一个fd连续read7和11，拼18字节核对；第二fd读取前7字节，证明位置独立。
3. 显式user_close第二fd并检查0；第一个fd故意留到进程退出，由统一退出路径关闭。
4. 输出`[worker] fd read OK\n`，原yield/全数据栈检查/退出argument+20保持。正常两份worker日志各一次，仍退出23/37、父0。

fork共享与exec继承的完整用户场景由协作者隔离测试，不要求作者再扩DEMO或写一套工厂。

建议实现顺序：Process成员/资源语义→file_service→Process方法→typed syscall/调度薄壳/ABI→fork/exit→boot/worker。
可以先写接口再补实现；原测试配置重命名的适配由协作者完成，不用因中间接线未齐而clean或批量重构。

## 7. 提前说明的限制

文件服务只有一次配置、一个私有池，所有Process FD来自它；没有池getter、没有Process池绑定指针。
不做进程FD表“整体复制”、默认析构关文件、或reset静默丢引用。文件内容寿命仍由长期FS提供。
read失败不承诺用户buffer原子不变，只保证不提交位置；原loader自身回滚再失败的历史fatal/归属限制不在本批解决。
已有QEMU脚本的配置/私有观察锚点需要助手适配，不把测试副本“修产品”当验收。

## 8. 固定验收与责任

P0由协作者保证可用，不是作者另写测试代码作业：

1. 目标构建/头自包含/C链接与原make test通过；真实文件服务、Process和模型参与宿主测试。
   FD非空时resources_empty/reset拒绝且元数据保持；仅释放地址空间不关闭FD；关闭后reset正常。
2. 真实U-mode OPEN/READ/CLOSE：独立打开位置、连续7+11、EOF、零长、非法fd(-1/8/高位值)、未知/非法路径、超长请求、不可写目标拒绝，失败引用/位置保持。
   一条跨页拷出失败实际前缀可能改变而位置不变，随后有效目标重试得到原位置数据。沿用8表/40池及原无PMM规则，不穷举新矩阵。
3. 正常正式用户：原READ_FILE14兼容，新FD成功日志两次、显式close及退出自动close都发生，退出23/37父0、最后服务池空。
   原五空间各7页、35次合法free/运行21分配/三次reap零PMM及最后两S ticks保持，文件操作PMM0。
4. 隔离生命周期负载：父open读7→fork子读11推进共享位置；子SPAWN的worker空表；子EXEC后仍能读继承fd；父关不影响子引用，正常exit/fault均清自己的表，reap后复用槽为空。
   一个满任务表的fork失败验证暂存FD引用与候选地址空间都撤销、父位置/引用/下一公开ID保持；未知程序EXEC失败保留旧FD。
   参考固定负载ID4 fork/ID5 spawn/ID4 exec/ID6 fault/ID7复用：7份正常镜像加1份失败fork候选，各7页，预计free56/运行alloc42/净恢复14、5次reap零PMM；若测试采用等价拆分，分别记录每段准确账本而不冒用这些数。

P1选做：带FD的EXEC准备OOM/重试、FD共享下的孤儿收养等故障场景；助手可按需要定位，不作为额外作者作业。
P2选做：更多容量组合/随机序列/500轮压力；不实施不影响本批收尾。
三题只在功能/P0完成后一次讨论，不要求重答或继续加矩阵。

```sh
make -j4
make test
timeout 20s make qemu
git diff --check
```

不需要clean、不提交推送。助手维护隔离探针/宿主共享配置fixture，保留快照/日志与范围；不新增生产reset钩子绕过单例配置。

## 9. 完成后梳理与原三题

梳理：当前USER识别→Process私有FD表→文件服务私有池→OpenFile共享位置→拷出后提交→fork候选移交→exec保留/exit关闭。

1. 为什么文件池属于系统文件服务，而每个Process仍需自己的FD表？同一个fd数值为什么不能拿去读另一个进程的文件？
2. fork准备完FD引用后Task分配失败，应撤销什么？为什么不能等allocate成功后再做可失败的文件继承？
3. exec保留FD而exit关闭FD，为什么不能把close_files放进release_address_space？用户拷出失败为什么也不能先推进共享offset？

功能与P0通过见第18节，原三题原答/集中澄清/协作者整理修正版见第19节；前期接口准备证据不单独作为功能验收。
配套阅读可继续对照第三版第5章的文件操作/打开文件表/进程表，重点看“一次打开的当前位置”与“进程的描述符”的分离；中文页码未核，不作为编码前置。

## 10. 交付前接口准备与限制

建议服务/typed syscall/Process增加FD成员与方法、调度C接口已在`.cache/batch22-preparation-20261115/`临时头中检查：
RV64 freestanding与native C++20在-Werror下自包含/重复包含/constinit/禁复制和调用形状通过，调度边界在C11中也通过。
没有修改正式头或实现方法，不把语法检查当作服务功能验收。

同目录另用临时用户副本增加15/16/17包装和第6节正常WORKER示例，仅编译/链接：DEMO RX3994、WORKER RX2153，RW内存各4152字节，
原单页代码断言通过，无诊断；不需要扩大装载布局。正式user代码及内核均未代写，此处没有运行新ABI。
92个正式源码/构建配置hash保持，证据results.json、rv64/native/c-boundary.log及user-preparation-build.log。
独立只读架构审查支持文件服务私有池方案；具体实现完成后仍需第8节真实功能/P0验证。

## 11. Process局部实现审查（2026-11-15）

作者请求检查process.cpp。实读头/实现及file_service，独立只读审查一致：resources_empty纳入files_.empty、
reset失败不改元数据、release_address_space仍只释放UAS、find的两版/clone与adopt委托均正确；exec和exit旧释放调用已同步改名。
void close_files中return一个void表达式是合法C++，不是语法错误；可直接写调用使意图更清楚。

当前仅有两组服务命名不一致：

| 位置 | 打开 | 全部关闭 |
| --- | --- | --- |
| file_service.hpp声明及Process调用 | open_path | close_all_file |
| file_service.cpp实现 | open_file | close_all_files |

三处统一即可；可按现实现采用open_file/close_all_files，也可保留作者偏好的名字后统一定义，不因纯风格扩大改动。
实际RV64局部语法通过；使用真实Process/FS/模型/服务并丢弃未调用地址空间函数的native局部链接，明确报open_path及close_all_file未定义。
证据`.cache/batch22-process-review-20261115/results.json`与log，六个相关源hash保持，无产品代改/功能或整批P0通过声明。
diff-check当前报告process.cpp三处旧行尾空格，属于局部排版整理，不是新增运行错误。
后续fork/exit关闭文件接线等按原第5节继续，未将尚未写的阶段当作本次Process错误；未提交推送。

## 12. 作者询问剩余工作时的实际状态（2026-11-15）

只读核对：Process/服务、新15—17编号、C dispatcher、sched三薄壳及READ/CLOSE桥接主体已写，Make已加file_service.o，用户API已有声明。
这些是已写的进度，不是整批编译/运行通过声明。当前剩余集中四块：

1. 边界/一致性修正：OPEN直接reinterpret_cast用户path为span，须改为ready/root/长度检查后user_copy_from到固定内核数组；
   原open_path/open_file及close_all_file/close_all_files两组命名仍未统一；READ_MAX目前局部宏，应迁公共syscall头并删除cpp重复定义/无效namespace包裹。
2. 生命周期：fork尚未clone暂存FD/失败close_all/成功adopt；统一exit/fault尚未在切kernel root后close_files。exec只改释放名已完成。
3. 用户汇编包装：user_open/read/close仅头中声明，syscall.S尚无对应入口。
4. 启动与演示：boot仍调用旧configure_file_syscalls；worker尚无新FD读取/显式close及退出自动close演示。

完成后由协作者执行原第8节验收，不追加新测试作业；本次只核对源码状态，没有代写或新增编译/QEMU通过声明。

## 13. OPEN及用户包装/配置接好，接下来生命周期（2026-11-15）

最新实读OPEN已按ready/root/路径长度检查，user_copy_from到真实固定数组，再按实际path_length传Process，之前直接访问用户路径的问题已修。
user/syscall.S的三个新入口均位于.text、GNU-stack注记之前，boot已调用configure_file_service；Make已有service.o。
服务命名两组旧不一致仍在，READ_MAX仍是cpp局部宏（公共头只有READ_FILE_MAX），需按原接口统一。

下一主线为user_task_create.cpp：clone地址成功后clone_file_descriptors到pending_files，分配失败close_all_files撤销引用再discard地址；
私有create_from_frame增加pending表参数，allocate成功后adopt_file_descriptors，保持无失败发布与原现场规则。
user_task_exit.cpp在统一task_destroy切kernel root成功后close_files，再release_address_space；这样正常exit/fault共用，exec继续只释放地址空间。
worker新FD演示仍后续按第6节完成。本轮只实读阶段状态，未改产品或运行新验收。

## 14. 作者授权统一服务命名（2026-11-15）

作者明确要求“这个你改把”，范围为前述两组服务名一致性。协作者按现有实现统一为open_file和close_all_files：
修改file_service.hpp两项声明、process.cpp两处调用，并将作者刚加入fork失败清理中的close_all_file同步为close_all_files。
没有实现/改写其他生命周期逻辑，不修改作者并行改动；生产源码include/kernel/user/test中已无这两个旧标识符。

真实Process/服务/FS/模型的局部native链接与open/close运行通过（fd0、最终资源空），相关Process/service/user_task_create目标语法无诊断。
证据`.cache/batch22-name-fix-20261115/results.json`及log，原未定义引用已消除。
局部测试丢弃未用地址空间方法并提供正常宿主panic桩，不冒充整批B22/用户态/QEMU验证。
其他接线与验收仍按原任务继续，无提交推送。

## 15. 接线集中审查：三处运行问题（2026-11-15）

当前`make -j4`通过。实读与独立只读审查确认，编译通过仍有三处运行问题，交作者集中修正：

1. boot删除了长期`g_builtin_fs`对象，原初始化/绑定被两次`file_service_ready()`查询替代，全生产源码无configure调用。
   恢复匿名namespace中的constinit FS对象，在files数组后依次init(files)、configure_file_service(g_builtin_fs)，再lookup；删除后面的重复ready查询。
   服务的g_filesystem只是借用指针，不能替代实际FS对象及初始化；目前启动会在第一次ready检查失败。
2. task_create_user_from_frame连续两次adopt(candidate)。第一次已清空candidate，第二次覆盖孩子地址空间造成失去归属。
   只保留一次地址空间adopt，再adopt_file_descriptors；其他父ID/子a0规则保持。
3. worker丢弃首次open返回值、把第二次open也判断为0，读取仍走旧READ_FILE14。
   保留原14演示，另加保存两fd的新READ16演示：同fd读7+11并比字节、另一fd从头读7；显式关闭第二fd，首fd留退出自动关闭。
   不硬编码fd0/1，以允许exec保留既有FD；新日志沿用第6节约定。

OPEN复制、READ完整拷出后commit、完整fd范围、fork pending继承/allocate失败清引用与候选、统一exit/fault切根后close_files再释放地址、exec保留文件均已正确接线。
助手仅维护宿主迁移/必要测试，不代写上述产品修正；本节不新增原第8节以外的验收条件，未运行已知错误启动的QEMU或宣称整批通过。

宿主迁移已完成：真实file_service加入host target，旧配置测试迁为configure_file_service，保留未初始化拒绝/重试/重复绑定保持。
新增test_process_files.cpp四组：FD阻止reset并保留元数据；clone/adopt共享及非空输出拒绝/撤销；真实私有UAS释放保留FD；真实映射typed OPEN/READ/CLOSE与失败不推进offset、重试/EOF/零长。
`make test`全套通过（104个run_case，CTest 1/1），94个正式源前后hash保持；证据`.cache/batch22-final-20261115/host/results.json`、build-test.log、LastTest.log。
只改既定test文件，不替代尚待修正后的真实U-mode/QEMU验收，未跑500压力。

## 16. 正式QEMU复核：worker旧片段仍需删除（2026-11-15）

作者报告timeout终止。源码确认boot已恢复长期对象/init/configure，fork只adopt一次；worker新FD段已加，但旧READ_FILE14段前后原两次open及close未删，第二次open仍要求fd0。
实际`timeout 25s make qemu`中FS三项启动日志正常，U0退出0，U1创建ID4后打印`open file failed`，ID4退出1，父ID3打印`unexpected child exit result`后退出1；未到新FD成功日志。
8秒观察只到spawn/yield，不能据timeout提示判通过。原始日志`.cache/batch22-worker-review-20261115/qemu-25s.log`（另有8秒qemu.log）。
下一步仅删worker旧段的两块user_open检查及user_close(fd_2)检查，保留两次user_read_file与字节比较、新FD段；正式源码未代改，余下原P0待正常路径修复后继续。

## 17. 正常演示修复后复验进行中（2026-11-15）

作者已删worker旧open/close残留。报告日志两次`[worker] file read OK`、`[worker] fd read OK`与`fresh image OK`，ID4退出0x17（23）、ID5退出0x25（37），父ID3退出0，原fork snapshot/isolation日志正常。
正式源码检查与作者日志相符；不是据timeout提示判通过。协作者按原第8节分别复验正常资源账本、真实U-mode边界、fork/exec/退出文件生命周期，不新增矩阵或作者作业。

目标构建与接口检查已完成：make -j4、nm无未定义，六个C++接口头各自重复包含在RV64/native通过，四个共用接口头C11通过，constinit/禁复制移动检查通过，94正式源hash保持。
证据`.cache/batch22-final-20261115/build/results.json`。所有权测试最初漏include完整OpenFilePool定义，已仅修测试fixture显式include open_file.hpp后重验；产品头的前置声明正确，未改产品。

## 18. 功能及全部既定P0通过，进入理解收尾（2026-11-15）

统一证据`.cache/batch22-final-20261115/summary.json`，各阶段保留脚本、实际日志、断言结果与正式源hash；94个正式源码/构建配置保持当前版本。

| 验证 | 实际结果 |
| --- | --- |
| 目标构建/接口 | make无诊断、nm无未定义；六C++头RV/native自包含重复、四C11头、constinit及禁复制移动通过 |
| 宿主 | 当前make test 104个具体用例/CTest1通过；真实服务/Process/模型、资源空/reset保持、继承移交与真实映射copy通过 |
| 正式正常QEMU | 34项通过；新OPEN4/READ6/copy_to6/显式CLOSE2/exit自动close2、最终服务池0，文件操作PMM0 |
| 原正常行为 | 旧14四次7/11、boot FS/FD模型、双worker退出23/37父0保持；五空间各7页，35free/运行21alloc/净14，三次reap零PMM及末尾两S ticks |
| U-mode边界 | OPEN12/READ14/CLOSE7共33次，19次拒绝；非法fd完整宽度/路径/长度/只读目标、EOF/零长通过；所有文件操作零PMM，拒绝时表项/引用/位置保持 |
| 跨页失败重试 | 0x40004ffd请求7字节，写入有效前三字节Tin后返回-1；共享refs2、offset仍0；合法重试读TinyOS空格并推进7，父继续读11到18 |
| 生命周期QEMU | 23个阶段标记各一次；真实fork共享、父close后子可读、SPAWN空表、EXEC保留/未知EXEC拒绝、正常与fault关闭、reap后复用空表、满槽失败回滚全部通过 |
| 生命周期账本 | 7份成功镜像与1份失败候选，各7页，共56free/运行42alloc/净14、5次reap零PMM、20次文件syscall零PMM、最终池0及两S ticks |

宿主最终复验位于host-current：最初host通过后作者清理process.cpp行尾空白，助手发现hash变化，确认仅排版后以最新模块重跑；104用例保持通过、源hash无漂移。先前host证据保留。
normal/boundaries/lifecycle仅在隔离副本添加观察器及相应用户测试负载；正式正常demo未替换。私有池只用隔离同TU只读观察，无生产getter/reset钩子，无产品逻辑修补。
生命周期的满槽拒绝用当次trap内临时占用两个UNUSED槽state、调用真实clone/retain/allocate/回滚，再完整恢复的fixture；核对nextID入/拒绝点均8、元数据/父引用与位置保持，不声称自然活任务已占满的负载。
QEMU分别由runner在25/35/55秒外部停止，退出提示不是通过依据；全部通过以实际断言/字节/资源账本为准。P1 OOM/孤儿矩阵和P2 500轮压力未增加，不影响原P0完成。

### 功能梳理

1. boot长期BuiltinFileSystem拥有稳定文件记录，借用静态名字/内嵌字节；file_service一次绑定该实例，并独自管理系统OpenFilePool。
2. 每个Process拥有自己的FD表；fd只在该进程内解释。独立open产生独立OpenFile及offset，fork的两张表可引用同一OpenFile并共享offset与引用计数。
3. U-mode经15/16/17进入syscall，scheduler薄壳检查当前USER/SIE后显式传Process到文件边界。OPEN先复制用户路径；READ按fd读取到内核暂存区、按实际数量拷出，完整成功才commit共享位置。
4. fork在分配Task前准备地址空间和pending引用，失败撤销两份候选，成功无失败移交。EXEC只替换地址空间并保留FD。统一exit/fault切内核根后close_files再释放地址；reap/reset只接已经清空的资源。

原第9节三题的原答、集中澄清及协作者整理修正版已在第19节归档，不要求重答。未提交推送。

## 19. 原三题回答与集中澄清（2026-11-15）

### 作者原答（保留原文）

> 因为进程也是文件，也会借用文件，需要一个表来描述文件，另一个process拿到相同的fd能访问，因为只是reference+1
>
> fork准备好了文件的引用，但是失败分配，那就是直接fs::close_all_files(pending_files)，因为task很金贵，很少，先把东西准备好再塞进去task里更好
>
> 进程用户空间和文件是两个对象，分别负责往哪里存和从何处取，不能混在一起，复制失败的时候推进offset会影响下一次的复制

### 集中澄清

1. **进程与文件、fd与引用不能等同。** TinyOS的Process拥有FD表，是文件资源的使用者，不是BuiltinFile或OpenFile。
   系统池管理一次打开的对象及其引用数/位置；进程表把本进程的小整数fd映射到这些对象，关闭时撤销本方引用。
   相同fd数字只会查各自的表：A的fd0可指向X，B的fd0可指向Y或为空。传递数字不会自动retain，也不能借此访问A的表。
   fork显式inherit表项时，孩子同一fd才会指向父亲的同一OpenFile并将其引用数加一；独立open同一文件仍产生独立OpenFile和offset。
2. **文件回滚方向正确，还要撤销地址空间候选。** allocate失败时先close_all_files(pending_files)，再discard_fork_candidate(candidate)，
   归还已复制的内容页、私有根和下级表；父空间/表/引用/位置保持，公开ID不消耗。
   Task容量有限是背景，核心约束是事务：全部可失败准备放在allocate前，成功分配后只做不会失败的移交与现场初始化，
   不必再处理半成品Task及其公开ID的撤销。
3. **资源职责与offset失败保持理解正确，需要落到exec语义。** exec替换旧地址空间时仍然是同一个Process，现有FD应继续有效；
   若release_address_space同时close_files，exec会意外丢掉全部打开文件。正常exit/fault才在切内核根后显式关闭本进程引用并释放地址。
   拷出失败却推进offset会让下一次读取跳过尚未成功交付的数据，也会改变其他共享者的读取位置。
   offset不推进不等于buffer保持不变：跨页失败可以已写入有效前缀，本批真实探针已验证。

### 协作者整理的修正版（非作者重答）

1. 系统池统一管理OpenFile；每个Process的FD表保存本进程的引用关系。相同fd不代表相同打开对象；fork继承才显式共享对象并retain。
2. fork分配Task失败，关闭pending文件引用并释放candidate地址空间；将可失败准备放在提交前，分配成功后只无失败移交，父资源和下一ID保持。
3. release_address_space只释放地址空间，使exec能保留FD；exit/fault另关引用。读数据完整拷出后才提交offset，失败不推进但可能改动buffer前缀。

三题已一次集中说明并归档，无追加问题或重答要求。功能与全部既定P0结果保持，本批收尾；本轮仅更新文档/状态，不改产品、重复测试或提交推送。
