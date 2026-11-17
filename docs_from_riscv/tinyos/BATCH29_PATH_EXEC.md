# BATCH29：按路径执行内置程序

日期：2026-11-17。状态：完整任务已交付，待作者实现。
B28功能、全部既定P0及原三题归档已经完成，保持收尾；本批推进一个新概念：路径EXEC。
本地Git检查点时，作者正在添加19号常量、用户声明/汇编及分发草稿，按暂存快照原样保留；
registry关联、路径处理和完整接线仍待本批实现，当前构建通过不等于路径EXEC可用。

## 1. 目标与可观察结果

让用户程序以`/bin/worker`选择执行目标，复用已有内置程序registry与同Task的exec事务。
路径是文件名字，程序编号是registry内部索引，Task ID仍是进程的公开身份。

```text
user_exec_path(path, length, argument)
→ syscall19 → scheduler检查当前USER/SIE0
→ 完整复制路径到内核 → 精确lookup得到永久BuiltinFile记录
→ 按记录身份找到已配置的程序编号
→ 既有exec：准备新空间 → 切kernel根释放旧空间 → 移交/替换现场 → 新根运行
```

正式user模式的echo增加小写e按键：不回显e，执行`/bin/worker`，worker通过保留下来的stdout1
打印一次`Hello from TinyOS worker.\n`并返回0。成功后旧echo不再继续；q仍输出bye退出，
其他字节仍原样回显，NUL/ff语义保持。默认kdbg仍是原五命令，不创建交互用户程序。

本批仅执行已经登记的两份内置ELF。已有SPAWN8/12、EXEC10/13及编号0/1语义保持。
不添加路径SPAWN、argv/envp、PATH搜索、工作目录、目录遍历、可写文件、动态ELF解析、
执行位权限系统、shell、dup/重定向、管道或阻塞输入，不扩大loader的两LOAD布局。

## 2. ABI与失败协议

共享syscall.h新增SYSCALL_EXEC_PATH=19，用户接口为：

```c
long user_exec_path(
    const char* path, size_t path_length, uintptr_t argument);
```

| 寄存器 | 内容 |
| --- | --- |
| a7 | SYSCALL_EXEC_PATH，19 |
| a0 | 用户路径起始VA |
| a1 | 路径字节数，1—127，不含终止NUL |
| a2 | 不透明入口参数argument，允许0 |
| 失败返回a0 | 完整long的-1 |
| 成功 | 进入新程序，旧调用不返回；fresh frame.a0为argument |

接口保持普通long返回类型，不能标noreturn，因为拒绝时会正常返回-1。
user/syscall.S在.text内增加user_exec_path：li a7/19→ecall→ret，放GNU-stack节之前，
不改原user_exit兜底循环或其他包装。

路径规则沿用现有FS：精确完整绝对路径，长度不包含NUL，不做路径规范化或尾随斜杠消除。
零长、超过127、坏用户映射、相对路径、内嵌NUL、未知文件、未登记程序一律-1。
`/etc/about`有真实文件记录，但没有程序登记，所以执行拒绝；`/dev/console`不属于Builtin文件记录，
也拒绝。不需要先user_open，不创建临时FD，不受当前FD表是否有空槽影响。

失败仍从旧ecall后继继续：dispatcher已有sepc+4，返回a0=-1，其余旧用户执行环境、地址映射、
Task/Process身份、父关系和FD状态保持。成功复用原fresh现场，不再覆盖a0为内部成功码0，
也不能对新entry再加4；本批不复制旧用户数据到新镜像。

## 3. 文件记录身份与程序registry

保持现有类型名BUiltinProgramImage，不为拼写顺带重命名。把描述改为借用真实文件记录：

```cpp
namespace tinyos::fs { struct BuiltinFile; }

namespace tinyos {
struct BUiltinProgramImage {
    const fs::BuiltinFile* file{};
};

int find_builtin_program(
    const fs::BuiltinFile& file, uint64_t& program_id_out) noexcept;
}
```

builtin_program.hpp可前置声明BuiltinFile，具体实现包含fs/builtin_fs.hpp获取完整定义。
实现文件中的BuiltinProgramRecord保存`const fs::BuiltinFile* file{}`和原解析layout，
不另外保存可能与file->bytes失配的第二份bytes描述。

configure_builtin_programs沿用原root/布局/一次配置检查，新增两项file非空且彼此不同、
各file->bytes非空的预检。仍在局部parsed_layouts中完整解析两份ELF，全部成功后再提交
两个file指针/layout、共同布局/栈顶，g_root最后发布。失败保持原状态，允许未配置状态合法重试；
重复配置拒绝并保持旧配置，不新增reset或全局记录getter。

prepare_builtin_user_space保留原编号参数、完整宽度检查、错误码及候选归属协议，
只把原program.bytes来源换成program.file->bytes，entry来自缓存layout，仍不重复解析ELF。
配置借用的记录、path、ELF字节必须在registry使用期间地址稳定、持续存活且不可变。

find_builtin_program的协议：

1. 未配置返回-1，program_id_out保持。
2. 扫描两条已配置记录，比较`record.file == &file`；匹配后输出其编号0/1并返回0。
3. 未匹配返回-1，输出保持。仅查身份，不分配页、不解析ELF、不创建打开对象。

不要硬编码`if(path == "/bin/worker") return 1`，也不通过比较ELF字节或span起点认定程序身份。
即使另一个文件记录借用完全相同的ELF字节，只要没有登记，也不因内容相同自动取得执行资格。

## 4. boot：传永久lookup结果

boot_start_user已经依次执行FS.init、configure_file_service、console配置和两个lookup。
把现有images数组改为使用lookup返回值：

```cpp
const tinyos::BUiltinProgramImage images[] = {
    {demo_file},
    {worker_file},
};
```

顺序仍为DEMO0、WORKER1，其他布局和创建流程保持。
必须传demo_file/worker_file，不能传`&files[0]`/`&files[1]`：
boot局部files[]只是FS初始化输入，FS已经将记录描述复制到自己的长期成员数组，
lookup返回的是那些永久成员记录地址。用户路径lookup与registry必须指向同一个记录身份。

正式源中BUiltinProgramImage数组当前只有此处构造；测试工厂仍调用编号prepare，
无需为旧回归修改程序选择语义。旧缓存中的历史原型不自动加入迁移范围。

## 5. 路径处理放在用户任务操作模块

在task/private/user_task.hpp新增声明，实现在现有user_task_exec.cpp：

```cpp
int exec_path(
    task& current, trap_frame& frame, const Sv39PageTable* kernel_root,
    uintptr_t path_address, uint64_t path_length, uintptr_t argument);
```

调用上下文与原exec一致：受信的当前USER task、非空Process/用户root、来源SPP0，
当前SIE0，期间不主动调度。scheduler负责这些当前任务检查；本助手负责用户路径边界。

按以下依赖顺序完成，不新建第二份exec提交算法：

1. 检查file_service_ready、path_length在1—kMaxBuiltinPathLength，当前Process root有效。
2. 局部`char path_buffer[fs::kMaxBuiltinPathLength]{}`，用当前用户root和user_copy_from
   完整复制path_length字节。失败直接-1；禁止将原用户VA强转成内核可解引用路径。
3. 以实际长度构造只读span，fs::lookup_file精确取得const BuiltinFile*；空指针直接-1。
4. 使用局部uint64_t program_id调用find_builtin_program；失败直接-1。
5. 把编号和argument传给同文件现有exec(current,frame,kernel_root,program_id,argument)，透传结果。

路径数组无须补终止NUL，也不使用strlen。用户拷贝失败可能修改内核数组前缀，
但还没查程序或分配候选；不得将该前缀当作成功路径继续处理。
路径span和用户地址都不存到registry/Task/Process，不在旧用户空间释放后继续读取它们。
lookup结果是永久文件记录，可在路径数组退出作用域后继续由registry引用。

本批不引入独立path-exec模块、C++多态后端或通用loader对象；现有exec函数体与提交顺序保持。
新助手按现有包含次序显式加入file_service、BuiltinFile定义和user_access等实际依赖。

## 6. scheduler与syscall分发

sched.h新增带原C链接保护的接口：

```c
int sched_exec_path(
    struct trap_frame* frame, uintptr_t path_address,
    uint64_t path_length, uintptr_t user_argument);
```

sched.cpp中与sched_exec_current相邻：current_user_task(frame)检查失败或SIE非0直接-1，
成功转发到user_task::exec_path，传原g_kernel_root及参数。
不公开Task/Process/root getter，不把路径复制和FS查找塞回scheduler。
EXEC_PATH不额外检查console_user_io_enabled：输入归属不决定USER任务执行程序的资格，
kdbg中的独立用户回归也可以按路径exec。

syscall.c新增19分支，先按完整宽度保存a0/a1/a2，再调用sched_exec_path。
保持旧10/13的成功处理方式：只在返回非0时写frame->a0=-1，成功保留已替换的整个fresh frame。
公共sepc+4仍在switch之前执行，不另加第二次推进。

## 7. 正式main/worker的最小演示

user_api.h添加user_exec_path声明；main保留静态可写message[]，更新提示为：

```text
[console] ready (e to exec, q to quit)\n
```

该实际字符串39字节；bye仍14字节。新增静态路径字符串`/bin/worker`，调用长度sizeof(path)-1。
在普通字节回显前增加byte=='e'分支，调用user_exec_path(path,len,0)。
成功不会返回；只要返回就走已有io_error，向2尽力诊断并返回1，不继续回显e。
其余暂空/yield、q、原始字节echo与错误出口保持，不新增命令行解析。

worker保留静态可写message[]及原26字节`Hello from TinyOS worker.\n`，
把user_write旧5改为user_write_fd(TINYOS_STDOUT_FILENO,...)，核对实际长度后返回0/1。
argument仍可暂不使用，真实fresh参数由助手隔离验证。
这条演示说明exec保留stdout；没有OPEN/CLOSE、新Task或新三项初始化。
不要把独立测试payload的拒绝矩阵和fork/wait分支放回正式main/worker。

## 8. 文件范围与实现顺序

| 文件 | 本批改动 |
| --- | --- |
| include/process/builtin_program.hpp、kernel/process/builtin_program.cpp | 文件记录身份、一次配置、查编号及原prepare取字节来源 |
| kernel/boot/private/boot_stages.cpp | 从lookup结果构造两程序描述 |
| include/task/private/user_task.hpp、kernel/task/user_task_exec.cpp | 安全路径处理并复用原exec |
| include/task/sched.h、kernel/task/sched.cpp | 当前USER/SIE0薄壳 |
| include/task/syscall.h、kernel/task/syscall.c | 新19号及保持fresh现场的分发 |
| user/user_api.h、user/syscall.S | 用户三参数接口和汇编包装 |
| user/main.c、user/worker.c | 单键路径执行及标准输出演示 |

不增加对象文件，原Make依赖规则复用；不改fork/SPAWN、FD表、Console服务、trap汇编或MM算法。

1. 先改registry描述/记录与configure/prepare，补find_builtin_program，并同步boot的images数组。
2. 完成user_task::exec_path，再接scheduler薄壳、19号case和用户包装。
3. main增加e、worker改写stdout，整批编译并观察两种退出路径。
4. 作者整批完成后，助手集中列全问题并承担第9节固定P0；修正后仅复验受影响部分。

```sh
make -j4
make test
make CONSOLE_MODE=user qemu
make CONSOLE_MODE=kdbg qemu
```

user模式分别观察：x/NUL/ff/q仍按旧行为；另一次输入e仅出现worker消息并退出。
QEMU退出Ctrl+A后X；无需clean。助手原回归入口仍为make CONSOLE_MODE=user test-process，内部固定kdbg。

## 9. 固定P0及选做范围

P0全部由助手承担，作者只实现上述核心和小演示，不新增测试框架。
本批验收是路径选择及复用旧exec事务，不重开B28标准流矩阵或500轮压力。

| 优先级 | 完整预期 | 方法/学习点与作者新增测试代码 |
| --- | --- | --- |
| P0 | 两profile完整build/nm、相关头RV/native自包含重复、用户C11及19号C符号/三寄存器包装正确，旧137组host保持 | 助手接口/既有suite；作者0行 |
| P0 | registry两永久记录分别映射0/1；未配置/未知记录失败时id_out保持，借相同ELF字节的不同记录也拒绝；空file/重复file/空字节/坏ELF配置失败无发布，合法retry和重复配置保持 | 目标QEMU有限配置probe，使用真实FS/parser/registry并确认零PMM；不为宿主测试改产品算法，作者0行 |
| P0 | 正式e成功，不回显e/不回旧echo；原Task/Process/公开ID/parent/内核栈/frame地址和FD0/1/2对象引用保持，fresh入口/栈/a0=0正确，worker经stdout一次26字节后exit0、最终池0与两S ticks | 正式payload真实QEMU；成功替换与资源寿命，作者仅第7节演示 |
| P0 | user正常39字节banner/xNULff/q与14字节bye保持；默认kdbg五命令/安静/至少两S timer保持 | 助手两个profile有限观察；作者0行 |
| P0 | 隔离用户调用长度0/128、坏首地址、跨未映射页路径、相对路径/内嵌NUL、未知路径、/etc/about和/dev/console均-1，解析拒绝阶段无PMM/FD/pool变化；旧程序继续、数据/映射/关系保持 | 有限新边界组，不扩通用地址穷举；作者0行 |
| P0 | 新接口的非0入口参数（如17）实际到达新入口；父先open about3/close2，fork子以路径exec后仍共享0/1/3与普通offset、2仍空，父引用保持，退出关表/父wait正确 | 一个独立有限生命周期负载；无新SPAWN继承或完整B28矩阵，作者0行 |
| P0 | 路径解析后准备目标的第4次PMM申请受控一次NULL：候选先前3个PA完整回收/根最后，旧satp/空间/数据/FD保持并返回-1；旧用户实际继续后同路径重试成功 | 一次准备故障/重试，精确区分候选与旧空间；作者0行 |
| P0 | 成功提交先切kernel根再释放旧空间/根最后，再运行新根；所有释放按实际ELF页数唯一核账，文件解析/复制本身零PMM，最终池0；旧test-process退出0/23/37/0及35free21alloc净14/reap3保持 | 助手真实QEMU与原回归各必要一次；作者0行 |
| P1 | 讨论同一字节内容的两个文件记录为何不自动拥有同一程序身份 | 可口头回答，0行，不阻塞 |
| P2 | 将来增加argv、PATH搜索、路径SPAWN或shell时分别在哪一层扩展 | 后续专题，当前不实施，不阻塞 |

失败观察中，dispatcher的旧sepc+4和a0=-1是预期变化，不要求整张旧frame逐字节不变。
候选从未活动，准备失败清理可以在旧用户root下完成并保持satp；成功提交才切kernel root释放旧空间。
不能复用“任何release都必须处于kernel root”的错误观察前提。
本批不承诺修复loader自身回滚再次失败的历史限制，也不扩清理失败/OOM全部位置矩阵。
测试payload与观察器由助手提供，核心实现保持，不用副本修补产品算法来获得通过。
registry实现已有目标专用wfi指令，因此配置/身份的纯逻辑检查也可在目标probe完成，
不要求整文件原样通过宿主汇编，不为此删除或替换产品中的目标指令。
正式小应用当前每空间6私有页；新演示/独立负载须按实际readelf及运行记录核账，
旧BSS回归每空间7页/总35free继续独立核对。

## 10. 功能与P0之后的原三题

1. 为什么不能把用户path VA直接交lookup，或留到旧空间释放后再读？lookup得到的文件记录
   为什么又可以跨exec提交继续使用？
2. 文件有名字、字节是合法ELF，为什么仍可能不能执行？文件记录、程序编号、Task ID分别代表什么？
   boot传&files[1]与worker_file有什么寿命和身份区别？
3. 路径exec失败返回时，旧程序哪些内容保持、哪些返回寄存器/PC会变化？成功时为何必须保留
   fresh frame.a0而不写内部0？准备失败清候选与成功切根清旧空间有何不同？

实现/P0完成后梳理完整相关路径，再一次集中作答和归档；不提前重答B28或追加题目。
可选配套阅读仍可看《操作系统：设计与实现》第三版第5章5.1“文件”，
围绕“按名字找到文件”与“进程采用哪些机制执行其内容”区分名字查找和执行策略，
再对照本批lookup/registry/exec三层；不套未经核对的中文页码，不作为编码前置门槛。

## 11. 准备验证与当前状态

独立只读设计审查支持文件记录身份映射既有ID、scheduler保持薄壳、操作模块安全复制路径后
复用原exec；特别核对了永久FS记录寿命、成功fresh现场与失败旧satp的不同约束。
.cache/batch29-preparation-20261117/results.json全部12项通过：拟定C++接口及三头
RV64/native重复自包含、C11用户/scheduler声明、19号汇编包装及C符号，108正式源配置哈希保持。
这些仅是缓存中的声明与调用形状/ABI检查，不是B29功能、完整链接或QEMU验收。

当前仅交付任务与索引，核心由作者完成；下一实现点为第3节文件记录关联和身份查找，
随后同步boot，再按第8节整批接线。助手不预铺实现或提前替作者修改产品。
本轮不重跑B28测试，不提交推送、不批量重构；B29修正、验证和原答继续集中记录于本文件。
