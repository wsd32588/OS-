# BATCH28：标准输入输出与新进程资源

日期：2026-11-17。状态：功能、全部既定P0与原三题原答/集中澄清归档完成，本批收尾，见第14—16节。
B27功能、全部既定P0与三题归档已经完成，保持收尾；本批在其基础上推进。

## 1. 目标、范围与可观察结果

用户程序进入main时，user模式的新进程已经具有标准FD，无须自行open控制台：

| FD/共享常量 | 用途 | OpenFile访问模式 | 初始对象与引用 |
| --- | --- | --- | --- |
| 0 / TINYOS_STDIN_FILENO | 标准输入stdin | ReadOnly | Console对象A，ref1 |
| 1 / TINYOS_STDOUT_FILENO | 正常输出stdout | WriteOnly | Console对象B，ref1 |
| 2 / TINYOS_STDERR_FILENO | 错误输出stderr | WriteOnly | Console对象C，ref1 |

采用三个独立OpenFile，复用现有40项池和每进程8项FD表，不扩大容量。
三者访问同一个长期ConsoleDevice；stdout与stderr当前输出到同一UART。
这次建立的是FD约定及打开权限，不添加C库FILE、printf、缓冲层或行规程。

作者已明确锁定：**SPAWN创建全新资源，不继承父FD表。**
user模式下，“全新”包含新的用户地址空间和三项标准打开对象；父进程的普通文件、
打开位置及关闭形成的空洞不复制到SPAWN孩子。设备和输入流继续共享，不清公共队列。

正常可观察结果沿用B27小echo：ready消息后，x/NUL/ff原样回显；小写q只触发bye并返回0。
程序直接read(0)/write_fd(1)，错误诊断尽力写2；正常退出由现有exit路径关闭三项引用。

本批保持现有18号write_fd、16号read及256字节上限，无新syscall、open flags或汇编包装。
旧WRITE5/PUTCHAR1保持兼容。dup/dup2、重定向、管道、阻塞read、IRQ、前台进程组、
行编辑、路径exec和shell继续后续分批安排，均不是本批验收条件。

## 2. FD号码与生命周期

0/1/2在创建时具有约定绑定；读写许可由当前OpenFile决定，不能按FD数字硬编码。
用户可以close任何标准FD，关闭后不自动补回。open继续取最小空槽，不永久跳过0—2。
例如三项都在时open普通文件返回3；close(0)后再open普通文件可以返回0，随后按该文件读取。

| 路径 | 本批行为 |
| --- | --- |
| user模式boot新建 | 全新0/1/2，各独立Console对象/ref1 |
| user模式SPAWN，新旧编号入口相同 | 全新0/1/2，不继承父FD；普通文件与父空洞不带入 |
| FORK | 保留父FD数字、空洞和权限，指向相同OpenFile并retain；不重新安装标准流 |
| EXEC | 同Process原样保留FD及空洞；成功和准备失败都不补齐0/1/2 |
| EXIT / 用户FAULT | 沿用统一close_files；共享引用未归零时其他进程仍可使用 |
| kdbg模式创建 | 保持空FD表，不安装用户控制台；原独立进程回归保持此模式 |
| 单独构造Process{} | 仍是空表，构造函数不隐式访问服务或分配资源 |

关闭最后一个Console打开对象只清池槽，不销毁设备、不清待输入、不修改服务配置。
多个进程有stdin只表示可以竞争同一输入流，不建立各自键盘队列或输入归属路由。
并发约束仍为B27的user syscall整段SIE0、无主动调度、无后台producer；不新增锁。

## 3. 改动范围与共享常量

作者实现涉及以下文件；同一批整批完成后集中审查，不做逐行闯关：

| 文件 | 内容 |
| --- | --- |
| 新include/fs/standard_streams.h | C/C++共用的0/1/2常量 |
| include/fs/open_file.hpp、kernel/fs/open_file.cpp | 访问模式、能力查询及底层守卫 |
| include/fs/file_descriptor_table.hpp、kernel/fs/file_descriptor_table.cpp | 带模式打开控制台、空表标准流事务 |
| include/fs/file_service.hpp、kernel/fs/file_service.cpp | 私有池上的标准流准备服务 |
| kernel/fs/file_syscall.cpp | 在副作用前检查打开权限 |
| kernel/task/user_task_create.cpp | fresh创建前准备、失败撤销、成功移交 |
| user/user_api.h、user/main.c | 包含常量、直接使用标准FD的echo |

标准流常量头只需要include guard和三个数字宏：

```c
#ifndef TINYOS_FS_STANDARD_STREAMS_H
#define TINYOS_FS_STANDARD_STREAMS_H

#define TINYOS_STDIN_FILENO  0
#define TINYOS_STDOUT_FILENO 1
#define TINYOS_STDERR_FILENO 2

#endif
```

user_api.h包含此头；FD表实现需要这些数字时也包含同一头，不重复定义。
无需增加.o或修改Makefile对象清单；保留user/syscall.S和现有C ABI签名。
不新增Process初始化方法、公开池getter、全局设备getter或通用固定FD安装API。
Process已有adopt_file_descriptors足够完成移交。

## 4. OpenFile的访问模式

在tinyos::fs中增加以下类型和接口，原kind/file/offset/ref语义保持：

```cpp
enum class OpenFileAccess {
    ReadOnly,
    WriteOnly,
    ReadWrite,
};

// OpenFile的新增public查询
OpenFileAccess access() const noexcept;
bool can_read() const noexcept;
bool can_write() const noexcept;

// OpenFile的新增private状态
OpenFileAccess access_{OpenFileAccess::ReadOnly};

// OpenFilePool：替换原无参声明；默认参数只写在头中
OpenFile* acquire_console(
    OpenFileAccess access = OpenFileAccess::ReadWrite) noexcept;
```

| 对象 | can_read | can_write |
| --- | --- | --- |
| None，包括默认构造和最后close后 | false | false |
| Builtin，acquire时固定ReadOnly | true | false |
| Console / ReadOnly | true | false |
| Console / WriteOnly | false | true |
| Console / ReadWrite | true | true |

能力查询同时检查有效kind与访问模式，不能让None因默认ReadOnly变成可读。
acquire_console先拒绝非法enum值及设备未ready，再找空槽，成功记录模式并发布kind/ref。
原无参调用保持ReadWrite，确保B27普通/dev/console打开及已有测试继续双向使用。
Builtin的acquire显式设置ReadOnly；最后release恢复access至ReadOnly，kind清None。
retain及非最后release保持模式，引用数仍代表指向该打开对象的FD数量。

底层read_current和commit_read先检查can_read，write先检查can_write。
无权限时返回-1，包含空span和commit_read(0)，不触设备/位置/队列。
有权限后再沿用原Builtin/Console分派、实际数量和零长语义。
kind决定对象后端，access决定允许操作；Console的file()仍合法为nullptr。

## 5. FD表与文件服务：完整准备后移交

FileDescriptorTable新增/调整：

```cpp
int open_console(
    OpenFilePool& pool,
    OpenFileAccess access = OpenFileAccess::ReadWrite) noexcept;
int initialize_standard_streams(OpenFilePool& pool) noexcept;
```

file_descriptor_table.hpp直接包含open_file.hpp，让默认枚举值自包含可见。
open_console保持最小空槽算法，将模式传给pool.acquire_console；失败返回-1且不占FD。

initialize_standard_streams返回0成功/-1失败，协议为：

1. 只接受完全空的目标表，非空直接拒绝并保持原表。
2. 局部FileDescriptorTable pending{}拥有准备中的引用，依次open_console三次，
   权限ReadOnly/WriteOnly/WriteOnly，返回FD应分别为共享常量0/1/2。
3. 任一次失败，pending.close_all(pool)撤销已经取得的引用后返回-1；目标保持空。
4. 三次全部成功，目标adopt(pending)，pending变空，返回0。

不要先把0写入目标、第二项失败后留下半成品；不需要为此提供任意FD替换/dup接口。
若池只剩两槽，第三次分配失败必须撤销前两项；原有活对象的字段、引用和池占用保持。
不要求刚申请又释放的原空槽逐字节恢复，只要求重新空闲且无资源遗留。
初始化及失败撤销不poll、不输出、不清设备输入、不发生PMM分配/释放。

file_service.hpp/.cpp新增：

```cpp
int prepare_standard_streams(FileDescriptorTable& output) noexcept;
```

先检查file_service_ready和console_user_io_enabled；拒绝时保持output。
随后委托output.initialize_standard_streams(g_open_files)，私有池仍只在服务实现中可见。
kdbg模式直接调用此服务返回-1；不为测试增加重绑/reset入口。
普通open_file路径保持：Builtin只读，精确/dev/console使用默认ReadWrite。

## 6. READ/WRITE：权限必须先于副作用

syscall_read保留现有root、服务、FD完整宽度及长度上限检查，找到OpenFile后，
在合法零长返回和console poll之前拒绝!file->can_read()。
之后继续现有模式门槛、零长0、正长度poll→peek→copy_to→成功commit。

syscall_write_fd同样在copy_from及输出之前拒绝!file->can_write()，原Console种类、
user模式、FD/长度/root检查保持；完整复制到内核后只输出请求的实际length。
底层对象守卫仍需要保留：syscall的提前检查额外保护poll/用户内存复制等副作用。

| 调用条件 | 返回与副作用 |
| --- | --- |
| read(0,坏地址,0)，标准项仍有效 | 0，无poll/复制/消费 |
| write_fd(1或2,坏地址,0)，标准项仍有效 | 0，无复制/输出回调 |
| read(1或2,...,任意合法长度含0) | -1，不poll、不碰用户目标、不消费 |
| write_fd(0,...,任意合法长度含0) | -1，不读用户源、不输出 |
| 关闭/非法FD或长度超过256 | -1，不因零长放宽FD有效性 |
| 正长度合法stdin暂空 | -2，用户yield重试；仍非EOF |
| 普通文件末尾 | 0，保留原语义 |

复制失败契约保持B27：read失败不commit，用户目标可已有前缀；write完整copy_from失败时零输出。
旧WRITE5/PUTCHAR1依旧直接输出，不将其解释为stdout包装或访问权限隔离机制。

## 7. fresh创建事务：boot与SPAWN共用

只在user_task::create中接入，原公开task_create_user和SPAWN签名不变。
新增console/console_service.hpp依赖；原fs/file_service.hpp已经包含。

```text
原entry/stack/root/内容页等全部前置检查
→ 局部pending_files空表
→ 若console_user_io_enabled()：prepare_standard_streams(pending_files)
→ TaskTable::allocate
→ 填初始frame、绑定同槽Process
→ adopt地址空间、adopt_file_descriptors(pending_files)、设置USER/parent
→ 返回新公开ID
```

安装条件使用现有运行时console_user_io_enabled()；该查询包含设备ready及UserProcesses模式。
生产boot已经要求console配置成功才进入创建，不新增第二套模式状态或编译宏分支。
kdbg创建时pending_files保持空表，沿用原空FD行为。

- 标准流准备失败：-1，source地址空间保持，Task/ID/count无变化；临时引用由准备层撤销。
- allocate失败：close_all_files(pending_files)后-1；source仍由原boot/SPAWN调用者清理。
- allocate成功后：只做原有无失败发布和两个adopt，不再open/retain或添加其他可失败操作。
  pending引用转给Process且来源清空，不能成功后再close pending所移交的资源。
- 保留source“失败不动、成功移交清空”语义；不要把地址空间清理与临时FD清理交错丢失归属。

fork已有独立的task_create_user_from_frame和pending继承，不调用fresh初始化。
exec仍只替换地址空间，exit/fault仍统一关files；这些路径仅核对，不顺带改写。
SPAWN8/12都经同一fresh创建入口，自然得到新三项，不复制调用者表。

## 8. 用户main：直接使用0/1，保留简单echo

删除本应用的/dev/console路径数组、user_open及用于该独立FD的close cleanup。
静态可写message[]保留原28字节ready文本，保证当前受限ELF仍有可写数据段；
goodbye保留原14字节文本，不放宽loader/user.ld约束。

- banner和普通字节回显使用user_write_fd(TINYOS_STDOUT_FILENO,...)并核对实际返回长度。
- 单字节输入使用user_read(TINYOS_STDIN_FILENO,...)；-2后检查yield成功并继续，其他非1为失败。
- q不回显，完整输出bye后return0，三项由现有user_exit→close_files清理。
- 失败路径向TINYOS_STDERR_FILENO尽力输出一次`[console] I/O failed\n`（21字节）后return1。
  若忽略这次诊断write返回值，注释说明错误路径尽力输出、不再递归报错。

不把fork/SPAWN/exec自测塞回main，不修改worker或旧回归payload。
标准流可关闭/复用、错误方向和生命周期的测试由助手在独立测试中完成。

## 9. 实现顺序与命令

1. 建共享常量头，完成OpenFileAccess、查询、acquire/release和底层守卫。
2. 完成带模式open_console、空表三项准备及服务接口。
3. 加syscall权限前置检查，在fresh create接准备/allocate失败清理/成功移交。
4. main改用标准FD，整体编译并分别观察user/kdbg。
5. 作者整批完成后，助手一次集中列全问题、补既定验证，修正后按受影响范围复验。

```sh
make -j4
make test
make CONSOLE_MODE=user qemu
make CONSOLE_MODE=kdbg qemu
```

QEMU退出Ctrl+A后X；无需clean。timeout只结束观察，不能单凭返回124判定功能通过。
助手回归入口仍为make CONSOLE_MODE=user test-process，其内部固定kdbg。

## 10. 固定验收：P0由助手承担

下列P0是本批提前确定的完整范围，不要求作者新增测试框架。已有B27测试保留，
通过当前make test检查兼容；不重开其历史完整故障矩阵或另加500轮QEMU压力。

| 优先级 | 场景与完整预期 | 方法/学习点与作者新增测试代码 |
| --- | --- | --- |
| P0 | 两种profile完整构建/nm无未解析；新共享头C11/C++20及三接口头RV/native自包含/重复包含、默认参数与constinit有效；原host保持 | 助手编译/既有suite；作者0行 |
| P0 | 标准表0/1/2为三不同Console对象，各ref1/offset0/file空，权限R/W/W；再open about得3；独立新表得到新对象；已有/dev/console无参仍RW | 真实pool/表宿主；打开对象身份；作者0行 |
| P0 | 空对象能力false；错方向的read_current/commit_read/write含0都-1；标准read/write错方向含0在poll/copy前拒绝，输入/目标/输出保持；合法0长静默，1/2各输出实际二进制长度 | 真实syscall与映射宿主；权限/副作用；作者0行 |
| P0 | 非空目标初始化-1且原引用保持；池仅剩两槽时第三项失败，撤销前两项，目标空/旧活对象保持/池占用恢复；close后可重试。未ready及kdbg服务准备拒绝保持 | 有限宿主fixture；事务回滚；作者0行 |
| P0 | 关闭0后open about得0并可读，关闭标准流不补回、不清公共待输入；fork式inherit保留模式和空洞/共享refs，adopt移交不增refs、全关池空；全部文件操作零PMM | 宿主真实引用与队列；数字和绑定分离；作者0行 |
| P0 | 正式user main无需open即三项有效，28字节banner、x/NUL/ff精确回显、q仅14字节bye；暂空真实yield后再读，输入事务SIE0/无A，退出0自动关三项后池0/Process空/两S ticks | 真实QEMU正常路径；作者仅第8节echo |
| P0 | 独立小型用户负载验证stderr真实输出及错方向含零长-1/零副作用；同一隔离生命周期中SPAWN新三项且不带父普通文件/空洞，fork共享含空洞，exec保留含空洞，孩子及根退出后池0 | 助手独立payload和只读观察，不改正式main/worker；作者0行 |
| P0 | 一次受控满Task槽fixture：实际fresh create已准备三引用、allocate失败后撤销；source候选、原任务/ID/high_water/既有引用保持，调用者可清候选；fixture恢复后重试成功且ID未消耗 | 隔离QEMU受控fixture，注明非自然满载，fixture账本另核；作者0行 |
| P0 | 默认kdbg原五命令/安静及至少两S timer保持；当前test-process原退出0/23/37/0、35free/21运行alloc/净14/reap3/最终池0保持，外层user参数不污染内部kdbg | 助手各一次必要回归；作者0行 |
| P1 | 预测父close(2)后分别SPAWN/FORK/EXEC的fd2与引用变化；可口头讨论 | 生命周期学习点；0行，不阻塞 |
| P2 | 设计将来dup2实现重定向时的引用转移；只讨论接口也可 | 后续专题；本批0行，不实施 |

生命周期隔离观察以父先打开about(fd3)、关闭2为例：fork孩子共享0/1/3、2仍空；
exec后仍如此；SPAWN孩子却有全新0/1/2、3空。原始三对象均ref1；该父先开about再关闭2后，
池占用为3；单独fork不增加对象数，SPAWN若与该父同时存活则对象数增加3。
观察指针/引用/权限使用只读探针，不给产品加getter、重绑或临时修补算法。
新增标准FD准备/读写/关闭自身零PMM；进程MM按实际镜像核账，不把fork/exec自身分配算文件开销。
正式小镜像当前为6私有页，旧test-process的BSS镜像仍每空间7页/总35次free；
新隔离payload必须按实际内容页/表页记精确数，不能机械套旧常量。

## 11. 功能与P0之后的原三题

1. 为什么0/1/2可以指向同一设备，却选三个OpenFile？close(0)后open about返回0，
   内核凭什么决定能否读写？最后close为何不能清设备输入？
2. 父进程有0/1、关闭了2、另有普通文件3时，SPAWN、FORK、EXEC分别得到什么FD？
   哪些是新对象、哪些增加引用、哪些保持现表？
3. 为什么三项准备必须放在allocate前？第三项失败与Task槽满分别由谁关闭临时引用、
   谁仍拥有地址候选，如何保证失败后重试不丢ID或资源？

实现/P0完成后一次梳理相关路径，再集中答这三题，不提前加题或要求反复重答。
可选配套阅读：《操作系统：设计与实现》第三版第5章5.1“文件”，
带着“进程FD如何关联打开对象、为何程序依赖标准入口”阅读，再对照本批表/池/设备三层。
不套用未经核对的中文页码，也不把阅读作为继续编码的门槛。

## 12. 准备验证与当前限制

独立只读设计审查已核对访问模式、三项事务、fresh/fork/exec边界与旧kdbg回归兼容。
.cache/batch28-preparation-20261117/results.json记录7项拟定声明检查：RV64/native C++20、
C11用户常量/API，以及三接口头自包含/重复包含，含默认无参兼容与constinit实例，均无诊断。
这是缓存中的声明与调用形状检查，不含B28算法实现、完整链接或新功能运行验收。
107个正式源/配置文件哈希保持；本轮仅任务文档/索引/记忆及忽略的准备缓存。

下一步由作者从第4节OpenFileAccess开始，按第9节整批完成；助手集中审查并承担第10节固定P0。
当前不宣称B28功能已实现或通过；不提交推送、不批量重构，不重复B27已完成的三题与验收。
本节为准备阶段历史，当前状态以第14—15节为准。

## 13. 主体接线已修，OpenFile权限集中待修（2026-11-17）

作者已修create：页检查后仅user模式准备临时表、allocate失败关闭pending、成功移交FD。
main已直接read0/write1，统一错误出口尽力write2并返回1；service误用TRACE门槛已删除，
三项权限参数R/W/W和初始化成功return0已补。上述接线静态正确，不再重复作为待修。
实际采用standard_stream.h与initialize_standard_stream单数，声明/定义/调用一致，
本批接受该命名，无须为纯命名差异重命名文件或接口。

当前集中待修均在kernel/fs/open_file.cpp：

| 位置 | 问题与修正 |
| --- | --- |
| can_read / can_write | 当前只比较access，默认None也会can_read=true；须同时检查有效kind和对应允许模式，None两者false |
| commit_read | 误检查can_write，必须改can_read；提交读取仍属于读操作，包含count0 |
| write | 当前只有Console种类检查，须在调用设备前增加can_write守卫，包含空span |
| acquire_console | 已验证access但未保存参数；取得槽后设置entry.access_=access，保留ref1/offset0/file空/kindConsole |

作者刚运行的test/build/cmake/Testing/Temporary/LastTest.log显示三项CTest失败：
主host在五处OpenFile提交位置返回-1而预期0；kdbg/user两独立console组均先在提交返回-1，
随后真实syscall_read的“成功复制后commit失败”内部不变量触发host panic/abort。
这与commit_read误查写权限、Console未保存模式相符，不是CTest本身异常。
正常Builtin明确只读，所以错误can_write守卫直接拒绝其commit；当前Console也都保持默认只读。

独立实际源码检查11项均通过：四头各RV64/native自包含重复包含、五个实现RV64语法、
main的C11 RV64/native语法；证据.cache/batch28-review-20261117/interfaces/results.json。
所查11个产品文件哈希保持；助手只读核对作者当前失败日志，未重复make test或运行已知失败的QEMU，
未代改产品算法。编译通过不代表本批P0通过；下一步作者集中修本节四项，再续原定固定验证。
本节四项随后均已由作者修复，当前最终实现与全部既定P0结果见第14—15节。

## 14. 最终实现与完整执行路径（2026-11-17）

作者已经保存四项修正：能力查询拒None，commit_read检查can_read，write先检查can_write，
acquire_console保存access参数。当前集中审查无新的功能阻塞。
仅以“不是禁止模式”判断方向在现有分配入口枚举合法性约束下成立，不强制改写为重复白名单。
实际共享头standard_stream.h及初始化函数initialize_standard_stream采用单数，调用一致，保留作者命名。

1. boot先配置长期文件/控制台服务，再创建任务。user模式fresh create完成地址/代码栈页预检后，
   用局部FD表准备三个独立Console打开对象R/W/W；全部成功才allocate并把地址与FD移交Process。
   kdbg模式跳过准备，创建行为仍为空表；Process默认构造也不分配标准流。
2. 用户main直接使用stdin0/stdout1，暂空-2时真实yield重试；q输出bye后返回0。
   main出错时尽力向stderr2输出一次诊断，返回1；用户启动代码统一调用exit。
3. read先验证对象和读取能力，合法零长不poll；正长度SIE0内poll→peek→copy_to→成功commit。
   commit属于读流程，不能按写权限放行。write_fd先检查写能力，再完整copy_from后输出实际长度；
   底层对象也检查能力，错方向含零长均拒绝，空对象没有任何读写能力。
4. 父先open about得3再close2时，fork继承0/1/3并retain、保留hole2及普通文件共享offset；
   exec只替换地址空间，继续保留这些对象/权限/空洞；SPAWN重新取得独立0/1/2，不带父fd3。
   新打开对象与父对象独立，长期控制台及输入流仍公共，不因fresh而重置设备。
5. FD准备失败关闭准备中的引用，allocate失败也关闭pending；source候选仍归原调用者。
   allocate成功后只有无失败移交。close释放引用/保留公共输入，号码可被后续普通open复用。
   exit/fault统一close_files，最后引用使池槽可复用，随后按原切根顺序释放地址空间。

默认kdbg与用户输入仍互斥，无异步producer；本批不改变单hart/SIE0事务边界。
stderr当前仍输出到同一UART，没有独立错误设备；旧WRITE5/PUTCHAR继续保留原直接输出语义。

## 15. 全部既定P0验证（2026-11-17）

统一证据.cache/batch28-final-20261117/summary.json，all_passed=true。
108个正式源/配置在构建、两真实profile、生命周期、旧回归与当前工作区保持一致。
助手仅增加console_fd宿主用例与说明、隔离观察器/测试payload及文档，未代改作者产品算法。

| 项目 | 实际结果与证据 |
| --- | --- |
| 构建与接口 | build/results.json全部33项通过：15项实际头/实现/C11/constinit/默认参数检查、user/kdbg完整构建无诊断/nm无未解析；root最终kdbg/trace0 |
| 宿主 | make test三项CTest全过，原130+新7=137个具体用例，host.log与host-cases.log；原120组保持，新标准流组复用既有独立kdbg/user进程，各服务只绑定一次 |
| 标准表与权限 | 两模式各3组：三个独立R/W/W对象/ref1/offset0/file空，None及末次close能力false，非法mode拒绝，两个fresh表独立；普通open得3/旧无参console仍RW，stdin底层write与stdout/stderr底层read/commit含零长拒绝 |
| 准备与引用事务 | 非空目标拒绝保持；真实池仅余两槽时第三项失败，两临时引用完整撤销、38个旧对象/引用/位置保持；释放一槽后retry成功；继承空洞/ref+1、adopt不增ref、fresh不带普通文件、号码0可改绑普通文件、最后close保留原R字节，全部零PMM |
| 标准syscall宿主 | user新增1组：错方向含0拒绝且不poll/不改目标或输出；合法零长不读坏地址，后续stdin仍获原S；stdout1字节/stderr2字节精确保留X/NUL/ff；真实映射fixture全部回收 |
| 正式user QEMU | user/results.json全部19项：正式main无OPEN/CLOSE，三个R/W/W对象/ref1/池3；read全fd0、write全fd1，输出28/1/1/1/14与实际一致；x/NUL/ff精确、q仅bye，11226次-2对应11226次真实yield；SIE/frame/root错误0 |
| 正式user退出 | EXIT0自动release三项打开引用，最终池0/Process空；实际6个唯一私有页在kernel root下全部回收、根最后、available+6，随后两idle S中断；应用RX438/RW29，仍3内容+2下级表+根 |
| 默认kdbg QEMU | console/results.json全部36项：原五命令精确、55字节输入链、运行PMM0/池0、后续空poll和至少两S中断且串口安静，无用户输入争抢 |
| 独立生命周期QEMU | lifecycle/results.json全部21项：真实fork1次/exec1次/SPAWN2次，父0/1/3与hole2正确共享/保持；fork与exec继续共享普通offset，两个SPAWN均新R/W/W各ref1、fd3空；stderr真实输出E；6次错方向含零长均-1，poll/copy_to+copy_from/UART增量全0 |
| 生命周期/fixture账本 | 29次文件syscall零PMM且同frame/sepc+4/root/SIE0；独立未运行TaskTable先占满，真实create在allocate前观察到池+3，失败后引用撤销/候选及表字节/生产全局元数据保持；释放局部槽后同候选retry localID5移交三项，切kernel清6页后回caller root，fixture alloc6/free6单独核账 |
| 生命周期最终资源 | 包含fixture共6份各6页空间，36次唯一合法free/6次根最后；运行alloc30/free36净恢复boot6，3次reap、四次退出51/50/70/0，最终pool0/root Process空/两idle S中断 |
| 旧进程回归 | make CONSOLE_MODE=user test-process内部仍kdbg/trace1；.cache/process-regression/20261117-220709-123/results.json全部27项，旧退出0/23/37/0、35唯一free/21运行alloc/净14/reap3零PMM/池0/末两S保持 |

宿主的坏源写入拒绝本身不能证明没有调用copy_from；该调用顺序先由源码确认，
生命周期QEMU另以真实复制/poll/UART计数补足零副作用动态证据，不将黑盒-1当作完整证明。
满槽fixture使用独立静态TaskTable，未被调度；不声称生产全局表的自然满载。
生产全局比较排除活动内核栈字节，只核任务/Process身份、关系、状态、根及FD元数据；
未运行局部表可以逐字节比较。fixture仍调用正式create/allocate和资源实现，没有修补产品算法。

各负载按实际镜像分别核账：当前正式小应用与生命周期测试镜像每份6页，旧保存BSS回归仍7页/空间。
生命周期中的fixture6/6与真实用户空间分开记录，不把测试自身申请误判为文件操作分配。
user观察器首次因Pool别名冲突编译失败，仅修观察器并保留fixture-initial-build-error.log；
生命周期在运行前修正观察器的活栈比较与initializer_list写法，首个完整构建/run通过。
QMP用命名管道只读，满足条件后宿主停止QEMU，非客体主动关机。

本批功能与全部既定P0通过，当前仅待第11节原三题集中理解收尾。
不追加测试矩阵或作者测试作业，不重问B27三题，不提交推送；本轮既有验证通过后不重复整套运行。
原三题随后已在第16节集中澄清归档，本节“待理解收尾”为功能/P0通过时的历史状态。

## 16. 原三题原答与集中澄清（2026-11-17）

### 作者原答（原样保留）

> 因为共享同一个表，共享一个输入输出，复用由传入的文件权限决定
> SPAWN无法直接从父进程得到权限，但是可以继承引用和Openfile，fork得到父进程除了用户空间和task之外的所有内容，exec也得到权限，fd表，task实体
> fork是新建一个task或者直接使用现有task,UserAccessSpace也是自己申请，exec加载新的地址空间和堆栈，spawn是全新的内容
> 标准流失败是直接输出，排出内容挤在输出队列的内容，Task分配失败要清理空间，设置DEAD,然后设置死亡reason等内容输出，地址候选仍然归MMU或者父进程，PID只在资源准备完成再配置

### 集中审查

第一题中“复用由传入的文件权限决定”的方向接近当前实现：准确地说，syscall通过本进程FD表
找到当前OpenFile，再用其access决定许可；当前普通Builtin打开固定只读，没有独立文件权限元数据。
关闭0后再open about所得0访问的是新绑定的普通文件，不能因为数字0就继续按控制台处理。

“共享同一个表”需要分层说明。同一Process的0/1/2确实在同一张FD表里，
但不同Process各自拥有FD表；所有打开对象存放于同一个系统池，系统池也不等同于进程FD表。
user模式fresh初始0/1/2分别指向三个独立Console OpenFile，权限为R/W/W，各ref1。
共同收发源于三个后端都访问同一个长期ConsoleDevice，而不是共享一个OpenFile或跨进程共享FD表。
选择三个打开对象便于分别表达权限和关闭各自引用。最后close只清打开对象；设备仍长期拥有待输入，
InputQueue保存原始输入，当前没有需要排空的下游输出队列。

第二题里“spawn是全新的内容”“exec加载新空间”的方向正确，但SPAWN继承引用的说法
与本批已锁定设计相反。以下明确指user模式；kdbg fresh创建仍为空FD表：

| 操作 | Task/Process/公开ID | 用户空间 | 本题的FD结果 |
| --- | --- | --- | --- |
| SPAWN | 新逻辑Task、新Process资源记录、新ID | 从内置ELF新建 | 新0/1/2，各独立R/W/W对象/ref1；3空，不继承父对象或hole2 |
| FORK | 新逻辑Task、新Process资源记录、新ID | 新根和内容页，复制父运行快照 | 独立FD表保留0/1/3及hole2；这些条目指向父同一OpenFile并ref+1，普通offset共享 |
| EXEC | 保持当前Task/Process/ID | 准备新镜像后替换旧空间/用户栈 | 同一表原样保留0/1/3、hole2、权限和打开位置；不重新取得或retain标准流 |

fork复制全部已记录的用户内容页，含代码、数据和用户栈，因此不能把用户空间内容排除在复制之外。
Task实体和内核栈不从父实体复制；孩子有自己的现场和内核栈，内核共享映射继续借用。
“使用现有Task”只可能指复用此前已reap成UNUSED的静态槽存储，新逻辑任务仍取得新ID；
不能直接让孩子使用父进程正在运行的Task。
exec保留当前内核栈与frame地址，替换用户空间、用户栈和frame内容；它不会“得到一个新Task实体”。

第三题混入了输出排空和进程退出。标准流准备失败时没有新任务成功发布，
回滚只撤销本次取得的打开引用，不输出、不排空/丢弃公共队列、不设置DEAD或退出reason。
两层临时表的清理责任分别为：

1. 第三项open失败：initialize_standard_stream内部pending.close_all(pool)，关闭前两项；
   目标表保持空，向create返回-1，地址source保持。
2. 三项准备完成但allocate失败：create调用close_all_files(pending_files)，关闭三项临时引用；
   source仍完整留给其调用者。SPAWN的process_spawn_builtin负责释放局部candidate，boot也有自己的失败清理；
   此时没有把candidate接管到新Process，更没有修改父进程原有地址空间。
3. 地址候选的归属由创建流程的局部UserAddressSpace描述及移交协议确定。
   MM提供分配/释放机制，MMU是执行地址翻译的硬件，均不能代替具体拥有者承担候选生命周期。

“PID只在资源准备完成再配置”的方向正确；本项目具体为公开task_id：其他可失败准备放在allocate前，
allocate自身失败不推进ID计数或发布任务；allocate成功后只有无失败移交。
创建失败与已有进程exit/fault是两条不同路径，后者才记录结果、标DEAD并等待父收集。

### 协作者整理的修正版（不是作者重答）

1. 每个进程独立拥有FD表；user fresh的0/1/2指向三个独立打开对象，后端共用长期控制台。
   权限由当前OpenFile.access决定，号码可关闭重用；最后close不清设备输入。
2. SPAWN新建Task/空间和标准三项，不继承父FD；fork新建Task和独立复制的用户空间，
   FD表复制引用、保留空洞并共享打开对象；exec保持原Task/ID和FD表，仅替换用户程序执行环境。
3. 第三项失败由初始化函数关内部pending，allocate失败由create关外部pending；
   未转移地址候选仍归创建调用者清理。失败不排队列、不走DEAD/退出记录，未成功allocate不消耗ID。

原答已完整保留，以上为一次集中澄清，不要求作者重答或追加题目。
第15节全部既定P0结果保持；本轮只归档理解与更新索引，无产品修改、重复测试或提交推送。
B28功能、P0与理解归档完成，本批收尾；后续方向另行讨论，不自动交付下一批或代写。
