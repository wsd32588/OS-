# BATCH27：通过FD访问控制台

日期：2026-11-16—17。状态：功能、全部既定P0与原三题原答/集中澄清归档完成，本批收尾，见第14—16节。

## 1. 目标、启动模式与可观察结果

让用户程序显式打开/dev/console，沿用READ16读取，通过新增WRITE_FD18输出，最后CLOSE17关闭。
普通内置文件仍只读。旧WRITE5/user_write(buffer,length)与PUTCHAR1保持兼容，不改变参数解释。
本批不预留标准0/1/2，不做dup/重定向、行规程、管道、路径exec、shell、IRQ或阻塞等待。

```text
user_open("/dev/console") → Process FD表 → 同一打开池的Console对象
user_read(fd,buffer,n) → SIE0内poll一次 → peek → user_copy_to → 成功commit
user_write_fd(fd,buffer,n) → user_copy_from完整成功 → 设备同步输出
user_close(fd) → 释放该FD引用；最后引用仅清打开对象，不关闭公共设备
```

两种明确互斥的启动模式：

| 命令 | 启动与输入归属 |
| --- | --- |
| make qemu / make CONSOLE_MODE=kdbg qemu | 默认原任务A/kdbg；禁止用户打开/dev/console |
| make CONSOLE_MODE=user qemu | 不创建A/kdbg，创建一个DEMO用户程序并登记为reaper；用户read自己轮询UART |

用户模式预期：打印`[console] ready (q to quit)\n`，输入普通字节则原样输出；收到小写q打印`[console] bye\n`并关闭FD/返回0，q本身不回显。
不做退格编辑或CRLF合并，原始NUL/ff也可读写；无输入返回WOULD_BLOCK，用户yield后重试。
默认kdbg的help/cat/hex及安静状态保持，旧fork/exec负载仍在独立test，不恢复到普通启动。

## 2. 控制台输入模式：一次配置与生产/消费上下文

console_service.hpp增加：

```cpp
enum class ConsoleInputMode {
    KernelDebugger,
    UserProcesses,
};

int configure_console(
    ConsoleIo io,
    ConsoleInputMode mode = ConsoleInputMode::KernelDebugger
) noexcept;

bool console_user_io_enabled() noexcept;
```

替换原单参数configure声明/定义，默认参数只在头中给出，原调用仍可只传io。
service匿名namespace持有g_input_mode，初始KernelDebugger；不改ConsoleDevice的配置接口，不将用户模式规则塞入InputQueue。

configure先验证mode是上述两值之一，再调用设备configure；只有设备成功时才提交g_input_mode。
非法mode、无效回调或重复配置返回-1，原模式/回调/队列保持。
console_user_io_enabled仅在设备ready且模式为UserProcesses时true；不允许运行期切换，无reset或重绑。
ConsoleDevice/poll/read/commit/write原有接口与内部0暂空语义保持。

并发方案固定为：

- kdbg模式仍只有A访问输入；用户不能打开控制台FD。
- user模式没有A、后台poll_task、UART中断生产者；正长度用户READ在现有sched_read_file检查的SIE0区间内，主动poll一次，再peek/copy/commit。
- 从poll到commit之间不主动yield/sleep/调度，不开启SIE；同一hart上其他用户读取只能在另一完整syscall事务中发生。
- 两个用户FD/进程可以竞争同一个输入流，不承诺前台进程组；本批保证一次交付事务不被另一个消费者拆开。
- service仍不自动关中断。后续若增加独立poll任务/IRQ，必须重新保护生产者和消费者，不能只凭singlehart或consumer SIE0认为安全。

这条方案不需要本批新增CSR助手、锁或原子操作，也不在用户态忙等驱动。

## 3. OpenFile增加种类，保持同一个池与引用模型

在fs/open_file.hpp中增加：

```cpp
enum class OpenFileKind {
    None,
    Builtin,
    Console,
};
```

OpenFile新增：

```cpp
OpenFileKind kind() const noexcept;
long write(std::span<const unsigned char> source) const noexcept;
```

私有成员增加OpenFileKind kind_{OpenFileKind::None}，其余file_/offset_/reference_count_保留。
不增加ConsoleDevice指针、第二个池、虚函数或通用后端框架；本项目只有一个长期控制台，Console种类通过窄服务接口访问它。

| 状态/操作 | None | Builtin | Console |
| --- | --- | --- | --- |
| file() | nullptr | 原BuiltinFile指针 | nullptr，不伪造普通文件 |
| offset() | 0 | 原共享文件位置 | 始终0，输入消费由公共队列表示 |
| read_current | -1 | 原read_at(offset) | read_console_current，0仍是内部暂空 |
| commit_read | -1 | 原范围检查后推进offset | commit_console_read，不修改offset |
| write | -1 | -1，包括空span | write_console，空span按设备约定返回0 |

关闭后的对象/错误种类不因空长度变成有效对象。
各层具体错误由返回值处理，普通用户非法FD不panic；pool归属/非法release仍是内部不变量诊断。

OpenFilePool新增OpenFile* acquire_console() noexcept：设备未ready返回nullptr；找空槽后发布Console/offset0/ref1/file nullptr。
原acquire(file)发布Builtin/file=&file/offset0/ref1。
**空槽必须按kind==None识别，不能再用file()==nullptr**：有效控制台对象的file也为空。
retain/release的活对象判断改为kind!=None且refs>0，保留归属检查与SIZE_MAX引用溢出拒绝；in_use仍统计refs>0。
最后release清kind=None、file=nullptr、offset=0、refs=0；不清控制台队列、不重配设备、不调用设备析构。

独立open控制台会取得不同OpenFile，但它们最终读取同一个设备流；与独立open普通文件各有offset不同。
fork继承时共享同一OpenFile/ref+1，仍按原FD号码继承；exec保留，exit/fault按原close_all清引用。
这些已有生命周期管线不需要再增加一套“控制台专用释放”。

## 4. FD表与/dev/console路径分派

FileDescriptorTable新增：

```cpp
int open_console(OpenFilePool& pool) noexcept;
```

与原open相同：先找最小空FD，调用pool.acquire_console，失败不发布；成功才写entries并返回fd。
fd0依然合法，不假设打开的console总是0，也不预留1/2。
close/get/inherit/adopt/close_all的通用指针与引用逻辑不变，勿为console复制一套表。

file_service.cpp的open_file在file_service_ready检查后，按span的长度和逐字符内容精确匹配/dev/console：

- 匹配：要求console_user_io_enabled，调用table.open_console(g_open_files)。
- 其他：沿用lookup_file及table.open普通文件。

比较不能只比path.data指针，不能用无终止保证的strcmp；带NUL、后缀、尾斜杠或不同大小写不算精确匹配。
BuiltinFileSystem仍只有原三条文件，不向boot files数组塞一个空的“控制台文件”。
lookup_file和旧READ_FILE14仍只认识普通内置文件，对/dev/console返回未知/-1，不绕过FD处理设备。
Process的open_file/get/close/clone/adopt接口原样复用，公共设备寿命独立于Process；不往Process添加队列或新的全局入口。

## 5. READ16：暂空、零长度、复制与消费

共享task/syscall.h新增：

```c
#define SYSCALL_ERR_WOULD_BLOCK (-2)
```

这是本项目read的暂时无输入约定，不承诺POSIX errno兼容；不改变WAITPID等现有接口解释。
user_api.h直接包含该共享头，使用户程序也用同一常量，不复制第二个-2宏。

fs::syscall_read保持原file_service/root/fd/length上限检查，取得有效OpenFile后：

1. Console对象要求console_user_io_enabled；失败-1。
2. 合法FD的length==0直接0，不poll、不碰用户地址、不消费，即使目标地址为0。
3. 正长度Console：调用poll_console_input一次，负数返回-1；Builtin不poll。
4. 用内核固定buffer调用file->read_current，count<0或count>length返回-1。
5. count==0：Console返回SYSCALL_ERR_WOULD_BLOCK，Builtin返回原EOF0。
6. count>0：沿用user_copy_to，失败返回-1不commit；成功才file->commit_read实际count，意外失败panic。

OpenFile内部仍返回0暂空，**只在syscall_read的Console零数量分支映射成-2**，不要把所有文件的0都改成-2。
sched_read_file及syscall_handle原样传递long结果，不把-2折叠为-1或转成成功。
保持原SYSCALL_READ_MAX=256，FD读使用它；旧READ_FILE_MAX别名与14接口保持。

读取失败时，用户buffer可能已有成功复制前缀，仍遵守旧user_copy_to契约，不承诺回滚目标buffer。
队列消费状态不推进；这次poll新收到并入队的字节会留下，不能要求失败后恢复UART硬件到读取前。
正长度暂空时不做目标地址校验/复制，因此暂时无输入时无效目标也可能先得到-2；有数据要交付时才按页表拒绝。

## 6. WRITE_FD18：完整复制后再输出

新增共享编号与用户封装，旧5不动：

```c
#define SYSCALL_WRITE_FD 18

/* user/user_api.h */
long user_write_fd(long fd, const void* source, size_t length);
```

user/syscall.S新增同名符号：a0=fd、a1=source地址、a2=length，a7=18，ecall后ret。
fs/file_syscall.hpp新增：

```cpp
long syscall_write_fd(
    Process& process, long requested_fd,
    uintptr_t source_address, uint64_t length
) noexcept;
```

task/sched.h与sched.cpp新增：

```cpp
long sched_write_file(
    struct trap_frame* frame, long requested_fd,
    uintptr_t source_address, uint64_t length
);
```

sched薄壳沿用current_user_task(frame)与supervisor_interrupts_disabled检查，再交给fs::syscall_write_fd。
syscall.c的case18按a0/a1/a2提取并调用sched_write_file，结果写frame->a0；不新建trap_frame或更改sepc推进规则。

fs::syscall_write_fd顺序：

- file_service_ready、root非空、fd范围、length<=SYSCALL_WRITE_MAX。
- 找有效OpenFile，要求kind==Console且console_user_io_enabled；普通只读文件包括length0也返回-1。
- 合法Console的length0返回0，不碰source地址，不调用输出回调。
- 用固定unsigned char buffer[SYSCALL_WRITE_MAX]，user_copy_from完整成功后才调用file->write。
- 复制失败返回-1，不能输出已经复制进内核的前缀；输出按实际长度包含NUL/ff，不当作C字符串。
- 返回设备结果；设备无失败同步回调契约下应为length，内部不一致可用PANIC_KERNEL诊断，不向用户虚报完整成功。

不要为了复用而把旧syscall_write(source,length)改成三参数；旧用户程序及独立回归仍依赖它。

## 7. Make配置与boot用户模式

主Make新增变量CONSOLE_MODE ?= kdbg，仅接受kdbg/user，非法值make阶段报错。
分别生成内部值CONSOLE_USER_IO=0/1；既有build/kernel_config.h配方同时生成：

```make
@printf '%s\n' '#define TINYOS_KERNEL_TRACE $(KERNEL_TRACE)' \
    '#define TINYOS_CONSOLE_USER_IO $(CONSOLE_USER_IO)' > $@.tmp
```

保留原cmp后才替换的规则与所有内核对象依赖此头。切模式会自动重编内核，不需clean；重复同模式不强制重编。
不把配置头加到用户CFLAGS：同一用户DEMO程序仅由不同boot模式决定是否启动。

demo_tasks.cpp的boot_configure_console依据TINYOS_CONSOLE_USER_IO选择枚举模式，传入原UART回调；函数仍只在boot调用一次。
task_a原kdbg代码不必改，不在user模式创建它，所以不会再有第二消费者。
boot_stages先完成文件服务/设备配置、镜像registry、sched_init，再进入boot_create_tasks的既有顺序保持。

boot_tasks.cpp按同一宏分支：

- kdbg：原task_create(task_a)。
- user：检查kernel_root，prepare_builtin_user_space(USER_PROGRAM_DEMO,candidate,entry,stack_top)，task_create_user(entry,stack_top,&candidate,0)，成功后sched_configure_user_reaper(new_id)。不创建A/B。

准备/创建失败清理尚未移交的candidate，清理失败panic，普通失败返回-1给boot；使用现有函数语义，不另写ELFloader。
发布成功后candidate已空，不再释放Process已拥有的空间；reaper登记失败返回-1让boot停机，不继续启动未登记用户，也不把成功移交的空间当candidate释放。
此boot操作没有运行期创建者，不从user_tag或寄存器推断parent。

协作者负责把test-process以及已有压力入口的隔离make命令显式固定CONSOLE_MODE=kdbg，避免从外层user模式调用时继承MAKEFLAGS。
不重写原进程payload、退出值或回收账本；本批不重跑500轮压力，入口参数迁移只做必要检查。

## 8. 用户DEMO：一个小型原始回显程序

作者将user/main.c改为简单控制台程序；worker.c无需修改，不把旧fork/exec测试搬回来。
保留至少一个非const静态字符数组作为banner，确保受限ELF仍有非空RW段，例如：

```c
static char message[] = "[console] ready (q to quit)\n";
```

主流程完整行为：

1. user_open("/dev/console", sizeof(path)-1)，保存返回fd，失败返回非0。
2. user_write_fd输出上述banner，检查完整长度。
3. 每次user_read(fd,&byte,1)。得到SYSCALL_ERR_WOULD_BLOCK时调用user_yield，检查返回0，然后重试。
4. 得到1时，若byte=='q'则输出`[console] bye\n`并结束；其他字节用user_write_fd按1字节输出，检查完整长度。
5. 非预期0/负数/数量按错误处理；成功退出前user_close检查0，main返回0；错误也关闭已经打开的FD并返回非0。

read返回long，先检查负值再转数量。无需调用uart函数、span、内核设备接口或读取MMIO。
用户应用当前不编辑行，不做命令分派。q是应用退出选择，不是设备EOF协议。
代码保持小于现有一页RX限制；不为这段小程序扩页或撤掉linker断言。
默认kdbg不启动DEMO，因此原普通交互不受新main影响。旧完整回归由test/process_regression提供原payload。

## 9. 实现顺序与既定验证范围

整批按下列依赖顺序完成后集中检查：

1. OpenFileKind/对象分派与pool.acquire_console，修正所有“file为空就是空槽”的假设。
2. FD表open_console与file_service精确路径分派；保留生命周期通用逻辑。
3. service一次绑定模式及getter，Make/boot两模式；此时主分支引用的模式接口要一致。
4. READ16暂空映射、WRITE_FD18的fs/sched/C分发/用户封装，保持旧接口。
5. 用户main小程序，编译并分别运行kdbg/user模式。助手承担宿主/隔离边界测试与原回归入口适配。

```sh
make -j4
make test
make CONSOLE_MODE=user qemu
make CONSOLE_MODE=kdbg qemu
```

退出QEMU仍Ctrl+A后X。无需clean；timeout退出本身不是功能通过，需核对实际输出和状态。

| 优先级 | 固定验收与明确预期 | 方法/学习点 |
| --- | --- | --- |
| P0 | 目标build/nm、头RV/native及共享C/汇编封装正确；原host保持，user↔kdbg切换自动重编、同值不重复重编、非法mode拒绝 | 接口与配置 |
| P0 | mode非法/重复配置保持；默认kdbg禁止用户consoleopen，未ready拒绝；不同模式用独立进程验证，不为测试加reset | 原始设备/服务及FS宿主 |
| P0 | Console对象file=null仍活，不被下一open覆盖；满池/满表拒绝，最后close复用槽；Builtin原offset/只读/EOF保持 | 真实pool/FD；kind与所有权 |
| P0 | 两次独立consoleopen和fork式继承都访问同一流；close一个不清公共队列/配置，最后close仅清打开对象 | 宿主真实引用/流；不扩进程矩阵 |
| P0 | READ合法FD len0返回0且不poll/不碰地址；正长度暂空-2；普通EOF0；有数据copy失败-1且不消费，合法重试仍获原前缀 | 宿主真实syscall算法、SIE0真实用户最小失败/重试观察 |
| P0 | WRITE_FD18 Console零长度0，普通文件/坏fd拒绝；NULff按长度输出；user_copy_from失败零输出，合法重试成功 | 宿主及真实用户最小拒绝路径 |
| P0 | user模式没有A/kdbg/后台producer；正长度read从poll到commit处于SIE0，暂空yield后可继续、输入x/NULff及q关闭退出0 | 真实QEMU原始字节、有界回显；不承诺粘贴无损 |
| P0 | 默认kdbg原五命令精确、安静/至少2 S timer；新增文件操作运行期PMM0、最终pool0，旧test-process退出0/23/37/0与35free21alloc净14/reap3保持 | 两个隔离profile，助手负责 |
| P1 | 解释两个console FD轮流读取时输入归属；不做前台进程组 | 可口头追踪，不要求新框架 |
| P2 | 标准0/1/2、dup/管道/阻塞/IRQ、多hart、粘贴压力、路径shell | 后续专题，不作为本批完成条件 |

暂空/合法读写的具体小型隔离fixture与测试接线由协作者承担，不往正式main塞故障矩阵或恢复旧测试噪声。
用户读写内核固定buffer最多256，保持原页访问校验；不去掉写权限/U位检查或把用户虚拟地址直接交UART。
独立进程测试service模式是为了保持一次绑定约束，不公开池/设备全局变量，不改产品副本来隐藏失败。

## 10. 功能与P0之后的原三题

1. 为什么有效Console对象的file()为nullptr，却不能被池当作空槽？最后关闭为什么不能清设备队列，独立open与fork共享又有什么不同？
2. 正长度read的-2、普通EOF0、合法零长度0分别表示什么？复制失败为什么不消费，write又为什么必须先完成全部用户拷贝再输出？
3. 本批为何没有后台poll任务，user模式为何不创建kdbg？若以后加独立producer，现有“syscall SIE0”还缺哪一侧保护？

实现/P0完成后一次集中梳理作答，不提前要求重答或加作业。

## 11. 准备与限制

当前只读核对现有对象/池/FD/Process/用户封装、两种启动路径及SIE检查，任务已交付，核心实现由作者完成。
独立设计审查认可同步用户read主动poll与profile互斥，避免本批新增异步生产者竞争；不将此结论扩展为任意并发下安全。
准备证据.cache/batch27-preparation-20261116/results.json：拟定种类/模式/FDwrite声明的RV64/native重复包含、constinit池构造、C11用户封装声明与18号汇编封装/C符号通过；B26已验收106产品源保持。
这些是缓存中声明与调用形状编译，不是设备FD算法、两种boot模式或新用户syscall的运行验收。本轮仅任务文档/索引与准备文件，不重跑旧测试或修改产品。
后续实现修正、固定P0与原三题集中记录在本文件。当前不宣称设备FD或新ABI已实现/验收，无产品代写或提交推送。

## 12. 首次宿主中止定位与集中待修（2026-11-17）

作者报告make test的tinyos_host_tests Subprocess aborted。原CTest日志显示真正终止是：

```text
[HOST PANIC] kernel/fs/open_file.cpp:109: match closed file
```

前一行task-table PASS被stdout缓冲截断，不代表task-table越界或负责这次panic。
最小复现直接链接当前open_file.cpp/builtin_fs.cpp，仅桩掉未使用的控制台链接接口，普通pool.acquire(file)得到：

```text
actual_kind=0 expected_Builtin=1 refs=1 file_matches=1
```

证据.cache/batch27-review-20261117/ctest-abort.log与kind-results.json，复现返回1表示缺陷确实存在，不是算法通过。
open_file.cpp哈希保持，无产品代改/为通过替换算法。未重跑整套host或QEMU，也未提前开展新增故障矩阵。

### A. 先恢复普通对象生命周期与旧文件读取

1. OpenFilePool::acquire仍按file()==nullptr找空槽，且成功没设Builtin。改为kind==None找槽，设置file/offset0/refs1/kindBuiltin。
   retain/release已经按kind检查，所以当前普通对象刚分配就被视为关闭，直接导致这次panic；不能删除release检查来掩盖。
2. release最后一个引用时还缺kind=None。否则换用种类找空槽后，释放的对象永远不能被再次分配。
3. syscall_read把console_user_io_enabled与poll无条件用于所有FD。必须仅在kind==Console时限制用户模式并poll；
   普通Builtin读不依赖控制台配置、不读UART。保留原合法FD零长早返回、Builtin EOF0与copy后commit。

### B. 已写功能中的其他错误与缺少分派

4. OpenFile::read_current/commit_read仍只有普通文件逻辑。Console必须先分派给read_console_current/commit_console_read；
   None拒绝；Builtin保留原file指针/offset验证。Console的file本来为空，不能用它拒绝所有设备读取。
5. configure_console先写g_input_mode再调用device.configure，会在重复/失败配置时改变旧模式。
   先验证mode，调用device.configure并检查结果，只有成功才赋g_input_mode，最后返回0。
6. syscall_write_fd已把用户源复制到内核buffer，但最后又把source_address转换成指针交file->write。
   必须传buffer和实际length；不能绕过刚完成的安全拷贝直接解引用用户虚拟地址。合法零长分支提前0，不访问源/输出；完整输出结果按既定契约检查。
   当前user_copy_from与设备write自身已对零长早返回，因此没有证据表明现零长路径会解引用源或产生输出；显式零长分支是按原约定理清流程，非新增已复现缺陷。

### C. 尚未完成的原任务接线（不视为已实现函数的新错误）

7. file_service::open_file还没有/dev/console精确匹配与open_console分派。
8. boot_configure_console仍用默认KernelDebugger，没有依据TINYOS_CONSOLE_USER_IO传入模式。
9. boot_create_tasks的user分支还是candidate/entry/stack_top与TODO，需按第7节准备/创建/登记reaper/清理/返回。
10. main仍为旧Hello banner，完成演示时改为第8节ready提示；user_api.h尚未直接包含共享task/syscall.h。
    新WRITE_FD18用户汇编、fs/sched/C分发主体已接，不能把缺旧banner当系统调用崩溃原因。

Make的模式校验/配置头生成静态已正确。协作者尚待按原约定处理独立回归/压力入口固定kdbg与新增host接线，这不是作者新增任务。
作者按以上原批次范围集中修正后，再由协作者复验；不追加学习题、压力或历史矩阵。

## 13. 修正后旧host通过，新写接口长度缺陷复现（2026-11-17）

作者报告测试通过，原CTest LastTest.log显示120用例通过；当前核对第12节种类生命周期、read/commit分派、仅Console gate/poll、精确路径、模式成功提交、两种boot/用户创建清理与公共常量/banner均已修好。
独立recheck只读检查path头native/RV自含/重复、两种boot分支、user任务适配及main C11的-Werror编译通过；global namespace/显式stddef属于可选样式，不阻塞。证据.cache/batch27-review-20261117/review/recheck/results.json。

当前仅剩一个新发现的运行阻塞：syscall_write_fd使用已复制的内核buffer后，写成file->write(buffer)。
buffer是固定256字节数组，隐式span的长度为整个数组，而不是用户请求长度。真实syscall/页表映射的最小复现得到：

```text
length1: return=256 output_bytes=256 expected=1/1
length0: return=256 output_bytes=256 expected=0/0
```

证据.cache/batch27-write-count-review-20261117/results.json，源保持，失败返回1为已复现缺陷；16页fixture最终全部恢复。
探针初版缺host panic桩和合法共享模板分支，仅修诊断接线后得到以上结果，没有替换产品实现。
应显式传std::span<const unsigned char>{buffer, static_cast<size_t>(length)}，合法零长也因此得到空span；可按第6节补显式早返回与完整输出检查。
这一缺陷使banner第一次写返回256，main会认为失败并关闭退出；旧suite尚未调用18号控制台写，所以旧通过不能代表新增P0通过。

协作者已按原任务在verify_process_regression.py与verify_task_table_stress.py的隔离make参数固定CONSOLE_MODE=kdbg。
实际执行make CONSOLE_MODE=user test-process，内部配置仍为TRACE1/CONSOLE_USER_IO0，结果.cache/process-regression/20261117-181725-3/results.json全部27项通过：退出0/23/37/0、35free21alloc净14/reap3零PMM/最终池0/后续两S；107正式源保持。只改两脚本参数，未改产品/旧payload、未跑压力。

先修当前实际长度，再继续原定新host/两profile真实QEMU与用户最小拒绝重试。没有提前启动已知会失败的用户QEMU，不把旧回归通过当整批通过，也不追加矩阵或理解题。

## 14. 最终实现与执行路径（2026-11-17）

作者已将WRITE_FD输出改为显式span{buffer, static_cast<size_t>(length)}，当前集中审查无新的功能阻塞。
合法零长现由copy helper及空span输出路径保持不访问源、不调用输出回调，真实验证通过；没有为形式补一个重复分支设新门槛。
release最后refs由递减自然到0，清kind/file/offset即可，无须额外要求重复赋0。模式配置结果仅0/-1，现res==-1检查符合既定接口。

完整关系与路径：

1. boot配置一次输入归属。默认kdbg只有A消费；user模式只创建DEMO并登记reaper，没有A/B或异步生产任务。
2. 用户open经安全路径复制，精确/dev/console分派到同一OpenFilePool的Console对象，再放入本Process的FD表。
   kind与引用数定义对象身份，Console的file=null合法；独立open是不同打开对象，但消费同一长期设备流。
3. 用户read在原sched薄壳的USER/SIE0检查后，正长度才主动poll一次，再peek/copy_to/成功commit。
   正长度暂空只在syscall边界返回-2，零长0不poll，普通文件EOF0且不触UART；失败不消费已经入队的字节。
4. 新WRITE_FD18先确认Console种类/用户模式和长度，再完整copy_from到内核buffer，只输出实际length。
   用户源复制失败时没有任何输出，旧WRITE5/PUTCHAR保持，用户虚拟指针不直接交设备。
5. close只减打开对象引用，最后关闭清池槽，不清公共设备/待输入。fork继承共享对象，exec保留、exit/fault关闭的原流程沿用。
6. 用户DEMO暂空yield后重试；x/NUL/ff按原字节回显，q输出bye、关闭FD、返回0；默认kdbg原命令保持。

当前串行安全依赖两个profile互斥和用户READ整段SIE0且不主动调度，不代表任意后台producer或IRQ加入后也安全。
未预留标准0/1/2，应用使用open实际返回的fd；尚未实现路径shell或重定向。

## 15. 全部既定P0验证（2026-11-17）

统一.cache/batch27-final-20261117/summary.json，all_passed=true。
107正式源在接口/默认控制台/正式user/独立retry/旧进程回归与最终当前树完全一致，协作者未代改产品算法。

| 项目 | 实际结果与证据 |
| --- | --- |
| 构建与接口 | root make -j4无诊断；review/results.json全部16项通过，RV/native头、C11用户共享常量/18号汇编、constinit、C符号及两种profile完整编译链接/nm均过 |
| Make配置 | 隔离副本kdbg→user→kdbg分别重编45个内核对象，同值两次不重编；非法模式拒绝且产物保持；root产物未被该检查修改 |
| 宿主 | make test三项CTest全过，原120+新增10=130个具体用例；host.log。新console_fd/main.cpp在kdbg/user两个独立进程分别绑定服务一次，无重绑/reset/getter |
| 对象/模式/文件 | 新宿主覆盖未ready/非法/重复模式保持、Console空file指针仍活、混合满池/满表拒绝与槽复用、独立open/继承共享流、最后关闭保留待输入、精确路径与kdbg拒绝设备用户open、Builtin读写权限/offset/EOF保持 |
| 宿主交付边界 | 新宿主实际页映射：合法零长不poll/不碰地址，正长度空-2；copy_to只读页失败不消费/合法retry原NUL前缀及ff；WRITE0/1/2精确、跨未映射页copy_from失败不输出前缀及合法retry，全部操作零PMM/fixture页全恢复 |
| 默认真实QEMU | console/results.json全部36项，原五命令精确，hex与当前实际内嵌worker64一致；55字节队列交付，配置1次，68次输出442字节；运行PMM0/available31634保持/池0，末尾146空poll+3S且安静 |
| 正式用户程序 | user/results.json全部19项；真实单USER/reaper无kdbg，输入x/NUL/ff/q只前三字节原样回显，q只bye；18号返回28/1/1/1/14，与实际长度相同；13035次-2后真实yield再读，显式close后退出0 |
| 真实用户错误/重试 | user-retry/results.json全部23项，独立明确标注的测试payload；READ零长地址0返回0无poll、正长度暂空-2；收到R后copy_to地址0返回-1、队列仍1/未commit，合法retry得到原R并只commit一次；WRITE地址0返回-1零输出，合法retry输出R，零长无输出 |
| 用户执行/回收 | 两个user组从poll到peek/copy/commit皆SIE0、原frame/root保持，文件操作运行PMM0；close后池0，exit切kernelroot，6个唯一私有页完整释放且根最后、Process空/available恢复6，idle两次S中断且安静 |
| 独立旧进程回归 | make CONSOLE_MODE=user test-process内部仍显式kdbg，27项全过：旧退出0/23/37/0、35合法唯一free/21运行alloc/净14/reap3零PMM/池0/末尾两S；.cache/process-regression/20261117-182317-3/results.json |

当前正式用户小镜像是代码/数据/栈3内容页+2中间页表+1根，共6页；旧回归保存的多页BSS镜像仍每空间7页。
这两套负载分开核对，不能把旧7页常量套到当前小应用，也不因本批少释放一页误判泄漏。
QEMU使用隔离副本和只读链接/同文件观察，QMP命名管道读取RAM，不向正式终端刷观察日志；满足条件后宿主停止，不是客体主动关机。
独立retry组只替换明确的测试用户payload，核心对象/设备/页访问算法保持，不以产品修补掩盖失败。

协作者仅新增test/console_fd/main.cpp与CMake/说明，以及此前两回归脚本的显式kdbg参数；没有新增作者测试作业。
未扩IRQ/并发producer/粘贴压力/500轮或历史矩阵，不承诺任意并发安全或终端物理发送完成。
本轮无提交推送。功能和全部既定P0通过，接下来仅按第10节原三题集中理解收尾。

## 16. 原三题原答与集中澄清（2026-11-17）

### 作者原答（原样保留）

> file看的是Kind不是文件是否为空，没输出完还要继续用，fork是父进程直接把内容复制给子进程对于文件来说是引用+1，Open是新开一个独立使用
> 暂空的-2是当前暂时空队列一会重试，EOF0是到了文件结尾停止继续读，0请求字节是需要复制0字节不返回，读取没有必须的下一个消费者，你拿走了没有对下一个必须交付的契约，而且元资料没有变化，写需要保证写入的是正确的输入内容，对下面的队列有要求，如果写到一半出现错误，那么就无法追回原本数据
> 都在竞争使用，没有多线程负责同时运行，现在互斥状态下还能，但是如果加入独立输入生产者现有SIE就不够了

### 集中审查

- 用kind识别有效对象，而不是用file指针空否判断池空槽，以及fork文件引用+1的方向正确。
  精确地说，是OpenFile的kind定义种类；file()本身仍返回普通BuiltinFile指针，Console时合法为nullptr。
  fork复制FD表中的引用，父子指向同一OpenFile，普通文件因此共享offset；不是把文件内容再复制一份。
- 最后关闭不能清公共队列，原因是service仍拥有永久ConsoleDevice、队列保存的原始输入不属于某个打开对象。
  当前InputQueue不是输出队列，输出同步回调返回后已消费该span；“没输出完”不是保留输入的原因。
  独立open取得不同OpenFile，普通文件各有offset，但不同Console打开对象仍消费同一个设备输入流。
- 正长度控制台read的-2为暂空可重试、只读Builtin的EOF0为已到末尾，理解正确。
  零长度请求会立即返回0，不复制、不poll/不消费，并非“不返回”或等待输入。
- 失败不消费的契约针对本次交付是否完成，与是否还有另一个消费者无关。
  peek复制进内核buffer后，用户目标仍可能无效；只有user_copy_to成功才推进队首，失败让本次调用者可以合法重试原字节。
  用户目标可能已有复制前缀，不承诺回滚目标内存；队列消费状态保持才是保证。
- 写先完整copy_from的理由是，UART已输出的前缀无法撤销。用户源跨页复制失败时，应在任何输出之前返回错误。
  当前没有“下面的输出队列”要求，也不宣称copy完整后终端输出具有可回滚事务性或物理发送完成保证。
- kdbg与用户模式互斥避免争抢输入/破坏事务；当前正长度用户read自己在SIE0内poll→peek→copy→commit，没有异步生产者。
  单核即使没有多线程同时执行，也可因定时抢占让不同执行者交错；以后独立producer被抢占时，其队列读改写也要保护。
  仅consumer进入syscall后SIE0不能保护此前已经被打断的producer操作，不能把未来问题归结为有没有“负责同时运行的线程”。

### 协作者整理的修正版（不是作者重答）

1. Console种类与引用定义其有效性，file指针可以为空。关闭只清打开对象引用，公共设备输入由长期服务拥有。
   fork共享同一打开对象/ref+1；独立open有不同打开对象，普通文件位置独立，控制台流仍共享。
2. -2表示控制台暂空重试，普通EOF0表示读到文件末尾；零长度立即0且无交付。
   读只有成功交给用户才消费，失败保留输入重试；写只有用户源完整复制成功才输出，防止无法撤回的部分输出。
3. 当前两个profile互斥，用户读取整个事务SIE0且无后台producer；未来增加独立输入生产者时，还须保护生产者操作及完整交付事务。

本轮仅原答归档与一次集中澄清，不要求重答、不增加题目或测试。第15节全部既定P0结果保持，无产品改动、重复测试或提交推送；本批收尾。
