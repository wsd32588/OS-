# BATCH24：内核cat/hex文件查看工具

日期：2026-11-15—16。状态：功能、全部既定P0及原三题原答/集中澄清归档完成，本批收尾，见第13—14节。

## 1. 目标、边界与可观察结果

让现有S-mode任务A接收一行命令，显示内置文件。最小命令集：

```text
kdbg> help
help
cat PATH
hex PATH (first 64 bytes)
kdbg> cat /etc/about
TinyOS builtin fs
kdbg> hex /etc/about
00000000: 54 69 6e 79 4f 53 20 62 75 69 6c 74 69 6e 20 66
00000010: 73 0a
kdbg>
```

上例命令行本身由控制台回显，命令执行完成后再打印`kdbg> `。
查看的是当前BuiltinFileSystem登记的内核静态文本/嵌入ELF，不读取宿主文件。
这是内核调试工具，未来U-mode shell仍通过syscall执行用户操作。本批不添加用户ABI、文件FD、进程、磁盘、目录/ls、程序启动、引号/管道、历史/补全或UART中断/队列。
调度器和文件资源生命周期保持；不为安静终端而关闭定时抢占。2026-11-16作者明确要求普通启动与回归负载分离：默认仅控制台任务，过程日志关闭，原完整用户程序/启动自测在隔离测试运行，详见第12节。

## 2. 两个模块及数据流

```text
task_a：UART尝试接收 → console.feed(byte)
                          ↓
               有界行状态 → 命令分派 → lookup_file(path)
                                           ↓
                      file_view：read_at(局部offset) → 输出回调
                                                            ↓
                                                     task_a的UART writer
```

- console：只管一行输入、编辑、分词、命令分派、路径查找和提示。
- file_view：只接收const BuiltinFile&，分块读取并输出原字节或hex视图，不查用户任务/FD，也不反向读取UART。
- task_a：拥有一个栈上KernelConsole对象，提供UART输出回调，是唯一RX消费者。继续用原任务槽和定时抢占。

文件显示使用自己的局部offset，不用OpenFilePool，不改变任何用户OpenFile的offset或引用。
console查找后得到的FS记录依赖现有长期FS实例；传给lookup的路径span只借本行buffer，使用完不留到下一行。

## 3. 完整接口与文件布局

新增三个纯C++接口头和两个实现：

```text
include/debug/console_output.hpp
include/debug/file_view.hpp
include/debug/console.hpp
kernel/debug/file_view.cpp
kernel/debug/console.cpp
```

各头自包含、有独立include guard；纯C++接口不包extern C。

### console_output.hpp

```cpp
#include <span>

namespace tinyos::debug {
struct ConsoleOutput {
    void* context{};
    void (*write)(void*, std::span<const char>) noexcept{};
};
}
```

write必须非空；context允许为空，由回调解释。context和回调在控制台使用期间持续有效。
回调同步消费/复制span，不能保存其中指针供稍后使用；函数无失败返回，本批不设计异步输出。
这是显式传入的输出依赖，宿主测试可以收集字节，内核适配则逐字节UART输出。不能把任意span交给uart_puts，因为其中可能有NUL或没有终止符。

### file_view.hpp

包含console_output.hpp，前置声明tinyos::fs::BuiltinFile，声明：

```cpp
namespace tinyos::debug {
int cat_file(const fs::BuiltinFile& file, const ConsoleOutput& output) noexcept;
int hex_file(const fs::BuiltinFile& file, const ConsoleOutput& output) noexcept;
}
```

两函数返回0表示成功、-1表示读取失败；不负责打印错误提示，由console处理。空文件是成功且不输出数据。
output遵守上述内部调用前置条件，不为控制台输入增加一套重复的指针/文件页检查。

### console.hpp

包含console_output.hpp、stddef.h与span。类的最小形状：

```cpp
namespace tinyos::debug {
class KernelConsole {
public:
    static constexpr size_t kLineCapacity = 160;

    constexpr explicit KernelConsole(ConsoleOutput output) noexcept
        : output_{output} {}

    void start() noexcept;
    void feed(unsigned char byte) noexcept;

private:
    void finish_line() noexcept;
    void execute_line(std::span<const char> line) noexcept;
    void write_text(std::span<const char> bytes) const noexcept;

    ConsoleOutput output_{};
    char line_[kLineCapacity]{};
    size_t length_{};
    bool discard_line_{};
    bool skip_lf_{};
};
}
```

start仅打印第一个提示符，调用方在任务开始时调用一次；不需要新全局初始化配置。
feed接收一个已经由驱动取得的字节，不自己轮询UART。private助手可以按实现调整，不增加通用解析框架或新管理器。
160表示最多存160个有效输入字符；解析全用span长度，不要求line_额外留NUL位置，也不把输入当C字符串扫描。

## 4. 文件显示的完整行为

### cat_file

局部unsigned char buffer[64]、局部size_t offset从0开始。每次调用file.read_at(offset, buffer)：

- 负数：返回-1。
- 0：到达EOF，返回0。
- 正数：按实际count调用output.write，再将offset增加count，然后继续读取。

实际输出的关键形状：

```cpp
output.write(
    output.context,
    {reinterpret_cast<const char*>(buffer), static_cast<size_t>(count)});
```

只有count>0才到这里。借用buffer仅在回调返回前有效；返回后才能覆盖它读取下一块。
按read_at的既有契约，count受目标buffer和文件剩余长度限制；不再复制一套页归属、文件尺寸或FD权限检查。
不补NUL、不擅自加换行；无尾换行的文件后面提示符会紧接内容，这是原始cat语义。

### hex_file

只预览文件前64字节，每行最多16字节，局部offset从0开始。复用read_at，短行按实际count输出；空文件不输出行。

固定格式：8位小写十六进制偏移、冒号和空格、每字节两位小写hex、字节之间一个空格、无行尾空格，最后一个LF。
不足16字节的行不填充、不输出ASCII侧栏。四行后结束，不继续读取；帮助文本明确first 64 bytes，不另引入offset/count命令参数。
上面的18字节about示例是精确格式。64字节预览上限意味着行偏移只到0x30，本批无需泛化偏移显示宽度。
可用`0123456789abcdef`按高低4位转换；不引入printf、动态字符串或额外格式库。

## 5. 行输入、解析及错误规则

规则一次固定，避免实现过程中追加行为：

| 输入 | 行状态和输出 |
| --- | --- |
| 可打印ASCII 0x20..0x7e | 未丢弃且长度<160时存入并回显 |
| BS 0x08或DEL 0x7f | 正常行非空则减长度并输出三个字节`\b \b`；空行忽略 |
| LF或CR | 输出一个LF、提交/拒绝当前行、恢复空行状态并打印一次提示符 |
| 紧邻CR之后的一个LF | 完全吞掉，不二次提交或打印提示符 |
| CR之后的其他字节 | 取消skip_lf状态，再按该字节正常处理 |
| 第161个可打印字节 | 标记整行丢弃，不再存储或执行该行 |
| NUL、TAB、其他非法控制或高位字节 | 标记整行丢弃，不把非法字节回显进终端 |
| 丢弃状态下后续普通字节/BS | 保持丢弃，不能用退格把截断前缀恢复成可执行命令 |

丢弃状态持续到换行，在换行时只打印一次`error: line rejected\n`，清状态并继续下一行。
CRLF仍只产生一次拒绝/提示。清理前应确保execute_line已经同步使用完借用的路径；不要先清空buffer再解析span。

仅用ASCII空格分词，允许首尾和词间多个空格。TAB本批不作为分隔符。最多命令词+一个无空格路径，不支持引用或转义。

| 命令 | 行为 |
| --- | --- |
| 空行或全空格 | 不执行，重新提示 |
| help | 精确输出`help\ncat PATH\nhex PATH (first 64 bytes)\n` |
| cat PATH | lookup_file，找到后cat_file |
| hex PATH | lookup_file，找到后hex_file |
| 已知命令缺/多参数 | `error: invalid arguments\n` |
| 未知命令 | `error: unknown command\n` |
| lookup返回空（含非法/未知路径） | `error: file not found\n` |
| 文件显示返回-1 | `error: file read failed\n` |

先检查词数，再lookup；已有FS负责路径语义校验，不在console重抄路径规则。所有错误后仍打印提示并接受下一行。
命令文字按长度比较，不用未终止输入调用strcmp/strlen。内部固定提示字符串传span时用sizeof(literal_array)-1，避免把终止NUL输出进去。

## 6. task_a与构建接线

将kernel/boot/private/demo_tasks.c局部重命名为demo_tasks.cpp，保留原boot_private.h作为第一个接口头。
该头已为task_a/task_b声明C链接，随后同名定义继承该链接；不要把纯C++头整体包extern C。主Make的demo_tasks.o名字保持。

重命名前确认目标.cpp尚未存在；如果已开始写另一份，先合并内容，别覆盖。重命名并编辑后，仅清掉这个模块的旧生成产物：

```sh
rm -f kernel/boot/private/demo_tasks.o kernel/boot/private/demo_tasks.d
```

旧.d仍可能依赖已移走的.c；这里只移除两个可再生文件，不运行make clean。
保留task_b原逻辑供测试工厂使用，普通启动不再创建它。原delay的volatile计数器++在C++20下会触发弃用诊断，局部改为`i = i + 1UL`即可，仍是原忙等行为。

在此任务文件中提供小UART输出适配，第一参数可不命名：

```cpp
static void write_console(void*, std::span<const char> bytes) noexcept {
    for (char byte : bytes) {
        uart_putchar(byte);
    }
}
```

task_a栈上构造`KernelConsole console{{nullptr, write_console}};`，先console.start()，随后沿用B23循环：
收到非负字节交console.feed，暂空则delay。删除task_a原来的直接回显，否则输入会重复；回显现在由console根据编辑规则决定。

主Make OBJS新增：

```make
    kernel/debug/console.o \
    kernel/debug/file_view.o \
```

不要同时保留同名.c/.cpp供隐式规则选择。无需新增任务槽、全局console、FS配置或初始化页表。
宿主CMake/测试入口和必要脚本由助手接线，作者不用另写样板测试。

## 7. 实现顺序与最小验证命令

1. 建三个头，先写cat_file：这是已有read_at与输出依赖之间最直接的一条链。
2. 写hex_file，验证16字节行和短尾行。
3. 实现KernelConsole::feed/finish_line，再写execute_line；保持行状态和显示函数分工。
4. 接task_a的C++局部迁移与主Make，整批完成后集中审查。

```sh
make -j4
make test
make qemu
```

普通启动直接在kdbg提示后输入help、cat /etc/about、hex /bin/worker；原用户回归不再自动运行。
这版没有接收队列，测试按回显/提示握手逐字输入，不承诺大块粘贴无损。不要为测试丢字临时改变驱动或扩大到中断接收。

## 8. 固定验收与责任

P0由助手承担，原条件一次给齐，不交给作者追加测试代码：

1. 目标构建/无诊断无未定义、三个头native/RV64自包含重复包含通过；TaskA/B仍是C符号，当前宿主原回归通过。
2. 真实file_view宿主用例：65字节含NUL/ff内容cat完整输出（64+1分块），空文件成功无输出；hex18字节完整行+短行、65字节只显示前64及精确格式。
   使用真实BuiltinFile/read_at，输出回调同步复制，确认源字节/描述及PMM状态保持。
3. console宿主用例：help/cat/hex与空行、首尾多空格；已知命令参数错误、未知命令/文件；CR/LF/CRLF、BS/DEL；160合法容量及161超限整行拒绝；
   非法控制/NUL/ff/TAB整行拒绝，丢弃状态不能靠退格恢复，下一合法命令可执行。借用输出/路径均按长度，原全局FS只绑定一次，复用现about/empty测试提供者，不加生产reset钩子。
4. 真实QEMU使用正式task_a+console+file_view+FS/UART：按握手输入help、cat /etc/about、hex /bin/worker、cat /missing、help，验证实际输出及拒绝后恢复；
   hex四行64字节与实际内嵌worker ELF前缀一致。默认安静启动下PMM与空文件池保持，结束后task_a/定时抢占继续；使用隔离观察或QEMU独立调试日志核对，不污染终端。
   原旧14/新FD事件、worker23/37父0、35free/运行21alloc/净14与三次reap零PMM由make test-process的旧负载独立核对，不能把新普通启动套用旧页数。

新console会解释编辑/控制字节，B23“原始回显”证据属于上一批现场，不要求新命令台把非法字节直接回显。
只复验本批相关功能，不重新扩大B22生命周期/所有用户复制矩阵或历史五页脚本。
P1选做：更多文本长度或命令交错观察。P2选做：大量输入/UTF-8/补全/日志路由；不作为本批收尾条件。

## 9. 完成后梳理与原三题

梳理：UART字节→有界行状态→命令/路径span→只读文件记录→局部offset分块显示→同步输出回调→UART。
实现/P0之后再一次作答：

1. 路径span和输出span分别借用谁的内存？为什么不能先清行buffer，或让输出回调保存临时buffer指针？
2. 为什么超长行要一直丢弃到换行，退格也不能恢复？CRLF为什么只能提交一次？
3. cat分块读与hex只预览64字节有什么不同？为什么这两条命令不应改变用户FD的offset和引用？

仍可对照书中“终端驱动”的行编辑与设备层分离；本批只做有限命令输入，不要求复刻MINIX终端服务器或shell整章。

## 10. 交付前准备与限制

独立只读设计审查支持两个模块与局部C++任务方案，未扩通用parser/formatter/backend框架。
`.cache/batch24-preparation-20261115/results.json`：三个拟定头在RV64/native C++20 -Werror下自包含/重复包含通过，task_a/b接线形状实际编译并nm确认C符号，volatile循环写法无诊断；94个正式源hash保持。
只是接口/接线准备，未实现或运行console/file_view；没有代改正式源码、重命名任务、移除产物、接主Make/宿主目标或提交推送。

## 11. 文件显示阶段审查与下一实现点（2026-11-16）

实读作者当前file_view.cpp：cat按64字节及实际count输出/推进，hex已改为16字节读取、前64上限、8位偏移和双hex文本，无尾空格/带LF；两主体静态逻辑正确。
目标语法退出0，但file_view.hpp以class前置声明实际struct BuiltinFile，Clang产生mismatched-tags警告，应改struct；这是合法C++声明差异，不误判成当前RISC-V链接已经失败。
console_output.hpp的write声明应按原接口补noexcept和{}，保持输出回调契约与默认初始化。不是新增运行期null检查要求。
证据`.cache/batch24-file-view-review-20261116/{results.json,rv64.log}`，仅源码/语法检查、五相关源hash保持，无运行/P0通过声明或产品代改。

下一步先实现KernelConsole::write_text（同步转交输出回调）与start（输出一次提示），然后feed的行状态：
先处理skip_lf，再处理CR/LF提交，再按discard/BSDEL/合法ASCII/容量分支；丢弃状态下BS不能恢复。
finish_line负责换行/提交或报错/清状态/重提示，execute_line负责原第5节既定分派；保持原完整任务范围，不追加测试作业。

## 12. 作者调整：普通启动与回归测试分离（2026-11-16）

作者明确指出main/worker中的fork回归与timer输出妨碍输入，要求完全转到测试阶段；本节更新此前普通启动同时跑回归的约定。
本次授权修改负载迁移、启动编排与追踪打印，保留作者正在写的行编辑算法。

### 最终安排

- user/main.c、worker.c变为短用户应用示例，保留实际使用的可写消息以生成合法两个LOAD，不再带人为测试页/断言；默认不自动启动，仍注册/bin/demo和/bin/worker供查看。
- 原两完整用户负载按字节迁test/process_regression/main.c、worker.c，原boot FD模型函数按字节迁boot_file_checks.inc，ELF头/about检查也只在测试工厂运行。
- 基础boot保留长期FS/ELF registry配置及sched_init，调用私有boot_create_tasks(kernel_root)；生产boot_tasks.cpp仅创建A，不创建B/USER，现scheduler_start允许无USER且不需reaper。
- 隔离测试换入test/process_regression/boot_tasks.cpp，恢复2S2U/ID3收养者与完整旧负载；实际MM/Process/调度/syscall仍是当前产品。
- KERNEL_TRACE默认0，只控制timer/yield/sleep/正常exit/普通任务返回等过程打印；timer_set_next、sched_on_timer、syscall及退出操作始终执行，fault/cleanup/panic保留。
- build/kernel_config.h随0/1变化才更新，内核对象自动重编；独立测试副本的trace1不污染默认产物，不需make clean。

### 入口和验证

```sh
make qemu          # 安静控制台
make test          # 宿主测试
make test-process  # 隔离完整旧进程/FD回归
```

统一`.cache/quiet-boot-20261116/summary.json`全部通过，100当前产品源与最终回归快照一致：

1. 当前make/host104/nm无诊断，两个小ELF均有两个LOAD且boot注册成功；小示例未自动运行，不误记为执行验收。
2. 实际默认QEMU在输入前后安静，慢速逐字输入cat /etc/about仅回显输入，执行后得到18字节文本和提示；独立interrupt日志记录4次S timer，UART无过程/用户回归日志插入。
3. 最终进程回归27项通过，无构建警告：双worker/旧14/FD各两次，fork快照/隔离与退出0/23/37/0；五空间各7唯一PA，35free/运行21alloc/净14、reap3零PMM、文件调用零PMM、池0及两S ticks保持。
4. 现有压力脚本从剪切生产boot文本改为test/user_stress/boot_tasks.cpp工厂替换，2轮适配检查通过：8worker/77运行alloc/84free，含spawn/fork/exec/fault/满槽；没有跑500轮或扩历史矩阵。
5. trace1→0实际重编通过，重复0不重编；trace0仍调用timer与scheduler，非法值拒绝，默认产物已恢复0。

test-process不依赖.cache实现，每次默认新建证据目录，显式路径已有内容则拒绝覆盖。
最初作者新接线仅有class/struct及volatile++警告；助手按原接口局部修正这两处及输出回调noexcept/默认初始化后，最终构建无诊断，没有改行编辑/分派算法。

### 迁移完成时的行状态待修项（历史，后续结果见第13节）

1. console.cpp开头误贴的debug::BuiltinFile前置声明和全局write指针应删除，真正声明已在对应头修正。
2. if ('\n')恒真，且finish_line清掉skip_lf_；应先finish_line，再令skip_lf_ = (byte == '\r')，才能保留CRLF状态。
3. line_[length_++]前缺160容量检查，第161个字符会越界；超限应标记整行丢弃，不再写入。
4. 非法控制/高位字节目前直接忽略，应标discard_line_并丢弃到换行；拒绝提示按约定error: line rejected，而非仅line too long。

该阶段仅完成启动/测试分离及短命令安静验证。后续作者已修第2—4项，完整宿主/命令组验收见第13节；第1项是无功能影响的冗余声明清理建议，不新增完成门槛。未提交推送。

## 13. 功能与全部既定P0通过，待三题收尾（2026-11-16）

作者修正为换行仅一次finish_line、随后按CR设置skip_lf_；非法字节/超限整行discard、统一line rejected提示。
length_+1 >160的写法在length_始终0..160的当前不变式下正确；不因与建议的>=写法不同增加门槛。
未代修这些产品算法。console.cpp开头两条未使用声明仍可清理，但不影响当前功能、ABI或验收。

统一证据`.cache/batch24-final-20261116/summary.json`，100个产品源码/构建配置在全部阶段与最终当前版本一致。

| 范围 | 实际结果 |
| --- | --- |
| 目标/接口 | make/nm无诊断无未定义，三个头RV64/native自包含重复包含，task_a/task_b仍为C符号，默认trace0 |
| 宿主 | 原104加新6组，共110个具体用例/CTest1通过，无诊断 |
| file_view | 65字节含NUL/ff原样cat、回调64+1、空cat/hex成功；hex18精确短尾、65只前64；源记录/字节及PMM保持 |
| 行状态/分派 | help/cat/hex/空格/错误；CR/LF/CRLF及CR后非LF、BS/DEL；160/161容量；NUL/ff/TAB整行拒绝、丢弃中退格无效及下一行恢复，全部通过 |
| 实际默认QEMU | 20项通过；五命令help(CRLF)、cat about、hex worker、cat missing、help，精确输出且错误后恢复 |
| ELF内容来源 | hex四行64字节与kernel中实际嵌入的worker ELF以及构建worker.elf对应字节完全一致 |
| 命令资源状态 | PMM alloc/free均0、可用页31635保持、私有文件池0、satp保持；唯一运行任务A，无USER |
| 命令后存活/安静 | 继续155次空轮询，新增3次S timer；guest串口保持安静，独立QEMU interrupt日志共4次S中断 |
| 最新独立进程回归 | make test-process对应持久入口在当前产品再跑通过：原退出0/23/37/0、35free/运行21alloc/净14、reap3零PMM、文件调用零PMM和池0保持 |

host/存make test实际日志、新test_kernel_console.cpp已接CMake/入口，复用已有长期FS提供者，不添加生产reset/getter。
console/使用stdio字节管道、独立QMP命名管道读取观察RAM和独立interrupt日志；无socket权限需求，观察计数不污染UART。
file_view、console、driver和task实际实现保持原样，仅隔离副本添加只读观察器/PMM包装，未替换命令输出或修产品逻辑。
process-regression/使用已持久化的test负载与测试工厂，不把默认无USER的启动套用旧35页账本。
未扩突发输入/中断/队列/500轮/旧五页历史矩阵；QEMU为检查完成后宿主停止，不据timeout判断通过。没有提交推送。

### 功能梳理

1. TaskA持有栈上KernelConsole，轮询UART得到原始字节；输出回调显式借用writer/context，驱动不解析命令。
2. feed维护160字符行、CRLF和丢弃状态，正常输入回显，编辑修改本行。非法或溢出后整行不执行，换行清理并恢复提示。
3. finish_line把完整行借给execute_line；分词后的span借用同一个line_，命令/参数核对后lookup得到长期只读文件记录，执行完才复用输入缓冲。
4. cat按局部offset读完文件、按实际count同步输出；hex只读前64并格式化。回调返回后才复用临时输出数组，不保存借用指针。
5. 文件显示不用用户FD/打开池，因而不改变进程位置或引用；正常控制台安静，原用户回归通过独立测试入口继续覆盖。

原第9节三题原答、集中澄清及协作者整理修正版已在第14节归档，不要求重答或新增测试作业。

## 14. 原三题回答与集中澄清（2026-11-16）

### 作者原答（保留原文）

> line和栈上临时建立的缓冲区，会导致内容被篡改，容易出现引用悬空指针
>
> 会导致溢出，已经溢出之后，退格键无法被识别为操作，会增加转义字符添乱，不清楚
>
> cat需要输出全文，hex只是预览前64字节内容，防止破坏上下文，没人希望自己只是输出检查一下就会在下一次找不到到上次退出时候的位置

### 集中澄清

1. **借用来源理解正确，覆盖与悬空应分开。** 路径token借用KernelConsole的line_；输出span可能借用cat的buffer、hex的text或其他局部字符数组。
   line_尚存活时被清空/复用，会使token看到错误内容，但不一定已经悬空；局部输出数组生命周期结束后再访问其指针才是悬空。
   同一个输出buffer在下一轮读取时也会被覆盖，因此write必须在调用期间同步消费/复制，不能只保存span。
   lookup返回的BuiltinFile记录属于长期FS实例，与查询路径token的短期借用应区分；原文件字节依然由长期静态存储提供。
2. **丢弃政策保护完整命令意图，不是在发生越界后补救。** 当前在写入第161个字符之前就标记discard，不继续存，因此没有先写越界。
   若仅截断后执行，可能把不完整输入或带非法后缀的有效前缀当作命令。整行作废到换行才恢复，可避免这种误执行。
   退格控制字节仍然可识别，只是discard分支故意忽略它；并非退格自动变成了额外转义字符，也不是硬件失去识别能力。
   CR与LF是两个字节。收到CR先提交一次并设置skip_lf_；紧邻的LF仅清标记并返回，不再次提交空行/多打印提示符。
   若CR后是其他字节，则清标记后正常处理它，不能无条件吞掉下一字节。
3. **全文/预览区别正确，保护对象应明确到打开文件状态。** cat按64字节块反复读取到EOF，hex最多显示前64字节。
   本工具给BuiltinFile::read_at传自己的局部offset，不使用用户FD/OpenFile，因此不会推进已有用户打开对象的offset，也不增减其引用。
   这里保持的是文件读取状态，不需要笼统解释成保存CPU寄存器上下文；每次查看可以从头开始，而用户之前的读位置仍保持。

### 协作者整理的修正版（非作者重答）

1. token借用line_，输出span借用当前临时数组；同步使用完成后才能覆盖/复用，局部数组返回后不能再保留其指针。
2. 容量检查在写入前拒绝；整行discard防止截断命令被执行，退格在该状态下按策略忽略。CRLF以CR提交、吞掉紧邻LF实现一次换行。
3. cat分块读完全文，hex只预览前64；局部read_at偏移与用户OpenFile状态分离，查看不改变用户位置或引用。

原答与理解缺口已一次集中说明，不追加问题或要求重答。功能及全部既定P0结果保持，本批收尾；本轮仅更新文档/索引，无产品修改、重复测试、提交或推送。
