# BATCH20：用户按路径、显式偏移读取文件

日期：2026-11-13—14。状态：功能、全部既定P0及三题原答/集中澄清归档完成，本批收尾。

B19功能、既定P0和三题已收尾。本批将只读文件接到用户态，学习一次完整的用户输入/输出边界。
核心由作者实现；协作者集中审查、维护必要测试并承担下面预先确定的P0。

## 1. 目标与范围

用户能够调用：

```c
long user_read_file(
    const char* path, size_t path_length, uint64_t offset,
    void* destination, size_t length);
```

例如从`/etc/about`偏移0读7字节，再从偏移7读剩余11字节，拼成18字节的`TinyOS builtin fs\n`。
每次显式给offset，返回实际字节数；不保存共享位置。

本批不引入FD、open/close、目录树、磁盘或路径式EXEC，不改Process/Task/fork/exec的拥有关系。
后续打开文件状态再决定独立打开与fork继承时的位置共享规则。

## 2. ABI与固定行为

`include/task/syscall.h`新增`SYSCALL_READ_FILE 14`、`SYSCALL_READ_FILE_MAX 256`；原1—13编号和行为保持。

| 寄存器 | 输入 |
| --- | --- |
| a0 | 路径的用户虚地址 |
| a1 | 路径字节数，不含终止NUL |
| a2 | uint64_t文件偏移，单位字节 |
| a3 | 目标用户虚地址 |
| a4 | 最多读取字节数 |
| a7 | 14 |

返回a0：非负为实际读取数量（0表示本次无字节），-1表示错误；本次不扩展errno。

契约与检查顺序提前固定：

1. 文件服务已配置、当前为合法运行USER且可取得root；路径长度非0且不超过127，读取长度不超过256。
   在任何索引/窄化/复制之前检查长度。路径长度1随后由lookup规则拒绝，无需另写一套路径检查。
2. 用user_copy_from把指定长度的路径复制进内核固定数组，再调用lookup；沿用B19的完整绝对路径规则。
   输入span不包含终止NUL；包含NUL、未知名及其他非法路径均返回-1。不扫描用户内存寻找NUL。
3. 查到文件后，length==0或offset>=文件长度返回0，不访问destination。
   因而未知路径即使length==0仍返回-1；length>256即使offset已到EOF也返回-1。
4. 其余情况read_at到固定内核buffer，再只将实际返回的count字节通过user_copy_to送回用户。
   count非负且<=本次length；内部异常结果返回-1，不将负数转换成巨大无符号长度。
5. 完整拷出成功才返回count；拷出失败返回-1。沿用现有user_copy_to语义，失败时用户buffer可能已有合法前缀改变。
   本接口无文件游标，也不分配资源；不承诺失败时用户buffer原子不变，不为此增加一轮页表预扫描。

目标只需容纳实际count。例如文件剩余2字节，即使length为256，也只校验/写2字节。
EOF、空文件和零length不检查无用的目标指针。所有情况下都先检查并查找路径。
offset保留完整uint64_t；先与文件长度比较，大偏移直接EOF，不计算offset+length。
当前RV64的size_t为64位；转换前保证值可表示，例如offset<bytes.size()后再转size_t。

## 3. 模块边界与依赖

新增三个文件：

- `include/fs/file_syscall.h`：C/C++共用的桥接声明，自包含、include guard、自行管理extern "C"。
- `include/fs/file_syscall.hpp`：纯C++的一次配置声明，自包含、include guard。
- `kernel/fs/file_syscall.cpp`：服务绑定、用户路径复制、查找、暂存和拷出。

C桥接接口（头中包含stddef/stdint及现有mm/vm.h以取得正确类型）：

```c
long fs_syscall_read_file(
    const Sv39PageTable* root,
    uintptr_t path_address, uint64_t path_length, uint64_t offset,
    uintptr_t destination_address, uint64_t length);
```

实现保持与声明一致的C链接；可以先包含本模块的.h和.hpp，再其他依赖。
不把用户地址reinterpret_cast成内核span；原始地址只能交给user_copy_from/to解释。
root由syscall.c从sched_current_user_root取得并显式传入，文件服务不自行读取scheduler全局状态。

C++配置接口：

```cpp
namespace tinyos::fs {
class BuiltinFileSystem;
int configure_file_syscalls(const BuiltinFileSystem& filesystem) noexcept;
}
```

为BuiltinFileSystem增加只读`bool initialized() const noexcept`，返回现有initialized_；无需公开内部数组。
配置拒绝未初始化对象或重复配置，失败保持原绑定；成功仅保存借用的const指针，不复制/移动FS。
指针留在file_syscall.cpp匿名命名空间，初始nullptr。被绑定对象及底层名字/字节须在服务使用期间持续有效。
这是文件服务自身拥有的一次启动配置，不把boot对象或scheduler状态变为公开全局。

boot继续拥有私有g_builtin_fs：init成功后、用户调度启动前调用configure_file_syscalls并检查结果。
配置函数不负责创建FS、不分配页、不激活root；调用上下文为现有SIE关闭的boot阶段。

## 4. 实现路径和关键骨架

不再复制路径校验或页表遍历；复用已有lookup/read_at和user_copy_from/to。
handler内使用`char path_buffer[kMaxBuiltinPathLength]{}`及`unsigned char buffer[SYSCALL_READ_FILE_MAX]{}`。
这些是实际内核存储；span仅描述本次有效范围，不会创建缓冲区。

```text
检查配置/root/两种长度
  → copy_from(用户路径 → 内核path_buffer)
  → lookup(内核路径span)
  → 处理合法文件的零length/EOF
  → read_at(offset, 内核buffer的本次length范围)
  → 检查返回count
  → copy_to(内核buffer的count字节 → 用户目标)
  → 返回count
```

syscall.c增加一项case：从frame取a0..a4，以完整宽度传递，取当前用户root，调用C桥接，结果写回a0后break。
沿用dispatcher统一sepc+=4，不另加一次；这是普通返回调用，不更换frame。

user/user_api.h增加前述user_read_file声明；user/syscall.S沿用现有包装方式：

```asm
.global user_read_file
user_read_file:
    li a7, SYSCALL_READ_FILE
    ecall
    ret
```

五个参数本来就在a0..a4，包装不重新搬寄存器。主Make显式加入kernel/fs/file_syscall.o。
宿主CMake/桥接测试由协作者在作者完成后接线，不要求作者搭测试框架。

## 5. 正常演示与实现顺序

DEMO代码已接近一页上限，本批将正常读取演示放进仍有余量的user/worker.c，不扩大用户代码布局。
可以写私有check_file_read助手，在main中调用，保持原counter/BSS/栈校验、yield与退出argument+20。

正常演示一次完成：

1. 局部数组存路径`/etc/about`和18字节目标；路径长度为sizeof(path)-1。
2. offset0/length7读到前7字节；offset7/length11写到目标+7，分别要求返回7和11。
3. 逐字节与预期18字节比较，不把目标当NUL字符串；然后打印`[worker] file read OK\n`。

正式正常运行有两份WORKER，因此成功日志应出现两次，原fresh image成功和退出23/37/父0保持。
未知路径、无效用户指针等拒绝场景由协作者的隔离P0覆盖，不要求作者塞满worker。

建议先写两个接口头与配置/getter，再实现C桥接主体，最后接boot/Make/syscall/用户包装及worker。
核心由作者写；协作者在整批完成后一次集中指出发现的问题。

## 6. 预先确定的验收与责任

P0由协作者承担，不是作者额外测试作业：

1. 目标构建、接口自包含/C链接与原make test通过；配置前拒绝、未初始化绑定失败后可正确绑定、重复绑定保持原对象。
2. 真实U-mode正常读取：about分段7+11内容准确、两个ELF前4字节magic准确；短读只复制实际字节，目标尾哨兵保持。
3. 固定边界/拒绝：EOF及UINT64_MAX偏移为0，已知路径零length允许无效目标；未知路径即使零length仍-1；
   路径长度0/128、读取257、不可读路径地址、不可写目标均-1。空文件沿用B19真实算法测试，必要时仅测试副本登记空记录。
4. 一条跨页拷出失败验证返回-1、无kernel fault；允许目标合法前缀变化，不声称原子失败。
5. 新读取过程零PMM、无文件状态修改；正常双WORKER及原35free/21运行alloc/三次reap零PMM、最后两S ticks保持。

P1选做：用两个调用者交错offset，跟踪每次目的缓冲区变化；可只讨论，无需另建框架。
P2选做：更多路径/边界随机组合、重复500轮任务压力；本批未改任务生命周期，不将它们自动作为推进门槛。
不新加全矩阵、FD继承、磁盘或EXEC任务。

```sh
make -j4
make test
timeout 20s make qemu
git diff --check
```

协作者必要的隔离QEMU探针保存.cache并记录快照/范围；不为探针修正产品副本以冒充作者代码已通过。
不需要clean，不提交或推送除非用户另有请求。

## 7. 完成后梳理与原三题

功能/P0后梳理：用户路径地址→内核路径快照→文件记录→内核字节buffer→用户目标；
区分“复制路径成功”“读到文件数据”和“成功送到用户内存”三个时点。

1. 为什么不能直接把用户path地址构造成span交给lookup？复制后为什么不需要补NUL？
2. 文件只剩2字节而请求256字节时应校验/写多少目标内存？如果跨页拷出失败，文件状态和目标buffer各可能怎样？
3. 为什么本接口不需要往Process放offset？未来open/read相较本接口需要增加什么状态、由谁拥有？

原计划三题在全部P0后讨论；作者已主动回答，原答和集中澄清归档于第10节，不要求重答。随后正常资源P0亦已通过，见第11节。

配套阅读仍可对照第三版第5章5.1的文件访问/操作：本批显式offset与打开文件的当前位置有何区别。
中文页码未核对；阅读不作为编码前置作业。

## 8. 首轮主体与接线审查（2026-11-14）

作者已实现initialized()、一次绑定、C桥接与syscall14的a0..a4分发/API/汇编包装。
配置的全预检后保存指针、头的C链接、用户包装和寄存器映射静态正确。
真实RV64 freestanding C++20桥接语法、C头重复包含及C++两头重复包含均无诊断；git diff --check通过。

整批集中发现以下待修，不代改作者产品：

1. fs_syscall_read_file入口缺g_filesystem非空、path_length上界127、length上界256。
   路径复制可写爆127字节内核数组；读取大ELF可写爆256字节暂存数组，须在复制/构span前检查。
2. 入口的length==0、offset>=length和destination_address==0拒绝不符合既定契约，应删除。
   offset与请求长度无大小限制关系，例如offset7/length4应合法读取；只在找到文件后用offset>=bytes.size判断EOF。
   合法零length/EOF返回0且不访问目标；正count的目标由user_copy_to检查。
3. cpp局部重复声明SYSCALL_READ_FILE_MAX，改包含共享task/syscall.h并删除本地同名constexpr。
   采用宏后buffer大小使用SYSCALL_READ_FILE_MAX，不再写tinyos::fs::限定。
4. 主Make缺kernel/fs/file_syscall.o。实际make -j4在syscall.c:250引用处链接undefined reference to fs_syscall_read_file，未运行QEMU。
5. boot还未include file_syscall.hpp及在FS init成功后调用/检查configure_file_syscalls，服务尚未绑定。
6. worker未增加原任务要求的about分段7+11、逐字节比较和file read OK日志。

read_at当前保证只返回-1或[0,length]，所以count==-1目前能处理实际错误；可依原契约顺带统一写count<0，
内部异常数量检查不作为新增场景/门槛。C头显式包含自身所需标准头、hpp不顺带导出C头属依赖整理，优先级低于以上功能项。

证据`.cache/batch20-review-20261114/results.json`和各语法检查log。
12个相关产品/构建文件在独立语法检查期间hash保持；主线构建失败与未接线事实不等于B20运行通过。
本轮只审查、运行构建/语法检查及维护文档，没有代改产品、运行完整P0/压力或提交推送。
下一步作者集中修正上述项，再由协作者执行原第6节既定P0，不追加矩阵或提前下发三题。

随后复查：上述边界、共享常量及三处接线均已由作者补齐。目标make -j4成功、最终nm -u为空，
桥接/两头RV64 C/C++语法检查无诊断，WORKER的RX为1465字节、RW内存4152字节，仍在原布局内。
正常演示预期文本误写`Tinyos builtin fs\n`，与实际`TinyOS builtin fs\n`大小写不同；已告作者修正，未代改。
该差异会使真实内容校验失败，不修测试副本来冒充正式正常路径通过。
协作者继续独立进行服务宿主验证及隔离真实U-mode固定边界P0，正常资源专项待作者修好后运行。
当前构建/头证据`.cache/batch20-final-20261114/build/results.json`；完整P0尚未汇总，不提前下发三题。

## 9. 宿主与真实用户边界验证通过，正常演示待一处修正（2026-11-14）

协作者已在test/test_file_syscall.cpp增加三组持久测试，并接宿主CMake/test_main/README。
真实file_syscall/FS/usercopy/VM模块全部参与：未绑定拒绝、同一静态FS未初始化绑定失败后init再绑定成功、
重复绑定原对象或另一已初始化对象均拒绝并保持原内容；7+11、尾页剩2字节请求256只复制2、EOF/MAX/零length不访问目标均过。
逐调用零PMM及映射/用户路径保持，提供者与底层字节长期静态存储，无生产reset接口。make test无诊断，B19七组及原suite保持。

隔离真实U-mode边界探针同样通过：两份测试WORKER各16次、总32次ecall14，返回序列/内容均准确。
覆盖原第6节的about7+11、两个ELF magic、短读尾哨兵、仅页尾最后2字节实际拷出、EOF/MAX/零长度、
未知零长、路径0/128、读取257、不可读路径、只读目标及跨页失败；后者实际观察到合法前缀被写，返回-1且无kernel fault。
每次核对零PMM、available/satp/运行Task/FS记录及全部字节hash保持，dispatcher返回同frame且sepc+4。

证据`.cache/batch20-final-20261114/{host,user-read}/results.json`及各自log/runner；统一summary.json明确标为部分通过。
两个验证阶段87个正式源/配置各自在运行期间hash保持。边界探针显式替换WORKER负载，未修文件服务/FS/dispatcher/usercopy产品逻辑；
不将它当作正式WORKER正常演示通过，QEMU由runner在25秒外部SIGTERM结束。

当前正式worker的expected仍为Tinyos而实际TinyOS，已通知作者修一处大小写。
正常35页资源链及正式四次7/11读取专项准备中，修正后才最终运行；宿主/边界服务源码未变时无需重复这些已通过验证。
因此本批尚未全部P0、尚未三题收尾；不追加测试矩阵，无产品代改/提交推送。

## 10. 作者主动回答的三题与集中澄清（2026-11-14）

原计划在全部P0后讨论三题；作者在正常演示待修期间主动作答，现归档，不以时间顺序要求重新回答。
理解归档完成不替代尚未运行的正式正常资源专项。

### 原答（保留原文）

第一题：

> 用户哪里没有span，指定了复制内容长度

第二题：

> 校验2字节，写2字节，因为

```cpp
size_t remaining = bytes.size() - offset;
size_t count =
    (remaining > destination.size()) ? destination.size() : remaining;
for (size_t i = 0; i < count; ++i) {
    destination[i] = bytes[offset + i];
}
```

> 文件状态不变，目标buffer字符被截断

第三题：

> Process不需要标记文件内容定位位置，open&read需要保证文件存在，文件可读

### 集中审查与修正版（协作者整理，不冒充作者重答）

1. 指定有效路径长度的理解正确。span只保存指针和长度，不做用户页表翻译/权限检查；不能直接使用用户path地址与用户侧有没有span无关。
   先user_copy_from验证并复制到内核，再用path_length构造span查找，所以无需补NUL，也不扫描越过本次长度。
2. 实际只剩2字节就读2、user_copy_to也只校验/写2，方向正确。需要区分“短读”与“截断缓冲区”：
   正常短读只覆盖实际字节，其余原样保留，不清零/补NUL；跨页拷出失败可能已经覆盖合法前缀，后续未写部分保持，返回-1。
   文件字节及状态不变，本接口没有共享读取游标。
3. 当前每次请求显式携带offset，所以Process不必保存位置。“文件存在/可读”是open/read的必要检查，此外还需要保存跨调用的打开状态。
   概念上Process拥有fd到打开文件对象的对应关系；打开文件对象保存目标文件引用、当前位置及打开模式等。
   后续read(fd,...)从保存的位置开始，成功读取后按实际字节数推进；同一进程也可能两次打开同一文件并拥有两个位置，因此不能只在Process放一个offset。
   独立打开与fork继承是否共享打开对象属于未来批次的显式设计，不在B20追加实现作业。

三题已集中解释，不追加重答或新测试。本次仅维护理解归档；当前复查worker预期已由Tinyos改为TinyOs，但末尾s仍须大写为TinyOS，
仍未代改产品或执行正式正常QEMU。原宿主/边界证据保持，最后正常P0待作者完成这处修正后运行。

## 11. 最新正常验证与整批收尾（2026-11-14）

作者已将worker预期改成正确的`TinyOS builtin fs\n`，最新实读字节为O/S均大写。
前两次读取的Tinyos/TinyOs为各自检查时的文件内容；这次保存已落盘，未据此推断同步、网络或编辑器故障。
正式增量make -j4通过、nm -u为空，WORKER RX1465/RW4152仍在原装载布局内。

最终正常QEMU基于最新产品快照，28项检查全过：

| 范围 | 实际结果 |
| --- | --- |
| 新READ_FILE正常路径 | 两份正式WORKER共4次syscall、4次C桥接、4次实际copy_to；每份返回7/11，用户收到完整18字节about，成功日志两次 |
| 读取状态与资源 | 四次读取均零PMM分配/释放；Task/Process/context/frame/root身份保持 |
| 原启动FS来源 | 真实ELF4/ELF4/about18读取、零PMM、实际registry输入等于lookup spans，成功日志保持 |
| 原进程生命周期 | 两WORKER退出23/37、父退出0；EXEC保留身份/内核栈并设置fresh a0=17 |
| 原资源账本 | 五份空间各7页、35次合法free、运行期21次分配、净恢复14页、3次reap零PMM，最后至少两S ticks |
| 构建与快照 | 完整隔离构建无诊断/无未定义符号，87正式源配置在该次验证期间hash保持 |

统一证据`.cache/batch20-final-20261114/summary.json`；最终正常专项`normal/results.json`、`qemu.log`和`run_probe.py`。
宿主三组/原suite及32次真实U边界先前通过，之后产品仅worker预期文字修改；文件服务/FS/dispatcher/usercopy保持，故不重复无关验证。
正常探针没有修复产品副本逻辑；两个QEMU探针均在25秒由runner外部SIGTERM停止，非内核主动关机。
未新增OOM/孤儿/500轮矩阵，未改FD/Process生命周期；完整范围仍为第6节提前约定的P0。

最终执行路径为用户a0..a4→dispatcher取得当前用户root→复制路径→lookup→read_at到内核buffer→按实际count拷回用户→返回实际数量。
本接口没有打开文件状态；后续open/read/close需单独引入fd表和打开对象，具体下一批任务尚未交付。
三题已经第10节集中归档，不再重复下发。功能、既定P0与理解收尾均完成，本轮未提交或推送。
