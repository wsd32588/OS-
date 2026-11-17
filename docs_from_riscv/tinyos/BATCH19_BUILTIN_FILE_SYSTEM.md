# BATCH19：只读内置文件与按偏移读取

日期：2026-11-12—13。状态：功能、全部既定P0及三题原答/集中澄清/修正版归档完成，本批收尾。

[B18](BATCH18_BUILTIN_PROGRAMS.md)功能、全部既定P0和三题已收尾。本批进入文件系统阶段，先建立“有名字的字节序列”和读取接口。
核心由作者实现，协作者集中审查并承担下面提前确定的P0。一个批次使用本文件持续维护，不另拆任务文档。

## 1. 目标与可观察结果

把现有两份ELF与一个普通文本登记成三个只读文件：

| 完整文件名 | 字节来源 | 用途 |
| --- | --- | --- |
| `/bin/demo` | 原 `user_elf_start/end` | DEMO镜像 |
| `/bin/worker` | 原 `user_worker_elf_start/end` | WORKER镜像 |
| `/etc/about` | `TinyOS builtin fs\n`，18字节 | 验证文件层也能处理普通数据 |

启动时应能按名字查找三文件，读取两个ELF的前4字节并核对magic，读取about内容并输出：

```text
[fs] /etc/about: TinyOS builtin fs
[fs] ELF lookup/read OK
```

随后从查找结果取出只读ELF字节，配置现有program registry，继续原双用户/SPAWN/FORK/EXEC/收集演示。
这让文件层实际进入启动装载的数据来源路径，而不只是增加一段无调用者的示例代码。

本批支持完整绝对文件名的精确查找。`/bin`和`/etc`目前没有独立目录节点，不遍历目录树、不做路径规范化。
用户open/read/close、文件描述符与打开位置、路径式EXEC、可写文件、块设备及磁盘格式按后续独立批次推进。
本模块是只读内置文件表，不声称已实现通用VFS或某种现成磁盘ROMFS格式。

### 本批配套阅读（作者已有《操作系统：设计与实现》）

作者已确认第三版中文上下册。先按第三版节号定位，中文实书页码未核对，不套用英文页码；这是辅助理解，不增加实现前置或验收作业。

- 第5章5.1“文件”中有关文件访问/操作的部分：对照本批path/bytes/offset/实际读取量，区分文件内容与一次读取请求。
- 第5章5.7“MINIX 3文件系统的实现”开头介绍头文件及全局结构的部分：对照MINIX的const.h/type.h/proto.h与本批常量、BuiltinFile和接口声明，理解声明、类型与实现职责。

节号参考[原书第三版目录](https://flylib.com/books/en/3.275.1.3/1/)，代码结构可对照[官方源码附录](https://www.minix3.org/documentation/AppendixB.html)。中文标题措辞以手中译本为准。

后续涉及打开文件/FD、目录、缓存或磁盘布局时再提示相应部分；每次说明当前要从书中弄清的问题。

## 2. 职责边界

| 模块 | 本批责任 |
| --- | --- |
| `fs::BuiltinFileSystem` | 完整名字→只读文件记录，初始化与查找 |
| `fs::BuiltinFile` | 文件字节与显式偏移读取，不存全局读取位置 |
| `process/builtin_program` | 程序编号→已解析ELF布局，准备用户候选空间 |
| `user_loader` | ELF段校验、分配、复制、清零、映射 |
| Process/Task/scheduler | 保持原拥有关系、身份、现场和调度 |
| boot | 组合名字与永久字节，创建文件表，从查找结果构造原镜像配置 |

文件表不解析ELF，不知道Task、PTE或用户地址；ELF registry不解析文件名。
两者借用同一份永久ELF字节，各自保存自己的名字描述或解析布局，不重复拥有/复制整份文件。
当前编号ABI保留；以后接路径入口时，再在相应批次决定兼容映射和解析缓存，不在本批同时迁syscall。

## 3. 新文件和完整接口

新增：

- `include/fs/builtin_fs.hpp`
- `kernel/fs/builtin_fs.cpp`

纯C++接口，无需C ABI。头文件如下；实现文件先包含自己的头，助手留在 `.cpp` 匿名命名空间。

```cpp
#ifndef TINYOS_FS_BUILTIN_FS_HPP
#define TINYOS_FS_BUILTIN_FS_HPP

#include <stddef.h>
#include <span>

namespace tinyos::fs {
inline constexpr size_t kMaxBuiltinPathLength = 127;

struct BuiltinFile {
    std::span<const char> path{};
    std::span<const unsigned char> bytes{};

    long read_at(size_t offset, std::span<unsigned char> destination) const noexcept;
};

class BuiltinFileSystem {
public:
    static constexpr size_t kCapacity = 8;

    BuiltinFileSystem() = default;
    BuiltinFileSystem(const BuiltinFileSystem&) = delete;
    BuiltinFileSystem& operator=(const BuiltinFileSystem&) = delete;
    BuiltinFileSystem(BuiltinFileSystem&&) = delete;
    BuiltinFileSystem& operator=(BuiltinFileSystem&&) = delete;

    int init(std::span<const BuiltinFile> files) noexcept;
    const BuiltinFile* lookup(std::span<const char> path) const noexcept;

private:
    BuiltinFile files_[kCapacity]{};
    size_t file_count_{};
    bool initialized_{};
};
}

#endif
```

`BuiltinFile*`是借用的记录地址，**不是整数文件描述符FD**。文件系统对象固定存储且禁复制/移动，保证返回的记录地址稳定。
可以创建多个独立实例，例如宿主测试；禁移动不代表这个类型只能有一个实例。
文件表本身不分配页、不用堆、无锁、不隐式释放字节；初始化后记录保持只读。

### span与生命周期

- `init`复制文件描述，不保存调用者描述数组的地址，因此这个数组可以是boot局部数组。
- `path`和`bytes`都只复制span中的指针/长度，不深拷贝底层内容；底层字符和字节必须在文件表使用期间持续有效且只读。
- boot使用永久的incbin字节和`static constexpr`名字/about字节；文件系统对象在boot实现的匿名命名空间长期保存。
- `lookup`结果只在对应文件系统实例存活期间有效；本批无卸载、删除或重新初始化，因此没有运行期失效流程。
- 调用者传入合法span及实际可读/可写内存。将来用户路径/缓冲区先由syscall安全复制到内核，本模块不直接解引用用户VA。

## 4. 三个接口的契约与实现步骤

### `BuiltinFile::read_at`

输入offset以**文件开头的字节偏移**为单位；返回实际复制的字节数，0表示空请求或EOF，-1表示本接口的返回范围无法表示读取量。
读取不改变文件内容，也不保存当前位置。destination由调用者拥有，必须可写且不与文件字节区重叠。

实现顺序：

1. destination为空，或offset大于等于bytes.size()，直接返回0。空文件合法；不要先形成 `data()+offset`。
2. `remaining = bytes.size() - offset`，取remaining与destination.size()的较小值作为count。
3. 在复制前确认count不超过long最大值，再逐字节复制并返回显式转换后的long。
   当前GNU/Clang工具链可使用 `__LONG_MAX__`，临时目标/宿主检查已验证可用；不依赖缺失的目标libc头。
4. 仅写destination前count字节，其余保持原值。错误也不能写目标。

不要用 `offset + destination.size()` 判断边界，它可能溢出；先比较、再减法即可。
offset超过EOF（包括SIZE_MAX）返回0，不当作非法指针去计算地址。
按字节复制，文件允许包含0和0xff，不能通过strlen决定文件长度。

这不是 `user_copy_to`：这里的目标是可信内核缓冲区，没有Sv39根或PTE检查。
将来READ syscall再负责用户地址权限/复制，避免把页表操作塞进文件层。

### `BuiltinFileSystem::init`

返回0成功，-1失败。仅允许初始化一次；成功后文件数量和记录不再改变。
接受1到kCapacity个记录，**允许0字节文件**。

先全部预检：尚未初始化、数量合法、每个路径合法、没有两个完全相同的路径。
数量最多8，直接两层循环比较即可，不引入哈希表。
最后才逐条复制记录、设置file_count_，并把initialized_设为true。
失败时原配置完全不变；失败初始化可以用正确输入重试；成功后的重复初始化必须拒绝且保留已有记录。

路径规则（初始化与lookup共用一个私有检查助手）：

- 显式字节长度1—127，必须以 `/` 开始；`/`本身不是文件名，拒绝。
- 不包含任何NUL；调用者传的span长度不包含C字符串末尾的`\0`。
- 拒绝空路径组件：重复斜杠、末尾斜杠都拒绝。
- 拒绝组件恰好为`.`或`..`，不将它们展开。`.hidden`这样的普通组件可以接受。
- 名字按字节精确、大小写敏感比较；无需做Unicode或大小写转换。

因此 `/bin/demo`合法，`bin/demo`、`/bin//demo`、`/bin/../demo`、`/bin/demo/`均拒绝。
128字节输入必须先拒绝，不能为了检查末尾而越界读取。
路径合法性只是命名约束，本批没有目录节点或目录权限遍历。

### `BuiltinFileSystem::lookup`

未初始化、路径非法或找不到记录，返回nullptr；找到则返回 `&files_[i]` 的只读借用指针。
只遍历 `[0, file_count_)`；比较时先确认长度相等，再逐字节比较。
`/bin/demox`不能命中`/bin/demo`，也不接受只匹配共同前缀。
查找不修改表、文件字节或读取位置，不申请PMM页。
找不到文件与找到0字节文件有区别：前者nullptr，后者是有效记录，read_at返回0。

## 5. boot接线：从文件取得现有程序字节

在 `kernel/boot/private/boot_stages.cpp` 包含新文件头，匿名命名空间增加：

```cpp
constinit tinyos::fs::BuiltinFileSystem g_builtin_fs{};
```

保留现有ELF链接器符号和end>start检查。文件名/about使用静态常量，注意span排除终止NUL：

```cpp
static constexpr char kDemoPath[] = "/bin/demo";
static constexpr char kWorkerPath[] = "/bin/worker";
static constexpr char kAboutPath[] = "/etc/about";
static constexpr unsigned char kAboutBytes[] = "TinyOS builtin fs\n";
```

在现有两S创建段之前，构造局部三个描述并初始化文件表，例如：

```cpp
const tinyos::fs::BuiltinFile files[] = {
    {
        {kDemoPath, sizeof(kDemoPath) - 1},
        {user_elf_start, static_cast<size_t>(demo_end - demo_start)}
    },
    {
        {kWorkerPath, sizeof(kWorkerPath) - 1},
        {user_worker_elf_start, static_cast<size_t>(worker_end - worker_start)}
    },
    {
        {kAboutPath, sizeof(kAboutPath) - 1},
        {kAboutBytes, sizeof(kAboutBytes) - 1}
    }
};
```

之后按顺序完成：

1. `g_builtin_fs.init(files)`，失败用原boot_fail诊断停机。
2. 使用不含NUL的path span查三文件，每个返回值都先检查再解引用。
3. 用read_at读两个ELF的前4字节，确认实际数量4以及`0x7f,'E','L','F'`。
4. 用32字节内核buffer读about，实际数量应为18；按返回数量逐字节输出，不能假设buffer含终止NUL。
5. 输出第1节的两条观察日志，再从文件记录构造原镜像数组：

```cpp
const tinyos::BUiltinProgramImage images[] = {
    {demo_file->bytes},
    {worker_file->bytes}
};
```

这里沿用当前B18头中的类型拼写；不为本批顺手改名。两记录顺序保持DEMO0、WORKER1。
后面的sched_init、两S创建、configure_builtin_programs、两份DEMO prepare/发布、reaper登记与失败清理保持原顺序。
镜像配置现在使用lookup得到的bytes，不能另用原裸ELF span绕过文件层。

不需要把整份ELF读到另一个buffer再解析；只读内存文件可以安全提供持续有效的字节span。
read_at用来表达通用读取语义，当前loader继续消费已有连续字节接口。
后续出现磁盘文件时再处理缓存/装载输入边界，不在本批为尚未出现的后端增加虚函数框架。

## 6. 构建与实现顺序

主Make的OBJS加入 `kernel/fs/builtin_fs.o`，DEPS和现有模式规则会继续生效。
两个用户程序、用户ABI、用户链接脚本、镜像嵌入规则都沿用B18；新文本直接来自内核常量。

建议本批按以下顺序写：

1. 新头和实现，**先完成BuiltinFile::read_at**：偏移、短读、EOF、原字节不变。
2. 写私有路径检查/字节相等助手，再完成init全预检/一次提交和lookup。
3. 加Make对象与boot三个文件登记、查找、读取，改原images来源。
4. 整批完成后集中审查，再由协作者接宿主用例和执行既定P0。

协作者维护 `test/test_builtin_fs.cpp`、宿主库的 `test/CMakeLists.txt`对象与 `test_main.cpp`调用；
不要求作者另造测试框架。该模块没有CSR或硬件指令，宿主测试应直接编译真实实现。

常用验证：

```sh
make -j4
make test
timeout 20s make qemu
make test-stress
git diff --check
```

不需要clean。宿主验证纯查找/复制逻辑，QEMU确认文件表真正接入boot后的原用户流程。

## 7. 整批验收与责任

### P0：协作者承担，提前固定范围

1. 实际目标/宿主构建与头自包含通过；真实文件模块没有PMM或页表依赖，原宿主回归保留。
2. init事务：未初始化查找nullptr；空/超容量、非法路径、最后一条重复路径不留下部分配置；正确重试成功；重复初始化保持原记录及返回指针。
   描述数组可在调用后覆写，其登记结果不变；借用名字/字节的底层存储仍持续有效。
3. lookup：三个精确名字命中；未知名字/相同前缀不同长度、相对名、NUL、空组件、`.`/`..`、超长名字拒绝。
   127/128字节边界各一例；0字节文件可找到，读取返回0。
4. read_at：普通文本与含0/0xff的二进制；分段读取拼接等于原文件；短读只写实际字节且尾部哨兵不变；
   空目标、EOF、超过EOF及SIZE_MAX均返回0，原文件始终不变。计数可表示性检查作源码/目标检查，不构造非法巨型span。
5. 正常QEMU：三文件lookup/read的日志和两个真实ELF magic通过，配置输入确实来自lookup结果；
   原双WORKER/退出23与37、父隔离/退出0及最后两S ticks保持。用户地址空间仍各7页，正常五份空间35次合法释放、运行21次分配、三次reap零PMM；文件层不分配物理页。
6. 原make test-stress默认500轮继续通过；只复用既有负载，不加新文件系统压力框架。

以上是作者功能完成后的助手验证清单，不是作者额外测试代码作业。不会在每次小修后继续扩大矩阵。

### P1/P2：选做，不阻塞下一批

P1可自己设计两个读取者交错使用不同offset，解释为何不会相互推进位置；已有read_at接口即可，无新框架。
P2可写确定性随机路径/小buffer分段读取检查；不实现不影响本批完成。
目录树、可写文件、FD、块设备属于后续功能，不作为本批隐藏验收条件。

## 8. 实现后梳理与三题

功能与既定P0通过后梳理：永久字节→描述复制→名字查找→按offset读取→ELF registry→原进程装载。
然后一次讨论：

1. init复制了哪些东西，借用了哪些东西？为何局部files数组可以消失，局部名字字符数组却不能随意消失？lookup返回值由谁保证寿命？
2. 两个调用者交替读取同一文件，为何read_at不会让他们互相改变位置？未知路径与0字节文件如何区分？
3. 从同一文件得到同一份ELF字节，为何两个进程修改counter仍互不干扰？文件读取层与ELF loader各自决定什么？

原答、审查和修正版已在第11节归档，不要求重复作答。

## 9. 当前准备证据与限制

临时准备 `.cache/batch19-preparation-20261112/`：建议头的重复包含/自包含、constinit固定实例、
三个局部描述/借用返回指针/read_at调用以及接回原B18镜像配置，在RV64 freestanding C++20和native C++20均语法通过、无诊断。
`/etc/about`字节数18及目标/宿主 `__LONG_MAX__` 可用性同时确认，正式产品源码/配置hash保持。
这里没有实现任何文件系统方法，也没有运行B19功能/测试；不能当作本批验收。

以上是11月12日的接口准备证据，不代表随后作者草稿已通过整批运行验收。

2026-11-13：作者已创建头/实现，read_at先判空/EOF再减法取count，超long范围返回-1，复制后显式转long返回。
原草稿的范围错误返回0已修正；init目前为空函数体，lookup尚无实现，文件系统尚未加入主Make/宿主/boot。
作者要求夜间先保存到GitHub，本轮保留进行中代码；上传前主线make与make test通过，它们未覆盖未接线的FS草稿。
下一步从第6节第2项继续init/lookup，之后接入构建与boot，再由协作者承担既定P0；本次不提前填完这些核心函数。

同日init/lookup接续检查：作者init主体已写，但路径助手component_start误为size_t的-1，需改为1；
重复检查第二个参数误用成员file_[j].path，需改输入files[j].path。两处均在提交前预检范围，未代改/未验证修复。
lookup草稿是命名空间自由函数，应补BuiltinFileSystem::作用域和末尾const，按已初始化/合法路径→有效记录逐项内容匹配→成员借用指针/null实现。
此阶段只读检查及指导，整批功能/P0仍待完成。

随后实读：上述init/helper两处及lookup定义均已由作者修正。boot登记/读取草稿需集中修：
先前移已有uintptr_t链接器范围检查，再取整数长度；用真实4字节数组承载header，以lookup的result->read_at(0,header)读取magic。
默认空span不分配缓冲区，offset4不代表“读4字节”，files->总指第一个局部记录，不能替代lookup返回值。
about输出及原images输入改为lookup结果仍按第5节完成；不扩大本批范围，当前未代改/未运行整批P0。

同日整批检查：作者已接about按实际18字节打印、images来自demo_file/worker_file及主Make对象，目标make无诊断。
最后两项集中修正：end>start检查还在files[]使用之后，应前移至四个uintptr_t地址之后；三文件循环后补约定的ELF成功日志。
这两项待作者完成，未代改产品；不因已验证合法镜像就忽略预检顺序。

协作者承担的宿主用例已持久化test/test_builtin_fs.cpp，并接CMake/test_main/README，7组全过且原suite保持。
覆盖第7节固定范围：init事务/重试/异址同内容重复/描述复制/借用指针，路径/127与128，空文件及二进制/分段/短读尾哨兵/EOF/SIZE_MAX/空目标。
默认make test-stress亦过，500轮/2000worker、spawn/fork各1000、exec/fault各125、满槽拒绝500，alloc18375/free18382，4.35秒；84正式源/配置hash保持。
汇总.cache/batch19-final-20261113/host-stress/results.json，压力完整证据.cache/task-table-stress/20261113-225039-30/。
正常FS数据来源/零PMM及原35页账本探针已准备在.cache/batch19-final-20261113/qemu-normal/，两项boot收尾后再运行最终当前版本。
本批尚未全部P0/三题收尾，无本轮提交推送；不扩新故障矩阵或重复已通过的无关验证。

## 10. 最终实现与P0结果（2026-11-13）

作者已将两个ELF的整数范围检查移到files[]构造前，并在三文件读取完成后补上约定的成功日志。
后半保留的一次相同范围检查只是冗余，不阻塞验收；协作者没有代改产品实现。
本节更新前述阶段记录：功能与全部既定P0已通过；随后第8节三题已作答并在第11节集中澄清，整批学习收尾完成。

实际执行路径如下：

1. incbin的两个ELF和静态about文本提供永久只读字节；boot先验证链接器范围，再组成局部files描述数组。
2. init先检查全部路径及内容重复，再将三个记录复制到长期存活的g_builtin_fs，最后提交初始化状态。
   复制的是span描述值，名字和内容仍借用原存储；lookup返回成员记录地址，不返回局部files元素地址。
3. lookup按长度与逐字节内容精确匹配。boot对两个结果分别read_at(0,header)，实际各读4字节；
   about读到32字节buffer中实际返回18，只输出这18字节。read_at只接收本次offset，不推进文件位置。
4. 从demo_file/worker_file的bytes配置原ELF registry，随后沿用prepare→loader→私有用户页→Task发布。
   文件层不解析ELF、不操作页表；loader决定段装载、BSS清零、权限和映射。
5. 当前所有用户内容页（包括text/rodata）仍各自分配。共享永久ELF源不表示共享装载后的物理页；
   本批未改变SPAWN/FORK/EXEC、父子身份与释放顺序。

| 验证 | 最终结果 |
| --- | --- |
| 真实文件模块宿主测试 | 7组全过，初始化事务/重试、异址同内容重复、描述复制与借用、路径127/128、二进制/短读/EOF等固定范围已覆盖；原suite通过 |
| 原500轮真实压力 | 2000 worker，spawn/fork各1000、exec/fault各125、满槽拒绝500；alloc18375/free18382，原逐轮检查通过 |
| 最终正常QEMU | 25项全过；实际三次read为4/4/18字节，about精确输出，registry真实输入与lookup的span一致 |
| FS资源 | init/lookup/read及镜像描述构造零PMM分配 |
| 原用户资源链 | 五份空间各7页、35次合法释放、运行21次分配、净恢复14页；三次reap零PMM，双WORKER退出23/37，父退出0，最后至少两次S tick |
| 构建与完整性 | 最终探针完整构建无诊断、无未定义符号；84个正式源码/配置在该次验证期间哈希保持 |

统一证据：`.cache/batch19-final-20261113/summary.json`。
宿主/压力：`host-stress/results.json`及`.cache/task-table-stress/20261113-225039-30/`；
正常QEMU：`qemu-normal/results.json`、`qemu.log`及`run_probe.py`（以上相对统一证据目录，压力目录除外）。

宿主/压力通过后作者仅修改boot范围检查位置和成功日志，FS头/实现及其他正式源保持；最终QEMU使用这些修正后的最新快照。
不把不同阶段的快照描述成完全相同，也不为这两处boot修改重复500轮压力。
QEMU在25秒由runner发送SIGTERM停止，并非内核主动关机。
没有伪造超过LONG_MAX的非法span，也没有追加OOM/孤儿故障矩阵；只沿用本批提前约定的P0。
本轮只新增/接线宿主测试、维护文档与隔离探针，无提交推送。

## 11. 理解题原答、集中审查与修正版（2026-11-13）

### 作者原答（保留原文）

> 复制了地址，借用的是内容，files的指针已经被复制，但是地址的内容还在被借用，调用方和函数返回的对象
> 因为offset互不干扰，未知文件不好直接返回，但是空文件在offset >= bytes.size()的时候就返回了
> 两个进程有自己独立空间，而且counter又不是只读文件共享且不能修改，文件负责数据存储，loader负责数据读取

### 集中审查

1. 描述复制/内容借用的方向正确，但复制的不只是地址：每个BuiltinFile的path和bytes两个span都按值复制，各含指针与长度。
   lookup返回FS内部file_元素地址，该地址由持续存活且不移动的BuiltinFileSystem保证；名字/内容的底层存储由提供者保证持续有效且只读。
   当前FS为boot私有长期对象，名字/about是静态数组，ELF来自永久incbin；局部files描述数组返回后可以消失。
   lookup返回const指针不延长对象寿命，也不转移所有权，不能只凭“调用方和返回的对象”推断有效期。
2. 显式offset互不干扰的判断正确：文件对象不存共享读取游标。未知文件在lookup阶段直接返回nullptr；空文件lookup仍返回有效记录，bytes.size()==0，read_at返回0。
   因而read_at的0表示本次没有读到字节（还可能是EOF或空目标），不能单凭它判断文件不存在。
3. 私有运行数据的方向正确，但独立页表本身不保证物理页不共享；当前loader确实为每份空间分配不同PA，再复制ELF的.data，counter由此独立。
   只读内置ELF是装载来源，运行中的counter在用户可写数据页，写counter不会改ELF源。
   文件层负责按名字提供/读取原始字节；loader负责按ELF布局分配页、复制段、清零BSS、设置权限和映射，不仅是读取数据。
   当前text/rodata同样各自分配；只读属性不自动意味着跨进程共享。

### 修正版（协作者据原答整理，不冒充作者再次作答）

1. init复制两个span的指针与长度，借用名字和文件字节。局部描述可以消失，底层存储须继续有效；FS对象保证返回记录的寿命，存储提供者保证记录指向内容的寿命。
2. read_at每次使用调用者提供的offset，没有共享游标。不存在由lookup的nullptr表示；空文件有有效记录、长度为0，读取返回0。
3. 同一ELF源被分别装到不同用户物理页，counter属于各自可写.data；文件层提供原始字节，loader将它们装成带权限/映射及零初始化区域的用户空间。

功能、既定P0和三题收尾均完成，不追加重答或新测试门槛。本次仅更新理解归档及导航，未改产品、未重复测试、未提交推送。
