# BATCH21：打开文件对象与文件描述符表（内核模型）

日期：2026-11-14—15。状态：功能、全部既定P0及三题原答/集中澄清/修正版归档完成，本批收尾。

B20的用户按路径显式偏移读取已完成。本批先建立跨多次调用保存位置的内核模型；
下一批再接Process与用户open/read/close，不同时修改文件模型、用户拷出和进程生命周期。
核心由作者实现，协作者集中审查并承担下面提前确定的P0。

## 1. 目标与可观察结果

把三个不同概念分开：

| 对象 | 保存什么 | 谁拥有 |
| --- | --- | --- |
| BuiltinFile | 路径和永久只读字节的描述 | 现有BuiltinFileSystem |
| OpenFile | 一次打开的文件引用、当前位置、引用计数 | 固定OpenFilePool，地址稳定 |
| FileDescriptorTable | 小整数fd到OpenFile的对应关系，每项持一个引用 | 本批由boot/宿主示例持有；下一批由Process持有 |

两次独立打开同一个BuiltinFile，必须产生两个OpenFile，位置各自从0开始。
继承一张FD表则共享原来的OpenFile，并保留fd编号；父子通过同一打开对象观察同一个位置。
关闭一个fd只放掉它的一个引用，最后一个引用释放时才清空池槽，BuiltinFile不被销毁。

启动短演示应观察到：父表读前7字节并提交→子表继承后读剩余11字节并提交→父表也到18；
另一次独立打开仍从0开始；两表全部关闭后池为空，输出：

```text
[fs] FD model OK
```

后面的原B20正常用户流程继续运行。本批没有新用户syscall，原READ_FILE14及1—13完全保持。
不实现dup、seek、写文件、CLOEXEC、目录、磁盘、VFS虚函数框架或动态内存。

## 2. 固定容量与寿命

- 每表8个fd，范围0..7；分配最小空槽。fd0合法，当前没有为标准输入/输出预留fd；原user_write仍是独立串口接口。
- 池40个OpenFile，作为当前有界资源预算（可容纳5张满表的独立打开）；文件层不包含TaskTable头或读取scheduler容量。
- 池和表都默认零初始化、禁止复制/移动；需要共享引用用inherit_from，需要转移用adopt。
- 所有表必须使用同一个指定池。池存活且不移动，FS及其记录/底层字节存活且只读，直到全部引用释放。
- get返回临时借用指针，不增加引用；调用者不能跨关闭/槽复用缓存它。池槽地址可复用，不作为长期身份。
- 本批显式close/close_all，不依靠析构偷偷处理引用。表销毁前由拥有者关闭全部fd；池销毁前须in_use()==0。
- 单hart、boot或后续SIE关闭的syscall上下文；共享读取从read_current到commit_read之间不能调度/让出。
  本批不增加锁，也不声称多线程安全；宿主按现有规则串行测试。

## 3. 新文件与完整接口

新增纯C++文件：

- `include/fs/open_file.hpp`、`kernel/fs/open_file.cpp`
- `include/fs/file_descriptor_table.hpp`、`kernel/fs/file_descriptor_table.cpp`

以下是接口与内部数据形状，作者自行实现方法。头均自包含、有include guard，实现先包含自己的头。
不在正式仓库预先铺实现框架。类型位于tinyos::fs。

### open_file.hpp

```cpp
#ifndef TINYOS_FS_OPEN_FILE_HPP
#define TINYOS_FS_OPEN_FILE_HPP

#include <stddef.h>
#include <span>

namespace tinyos::fs {
struct BuiltinFile;

class OpenFile {
public:
    constexpr OpenFile() = default;
    OpenFile(const OpenFile&) = delete;
    OpenFile& operator=(const OpenFile&) = delete;
    OpenFile(OpenFile&&) = delete;
    OpenFile& operator=(OpenFile&&) = delete;

    const BuiltinFile* file() const noexcept;
    size_t offset() const noexcept;
    size_t reference_count() const noexcept;
    long read_current(std::span<unsigned char> destination) const noexcept;
    int commit_read(size_t count) noexcept;

private:
    friend class OpenFilePool;
    const BuiltinFile* file_{};
    size_t offset_{};
    size_t reference_count_{};
};

class OpenFilePool {
public:
    static constexpr size_t kCapacity = 40;
    constexpr OpenFilePool() = default;
    OpenFilePool(const OpenFilePool&) = delete;
    OpenFilePool& operator=(const OpenFilePool&) = delete;
    OpenFilePool(OpenFilePool&&) = delete;
    OpenFilePool& operator=(OpenFilePool&&) = delete;

    OpenFile* acquire(const BuiltinFile& file) noexcept;
    bool retain(OpenFile* file) noexcept;
    void release(OpenFile* file) noexcept;
    size_t in_use() const noexcept;

private:
    OpenFile files_[kCapacity]{};
};
}

#endif
```

### file_descriptor_table.hpp

```cpp
#ifndef TINYOS_FS_FILE_DESCRIPTOR_TABLE_HPP
#define TINYOS_FS_FILE_DESCRIPTOR_TABLE_HPP

#include <stddef.h>

namespace tinyos::fs {
struct BuiltinFile;
class OpenFile;
class OpenFilePool;

class FileDescriptorTable {
public:
    static constexpr size_t kCapacity = 8;
    constexpr FileDescriptorTable() = default;
    FileDescriptorTable(const FileDescriptorTable&) = delete;
    FileDescriptorTable& operator=(const FileDescriptorTable&) = delete;
    FileDescriptorTable(FileDescriptorTable&&) = delete;
    FileDescriptorTable& operator=(FileDescriptorTable&&) = delete;

    int open(OpenFilePool& pool, const BuiltinFile& file) noexcept;
    int close(OpenFilePool& pool, int fd) noexcept;
    void close_all(OpenFilePool& pool) noexcept;
    OpenFile* get(int fd) noexcept;
    const OpenFile* get(int fd) const noexcept;
    bool empty() const noexcept;

    int inherit_from(OpenFilePool& pool, const FileDescriptorTable& source) noexcept;
    void adopt(FileDescriptorTable& source) noexcept;

private:
    OpenFile* entries_[kCapacity]{};
};
}

#endif
```

### 各方法的行为与失败状态

| 方法 | 契约 |
| --- | --- |
| acquire | 找空池槽，file指向传入的长期记录、offset0、引用1；池满返回nullptr，不改变任何原槽 |
| retain | 参数须是本池存活对象；引用未溢出时+1并返回true，计数溢出返回false且保持原值 |
| release | 参数须是本池存活且引用>0的对象；减1，减到0时file/offset/计数全部清零；不释放底层文件 |
| in_use | 返回引用>0的池槽数量，不是总引用数 |
| open | 先找空fd再acquire；满表/满池返回-1且表/池保持；成功发布指针并返回fd |
| get | fd负数、>=8或未打开均返回nullptr；检查下标后才访问数组 |
| close | 非法/未打开fd返回-1且保持；成功释放一个引用并清空对应fd，返回0 |
| close_all | 释放每个非空fd并清空，重复调用安全，不改其他表的引用 |
| empty | 所有fd都为空才为true |
| inherit_from | 目标必须空且不是source自身；否则-1。给每个源fd增加一个引用，成功后保留原fd编号；失败目标仍空，源及原引用计数保持 |
| adopt | 内部无失败转移：目标必须空且与source不同，将所有指针移交并清空source，不增减引用 |

retain/release的对象来自池和表内部，不是用户传来的裸指针；错误池、悬空对象或adopt前置条件违反属于程序员错误，
可用现有内部断言/致命不变量处理，不能悄悄覆盖或下溢。用户fd的非法情况则必须正常返回错误，不能靠断言。
池成员归属若要检查，可在固定数组中逐项比较指针；不要对无关指针做相减或先解引用再判归属。
原头里的OpenFile默认对象不是一次打开；没有file时read_current/commit_read返回-1。

继承无需分配新OpenFile，也不复制offset。可以先将成功retain的指针登记在局部固定数组；
如中途失败，通过release撤销这些新增引用；全成功后才把指针发布到目标表。
release的前置条件由这份刚成功retain的账本保证，不能丢弃已增加引用。
无dup的当前API会保持每张表内的打开对象各不相同；不同表之间允许共享。

## 4. 为什么读取分成两步

```text
OpenFile::read_current(buffer) → 按当前offset读取，不推进
成功处理读出的count字节
OpenFile::commit_read(count) → 推进实际count
```

read_current复用BuiltinFile::read_at(offset_,destination)，保持文件字节和offset不变。
commit_read要求对象已打开且count不超过当前文件剩余字节，失败保持offset；成功offset+=count，0合法。
先确认offset<=文件长度，再用减法算remaining，避免加法溢出。正常方法始终维护offset<=bytes.size()。
调用方必须提交刚刚实际成功处理的数量，不能把请求长度当实际数量。

本批boot/宿主直接验证内核buffer后提交；下一批用户read只有user_copy_to全部成功后才提交。
若拷出失败，用户buffer可能已有合法前缀改变，但打开位置仍不变，因此可从原位置重试。
SIE关闭且中间不让出，保证另一个共享者不会在这两步之间改变offset；未来并发方案另行设计。

## 5. boot短演示与实现顺序

在boot文件查找完成后、用户调度前，使用已查到且检查非空的about记录。建议写boot私有助手，主体保持清晰。
模型对象可以是这个助手中的局部pool/parent/child；声明pool先于两表，且返回前显式关闭全部引用。
FS是原长期对象，不在助手中临时复制或重建文件表。

1. 新建空pool、parent表、child表，要求池0/表空。
2. parent两次open同一个about，要求fd0和fd1；指针不同、位置均0。
3. 经fd0读7字节，检查数量/内容后commit7；fd1仍为0。
4. child.inherit_from(pool,parent)，要求两个fd编号保持、各对应同一个打开对象；in_use仍2，每对象引用2。
5. 经child的fd0读11字节并commit11，拼接内容等于18字节about；parent的fd0也到18，parent/child的fd1仍0。
6. parent.close(pool,0)，child.fd0仍有效且位置18；其最后一个引用由child后续关闭。
7. 两表close_all后都空、池in_use0；重复close_all保持。打印FD model OK，继续原B20启动流程。

按顺序实现：OpenFile/Pool→FD表open/get/close→继承/移交→boot演示与Make两个对象。
不修改Process、TaskTable、scheduler、file_syscall或用户程序；下一批再接它们。
主Make加入kernel/fs/open_file.o及kernel/fs/file_descriptor_table.o；宿主接线与测试由协作者承担。
完整任务现在给齐，作者可以整批写完再集中检查，不追加逐行任务。

## 6. 固定验收与责任

P0由协作者承担，作者核心完成后执行：

1. RV64/宿主真实模块构建及头自包含、不可复制/移动约束通过；原make test保持。
2. 独立open位置独立，inherit共享位置/引用且保留fd编号；read_current不推进，commit只推进实际数量，EOF/空文件/非法commit保持契约。
3. 非法fd（-1/8）及重复close拒绝；最小空fd可复用；关闭一表不使另一表失效，最后close清空对象，close_all幂等。
4. 满8个fd后第9次open失败、池用量不增加；池40槽用满后新表open失败且原引用/位置保持，关闭一个后可重试。
5. 继承到非空表/自继承拒绝且无变化；adopt转移后源空、对象/offset/引用保持。引用溢出作代码审查，不伪造非法对象或追加穷举测试。
6. 真实QEMU boot演示通过，模型过程PMM0且全部关闭后池空；原B20两个worker文件读取/退出23/37父0及正常35free/21运行alloc/reap3零PMM保持。

P1选做：画出“一个BuiltinFile、两个独立OpenFile、两张共享FD表”的引用图，或手工追踪关闭顺序，无需新框架。
P2选做：确定性随机open/close/继承、更多容量组合或500轮任务压力；不实现不阻塞下一批。
不添加本批未涉及的用户指针/FAULT/EXEC故障矩阵。

```sh
make -j4
make test
timeout 20s make qemu
git diff --check
```

例行宿主与隔离QEMU由协作者维护；不clean、不提交推送、不改变正式产品来让探针通过。

## 7. 下一批的生命周期约定（本批不实施）

提前写明以避免模型返工：

| 事件 | 下一批目标 |
| --- | --- |
| 新建/SPAWN | 空FD表 |
| FORK | 保留fd编号，共享OpenFile/当前位置；增加引用 |
| EXEC | 保留FD；暂不支持CLOEXEC |
| EXIT/FAULT | 关闭全部FD，之后回收任务槽不再持文件引用 |
| RESET/REAP | 要求FD已经空，不能默默覆盖而泄漏引用 |

当前Process::release_resources同时用于EXEC旧地址空间释放和EXIT，不能直接在它里面统一close_all。
未来应区分“释放地址空间”和“结束进程关闭文件”的职责；不让exec意外清掉FD。
fork当前allocate后是无失败发布阶段：未来先准备临时FD继承，再allocate；失败关闭临时引用，成功以adopt无失败移交，
不能复制Process对象或在Task分配成功后加入可失败的继承步骤、消耗ID留下半任务。
具体Process接口/服务绑定/新增ABI与用户演示属于下一批完整交付，不在本批暗中增加工作量。

## 8. 完成后梳理与三题

功能/P0后梳理：永久文件→池中一次打开→表中fd引用→准备读取/提交位置→共享继承→最后引用关闭。

1. 同一个文件独立open两次，与继承一张FD表，分别共享哪些对象和状态？fd数值相同是否代表同一个打开对象？
2. 为什么read_current成功后不能立即假定用户read完成？拷出失败时位置与用户buffer各应该怎样？
3. 父表关闭fd0后子表为何还能读？最后关闭时回收什么，哪些文件内容仍然存在？

原答/审查/修正版已在第17节归档，不要求重复作答。
配套阅读可对照《操作系统：设计与实现》第三版第5章的文件操作及文件系统实现中打开文件表/进程表的讨论，
带着“文件本身、一次打开、进程里的fd为何不是同一个东西”去读；中文页码/更细节号未核，不猜页码，不作为前置作业。

## 9. 交付前准备与限制

独立只读设计审查建议拆分内核模型与Process/ABI接线，已采纳；未修改产品或增加未声明的验收矩阵。
将第3节建议头提取到`.cache/batch21-preparation-20261114/`，RV64 freestanding与native C++20在-Werror下均语法通过。
检查包含自足/重复包含、constinit池/表、禁止复制移动及open/get/read_current/commit/close_all调用形状。
87个正式源码/构建配置hash保持；证据results.json及rv64/native.log。
这只是接口可编译性准备，没有实现方法、没有完整链接或运行本批P0，不代表B21已实现。

## 10. OpenFile/Pool局部草稿审查（2026-11-14）

作者请求检查open_file.cpp，实读该文件和头后集中发现：

1. acquire设置reference_count_=-1，size_t实际变SIZE_MAX，应为1。这使首次retain就因溢出拒绝，普通一次release也不能释放对象。
2. release入口读取file->reference_count_，尚未确认指针属于本池；应先在files_中比较对象地址，找到后再检查/修改该entry。
3. release循环未命中后竟清空传入file的三个字段，会破坏另一池的活对象；未命中应按内部错误诊断，不得写外部对象。
   命中非活对象的分支也只有return，和注释的断言/停机不一致；遵守原任务内部不变量错误策略，不把非法释放当作成功。
4. cpp使用SIZE_MAX却未直接包含stdint.h。RV64的自供span恰好传递包含它，所以目标语法通过；native标准span不保证这项依赖，原始宿主编译实际报SIZE_MAX未声明。

read_current/commit_read的偏移、边界和失败保持，以及retain的按对象地址匹配/递增/溢出保持、in_use的占用对象数语义无其他正常路径问题。
头的commit_read(size_t const)是合法的未命名按值参数声明，不是签名错误；可按可读性改为size_t count，不作为新增功能门槛。
独立只读审查结论一致，未代改产品。

局部诊断证据`.cache/batch21-open-file-review-20261114/`：原native构建失败日志、目标语法日志和真实模块小复现。
为暴露剩余逻辑错误，仅诊断编译命令强制包含stdint.h，未修改作者源码，也不据此声称原宿主构建通过。
复现实际观察到初始SIZE_MAX、首次retain失败、单次release对象仍活、错误池release清空原池对象；read/commit基本路径正确。
相关四个产品文件hash保持，未运行整批宿主/QEMU/压力，未接主Make或代写后续FD表。
下一步作者集中修正本节后继续原B21任务，固定P0/三题范围保持。

## 11. FS当前实现集中审查（2026-11-14）

作者请求检查整个fs，实读include/fs与kernel/fs，独立只读复核一致。旧B19/B20六个FS文件与已验收快照hash相同，未重复无关回归。
当前新B21草稿的集中问题如下：

1. acquire现在offset_和reference_count_均为-1，应分别0和1。size_t会把两者变SIZE_MAX，初次read_current返回EOF0，commit_read(0)拒绝，引用retain也拒绝。
2. FD open直接将acquire返回写入entries并返回fd，未检查池满nullptr；会报告成功但对应fd仍为空。须用局部指针判空后才发布并返回显式int转换。
3. close和get（包括待写的const版本）必须先检查fd<0或fd>=8，再访问entries；非法fd按接口正常拒绝，不能用内部断言替代。
4. const get头声明成const OpenFile&，应为const OpenFile*，以便非法/未打开返回nullptr，并补对应定义。
5. release归属前解引用/未命中清外池对象两项已修；非法对象仍静默返回，应按原任务内部不变量诊断策略处理。
6. FD cpp直接调用kernel_panic却没有显式声明。RV64恰由自供span传递得到声明而语法过，native标准span没有，实际编译失败。
   协作者前一份close_all示例漏交代此依赖，现明确补充：在调用的cpp显式声明正确extern "C"签名，或包含正式共用panic接口；不依赖span顺带导出宏/符号。

read_current/commit_read、retain有效路径/in_use和empty逻辑正确，close_all遍历/重复调用的设计正确。
inherit_from/adopt/const get定义与Make/boot演示尚未完成，按阶段列待做，不当作作者已完成实现的新错误。

证据`.cache/batch21-fs-review-20261114/results.json`：两新cpp目标语法过；open_file原native语法过，FD原native因panic未声明失败。
仅诊断命令强制提供panic声明及宿主abort桩，未修产品逻辑，真实模块复现初始两项SIZE_MAX、池满open返回0但get0为空。
ASan/UBSan分别确认get(8)和close(-1)越界；因沙箱ptrace限制关闭仅LSan泄漏检测，地址/未定义检查仍启用。
这些是错误复现，不是B21宿主通过声明；相关FS/Make/boot源hash保持，无产品代改/全P0/QEMU/压力/提交推送。
集中修正后继续既定继承/移交及boot/Make阶段，不追加新矩阵或三题。

## 12. 共用panic接口已接，当前剩余两处基础错误（2026-11-14）

作者新增runtime/panic.h，C/C++分别声明noreturn/C链接并统一PANIC_KERNEL；panic.c和两模型cpp已包含，局部重复声明/宏已删除。
C11和RV64 C++重复包含、panic实现、两模型target/native语法均通过，无诊断。release现在对null/非活/未归属实际panic；
close/get边界及const get指针返回和定义已正确，acquire offset已为0。

当前集中剩余：

1. acquire把reference_count_改成了0，仍应为1。返回给调用者时已经有一个引用，否则retain拒绝、release将panic、in_use错误显示0。
2. FD open修补池满判空时丢了原entries_[i]空槽判断，循环每次直接分配并return0，会覆盖已占用fd0并丢失旧引用。
   应先遇非空槽continue，空槽中再acquire/判空/发布，满表时不调用acquire而返回-1。

小复现用真实三模块及正常宿主panic桩，无强制补声明或产品修复，观察first_fd0/second_fd0、fd0对象被覆盖、fd1仍空、pool.in_use0。
证据`.cache/batch21-fix-review-20261114/results.json`与log，八个相关产品文件hash保持。
这是错误复现与语法通过，不是整批P0；继承/adopt与boot/Make仍按原任务继续，未改产品/提交推送。

## 13. 池与FD表基础修正复验通过（2026-11-14）

最新实读acquire为offset0/ref1；open恢复先跳过非空槽、池判空后发布。此前两处当前问题已修。
局部真实模块宿主回归通过：初始引用/retain-release最后清空、独立open返回0/1且对象不同、read不推进/commit推进、
两版get及close的非法fd、关闭后最小槽复用、表满不分配、池满open失败且表保持空、close_all幂等。
ASan/UBSan未发现问题；仅LSan因沙箱ptrace限制关闭。使用正常宿主panic abort桩，无修复产品或强制补声明。

证据`.cache/batch21-basic-check-20261114/results.json`与build/run.log，七个相关源hash保持。
尚未实现的inherit_from/adopt、boot演示与主Make接线仍按原任务继续；该局部通过不表示整批P0或QEMU已通过。
下一步可先写无失败adopt的空目标/非自身前置检查与逐项移交，再写inherit临时增加引用/失败回滚/全成后发布。
两者分别是移动拥有关系与共享已有对象，前者不增减引用，后者不acquire新对象。三题仍留功能/P0后，不追加范围。

## 14. 继承/移交与宿主P0通过，下一步boot接线（2026-11-14）

作者已完成adopt和inherit_from。adopt先检查非自身/空目标，逐项移交并清源；inherit用局部pending表按源fd位置登记成功retain的引用，
中途失败close_all撤销新增引用，全成后adopt发布。源表始终保持，未acquire新对象；前一草稿的空span、条件反向及空洞索引问题已修。

真实RV64两模型实现和头重复包含/constinit/不可复制移动/继承移交调用形状无诊断。
协作者持久化test/test_open_file.cpp并接宿主CMake/test_main/README，真实FS/模型六组及原suite通过，无构建诊断：

- read_current不推进，commit实际数量推进；EOF、空文件及非法commit保持。
- 独立打开位置独立，继承共享位置/引用、保留fd空洞，自继承/非空目标拒绝且保持。
- 非法fd/重复close拒绝、最小槽复用、8槽满及40对象满失败保持/释放后重试。
- adopt移交不增加引用，关闭一表后另一表仍有效，最后关闭清空池对象，close_all幂等。
- 静态类型约束禁止复制/移动，逐组核模型无PMM分配/unmap/flush/free。

单一宿主kernel_panic桩在新测试文件中经正式panic头声明，直接abort，不用异常穿过noexcept。
引用计数溢出只审查SIZE_MAX检查先于递增，未伪造私有字段/新增故障矩阵，未跑500压力。
证据`.cache/batch21-final-20261114/host/results.json`及构建/测试log，92个正式源配置在验证期间hash保持。
目标阶段语法证据`target-stage/results.json`，该阶段不冒充目标链接或QEMU通过。

作者接下来完成原第5节：主Make加入两个模型.o，在boot中对已有about记录作7+继承11/独立打开/全关闭演示。
可在读取about的i==2分支中调用boot私有助手，借用当前已检查的result记录；不新建FS或改Process/用户ABI。
随后协作者执行第6节剩余真实QEMU/资源P0，再一次讨论原三题。尚未整批收尾，无本轮产品代改/提交推送。

## 15. boot与主Make接线检查（2026-11-15）

作者日志已出现FD model OK与调度启动。实读helper及调用：在about读取分支借用已有长期记录，局部池/两表，
两独立open、7字节读/提交、继承两个引用、共享再读11/提交到18、父关闭一引用、全关池空及重复关闭均已写；主Make两个.o已接。
当前make -j4无诊断/nm-u空，make test再次全过无诊断；证据`.cache/batch21-final-20261115/{build,host-current}/results.json`。

helper尚未显式比较buffer内容，协作者已建议补循环。最终只读探针会独立观察真实read返回的字节与位置/引用/资源，
以此验证既定内容正确性，不把额外测试断言的存在性作为新门槛，也不在副本中修作者逻辑。
最终normal探针进行中，将保留原B20实际读取和35页资源链；完整P0及三题尚未收尾，结果待下一节归档。

## 16. 最终功能与全部既定P0通过（2026-11-15）

第15节的运行已完成：最新正式源快照的真实正常QEMU31项全部通过，功能与全部既定P0验收通过。
第8节原三题随后已作答并在第17节集中澄清，功能/P0与理解归档现均已完成。

| 验证范围 | 实际结果 |
| --- | --- |
| 原六组宿主模型 | 真实FS/模型、容量/失败保持/继承空洞/adopt/最后关闭等通过；11月15日当前make test再次全过，无诊断 |
| 实际boot读取 | 两次read_current返回7/11、真实字节拼成TinyOS builtin fs加换行；准备读取时位置0/7不变，提交后7/18 |
| 独立与共享 | 两次open对象不同，第二对象位置保持0；继承后引用2，父关闭一引用后子持引用1 |
| 最后关闭 | 四次实际close后两对象清空、两表empty、pool.in_use0，重复close_all保持 |
| 架构/资源 | 模型全过程PMM分配/释放0，satp保持；原B20四次真实用户read、身份与0PMM保持 |
| 原进程账本 | 五份空间各7页、35次合法free、运行分配21、净恢复14、3次reap零PMM，双WORKER23/37、父0、最后两S ticks |
| 构建/完整性 | 正式make和隔离完整构建无诊断、无未定义符号；92正式源配置在最终验证期间hash保持且复核无漂移 |

统一证据`.cache/batch21-final-20261115/summary.json`；本次build/、host-current/和normal/各含results及log，
原六组持久宿主证据仍保留`.cache/batch21-final-20261114/host/`。
normal/run_probe.py在真实操作的返回/提交/关闭处追加只读观察，没有修复模型实现或替换boot操作。
正式helper当前只检查数量、位置和引用；字节准确性由探针独立核对，不声称helper已有memcmp，补显式比较仅为建议。
QEMU由runner在25秒外部SIGTERM结束，非内核主动关机；未扩大OOM/孤儿/500压力矩阵，无提交推送。

本批执行路径：lookup取得永久文件记录→表从池申请一次打开→fd引用OpenFile→read_current准备字节→commit推进位置→
继承给相同对象增加引用→close逐个撤销引用→最后引用归零清空池槽，底层文件记录/字节仍在。
该模型目前由boot使用，未把FD表接到Process或新增open/read/close用户ABI；下一批单独完成生命周期接线。
接下来只讨论原三题一次，保留原答并集中澄清，不增加编码/测试门槛。

## 17. 三题原答、集中澄清与修正版（2026-11-15）

### 作者原答（按原内容排版）

第一题：

> 独立open两次会共享一个POOL，继承FD是共享文件，应该是

第二题：

> read_current之后还需要提交读取的字符数量并校验offset的位置是否正确，拷贝失败offset不变，buffer维持之前的清空不动，

```cpp
if (count > static_cast<size_t>(__LONG_MAX__)) {
    return -1;
}
```

> 此处没有说失败清空buffer，因为还没开始复制

第三题：

> 父表和子表不是直接保存同一个对象，只是父表哪里无法访问了而已，子表依然持有相关文件的指针，最后一个引用关闭的时候

```cpp
entry.file_ = nullptr;
entry.offset_ = 0;
entry.reference_count_ = 0;
```

> ，我不知道

### 集中澄清

1. 当前示例确实共用池，也借用相同BuiltinFile；独立open产生两个不同OpenFile、各有offset。
   继承额外共享的是同一个OpenFile及其当前位置，引用从1增至2。父子FD表仍是两个对象，表项复制指针。
   fd只是各表内部下标；数值相同不单独证明指向同一个打开对象。
2. 拷出失败不提交offset的判断正确。LONG_MAX检查属于read_at复制前的拒绝分支，只有这种分支能保证目标未写。
   read_current成功已将字节放进内核buffer；下一步user_copy_to途中失败，用户buffer合法前缀可能已覆盖，未写部分保持，不会清空或自动回滚。
   未来用户read须在完整拷出成功后commit实际count，不能在read_current一成功就推进位置。
3. “表保存指针而不是对象本体”正确，但父子指针指向同一个OpenFile，而非各自一份打开对象。
   父close先release使引用2→1，再清父表项；子表项和打开对象仍有效。
   最后close使引用1→0，清空OpenFile的文件指针、位置与计数，将固定池槽恢复可复用。
   这不是free/delete整个池槽的存储，也不是销毁BuiltinFile；FS中的记录、路径字符与永久文件字节仍存活，之后可重新open。

### 修正版（协作者据原答整理，不冒充作者重答）

1. 独立打开共享底层文件，OpenFile/offset各自独立；继承共享已有OpenFile及offset，双方表项各持一个引用，fd编号只在各自表内有意义。
2. read_current仅准备内核数据，完整user_copy_to成功才提交实际count；拷出失败位置保持，但用户buffer可能已有前缀改变。
3. 父关闭只移除自己的引用，子引用使打开对象继续存活；末次关闭清空并归还OpenFile池槽，底层文件和字节不受影响。

三题已集中说明，不追加重答或测试；功能、既定P0与理解归档均完成，本批收尾。
本次只更新理解归档/导航/记忆，未修改产品或重复测试，未提交推送。
