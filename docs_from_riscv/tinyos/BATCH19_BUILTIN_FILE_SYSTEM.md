# BATCH19：只读内置文件与按偏移读取

日期：2026-11-12—13。状态：完整任务已交付，作者已创建头/实现并完成read_at草稿审查；init/lookup及构建/boot接线未完成，未运行本批功能/P0。

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

原答、审查和修正版在本文件继续归档，目前未作答。

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
