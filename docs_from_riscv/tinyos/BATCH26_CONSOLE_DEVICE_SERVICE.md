# BATCH26：长期存活的控制台设备与服务

日期：2026-11-16。状态：功能、全部既定P0与原三题原答/集中澄清归档完成，本批收尾，见第10—12节。

## 1. 目标与边界

B25的InputQueue属于task_a的栈，适合当前kdbg；未来用户read需要一个在任务切换、退出后仍存在的输入对象。
本批将队列封装进ConsoleDevice，再由控制台服务长期拥有它，输入输出通过明确传入的回调连接UART。
原kdbg改走服务，命令与交互保持。
任务切换本身不会销毁栈上对象，当前A长期循环时队列也一直有效；迁移是为了不让公共设备寿命和访问入口依附某个任务的局部对象。

```text
boot_configure_console：传入UART接收/输出回调 → 一次配置
                                             ↓
console_service长期拥有ConsoleDevice → 内含InputQueue
          ↑                         ↓
task_a：poll → read_current → console.feed → commit
                              ↓
                         输出回调 → service.write → UART输出适配
```

这一批学习设备寿命和硬件适配边界。只引入一种设备，不建通用驱动框架。
暂不修改OpenFile/FD、Process、文件路径、用户ABI或用户程序；不声称/dev/console和stdin已经存在。
下一批再将设备接入FD读写；标准流与用户输入消费归属随后明确。
默认仍只有任务A/kdbg、trace0，原用户回归仍在test中，不恢复普通启动自测。

不做IRQ/PLIC、阻塞/唤醒、行规程、输入模式切换、磁盘或动态分配。
设备保存原始字节，kdbg负责ASCII行、编辑、解析和命令。

## 2. 文件范围与完整接口

作者新增四个文件：

```text
include/console/console_device.hpp
kernel/console/console_device.cpp
include/console/console_service.hpp
kernel/console/console_service.cpp
```

修改boot_private.h、boot_stages.cpp、demo_tasks.cpp和主Makefile。保留现有InputQueue算法。
协作者负责宿主测试与必要QEMU，不要求作者另写样板测试/测试启动工厂。

### console_device.hpp

独立include guard TINYOS_CONSOLE_CONSOLE_DEVICE_HPP，自包含，直接包含console/input_queue.hpp及接口需要的标准头。

```cpp
namespace tinyos::console {
struct ConsoleIo {
    void* context{};
    int (*try_read)(void*) noexcept{};
    void (*write)(void*, std::span<const unsigned char>) noexcept{};
};

class ConsoleDevice {
public:
    constexpr ConsoleDevice() noexcept = default;
    ConsoleDevice(const ConsoleDevice&) = delete;
    ConsoleDevice& operator=(const ConsoleDevice&) = delete;
    ConsoleDevice(ConsoleDevice&&) = delete;
    ConsoleDevice& operator=(ConsoleDevice&&) = delete;

    int configure(ConsoleIo io) noexcept;
    bool ready() const noexcept;
    long poll_input() noexcept;
    long read_current(std::span<unsigned char> destination) const noexcept;
    int commit_read(size_t count) noexcept;
    long write(std::span<const unsigned char> source) const noexcept;

private:
    InputQueue input_{};
    ConsoleIo io_{};
};
}
```

设备作为有身份的长期对象，后续可被设备打开对象借用，不允许无意复制或移动。
它不依赖debug/console、FS、Process或drivers/uart；UART回调适配在boot层。
context允许nullptr，回调自行解释；回调和其引用的上下文在整个服务使用期间有效。
write同步消费span，不保存临时buffer；try_read立即返回-1或0..255，不等待输入。
回调不得重入同一设备或触发任务调度，设备方法无异常。

### console_service.hpp

独立include guard TINYOS_CONSOLE_CONSOLE_SERVICE_HPP，包含console_device.hpp；仅声明窄服务接口：

```cpp
namespace tinyos::console {
int configure_console(ConsoleIo io) noexcept;
bool console_ready() noexcept;
long poll_console_input() noexcept;
long read_console_current(std::span<unsigned char> destination) noexcept;
int commit_console_read(size_t count) noexcept;
long write_console(std::span<const unsigned char> source) noexcept;
}
```

console_service.cpp匿名namespace内直接拥有一个constinit ConsoleDevice g_console_device{}。
六个函数分别转发给该对象。服务是本模块私有状态的拥有者，不返回设备/队列的可变指针、不暴露全局变量。
不需要另存g_device指针、第二份输入队列或第二份configured标志。

## 3. 设备全部操作的约定

| 操作 | 返回与状态 |
| --- | --- |
| ready | io_.try_read非空表示配置成功；新对象false |
| configure | 已ready或任一回调为空返回-1，原io/队列保持；预检全部通过后io_=io，返回0 |
| poll_input | 未配置-1；已满0且绝不调用try_read；否则只尝试一次，暂空-1的回调结果映射为0；收到字节入队返回1 |
| read_current | 未配置-1且不动目标；否则返回队列实际复制数量，空目标/当前暂空均0，不消费 |
| commit_read | 未配置-1；否则沿用InputQueue的0成功/-1超量且保持，提交0合法 |
| write | 未配置-1；长度超过__LONG_MAX__返回-1；已配置空span返回0且不调用回调；其余同步调用一次write，返回实际source.size() |

configure只设置回调，不清队列；重复配置无副作用，不提供reset或测试专用重绑。
以try_read指针作为完成标志是因为两个回调都先通过预检、再整体赋值；只存在“全未配”和“全已配”两个合法状态。
配置仅在调度启动前执行；运行期不允许并发configure，这不等于机器级原子发布机制。

poll_input的关键路径：

```text
未ready → -1
队列满 → 0
byte = io_.try_read(io_.context)
byte == -1 → 0
byte不在0..255 → PANIC_KERNEL（内部回调违反约定）
转换unsigned char，try_push；意外失败 → PANIC_KERNEL
成功 → 1
```

poll返回0既可能硬件暂空，也可能软件队列已满，不表示设备关闭或EOF。
内部read_current返回0仅表示本次没有复制字节，本批没有用户read的EOF/WOULD_BLOCK约定。
未来接用户FD时另区分“暂时无输入”与普通文件EOF，不提前加临时用户syscall或错误常量。

设备read_current委托队列，count最多256，返回long的转换安全；commit委托队列。
write在转换size_t到long前检查范围。回调是同步且无失败返回的当前UART输出约定，成功返回不宣称硬件已经发送到外部终端。
不加入OpenFile式offset：控制台消费的是输入流，位置已由队列状态表达。

## 4. UART适配与boot接线

在demo_tasks.cpp文件内保留私有UART适配：

```cpp
static int read_uart(void*) noexcept {
    return uart_try_getchar();
}

static void write_uart(void*, std::span<const unsigned char> bytes) noexcept {
    for (unsigned char byte : bytes) {
        uart_putchar(static_cast<char>(byte));
    }
}
```

UART调用只负责真实硬件动作，队列容量、提交、行解析分别留在自己的层。
没有字符串终止符要求，NUL和ff也按长度输出。

boot_private.h现有extern C区域内新增int boot_configure_console(void)声明。
demo_tasks.cpp包含该头并定义此函数，将ConsoleIo{nullptr, read_uart, write_uart}交给configure_console，直接返回结果。
静态函数回调寿命覆盖整个内核；不把boot函数的局部数组或局部对象地址传为context。

boot_stages.cpp的boot_start_user在configure_file_service成功之后、boot_create_tasks之前调用一次boot_configure_console，
非0时boot_fail("Console configuration failed.\n")。不在task_a循环里反复configure。
原boot_create_tasks及其测试替换工厂接口不变，不额外加入用户任务。

## 5. task_a与输出路径的修改

删掉task_a的栈上InputQueue，保留KernelConsole和32字节buffer。
KernelConsole输出回调仍为debug::ConsoleOutput需要的span<const char>，用一个薄适配转发给服务：

```cpp
const auto bytes_view = std::span<const unsigned char>{
    reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()};
```

调用tinyos::console::write_console(bytes_view)，检查返回值非负且等于bytes.size()；意外失败PANIC_KERNEL。
这只是按字节查看已有存储，回调同步消费，无需新分配或字符串复制。
该适配不能再直接uart_putchar，也不能调用回自己导致递归；真实UART输出只在write_uart。
task_b原演示输出以及其他内核日志不在本批迁移范围。

task_a每轮完整顺序：

1. 调用poll_console_input一次；负数为未配置等内部接线错误，panic；返回0仍继续读已有输入。
2. 调用read_console_current(buffer)；负数panic，0才delay并continue。
3. 将非负count转换为size_t，按实际数量feed；当前接口保证count不超过buffer大小。
4. feed全部完成后commit_console_read(actual_count)，非0panic。

不依据poll返回0就跳过读取：满队列恰恰需要消费才能继续接收。
不直接调用InputQueue或UART，不在任务内再定义一份全局/静态队列。
console.start必须在设备配置完成之后，第一次提示也通过服务输出。

主Makefile的OBJS新增kernel/console/console_device.o与kernel/console/console_service.o，保留input_queue.o。
纯C++接口用.hpp，不把新头包进extern C；boot函数C链接由现有boot_private.h管理。

## 6. 当前执行上下文与将来用户接入

从“任务A栈拥有队列”变为“服务拥有设备、设备拥有队列”；任务A仅调用窄接口。
拥有者寿命变长，没有增加并发消费者。当前只有任务A轮询、查看、提交和输出，timer/trap不访问设备。
因此本批不添加锁/volatile/atomics或在每个方法内关中断；依赖的是明确独占关系，而非“全局变量天然安全”。

将来用户syscall访问后，原独占前提将变化，必须再明确producer与consumer的串行化以及输入归属。
不能仅仅把服务函数加入syscall就让kdbg继续抢队首，也不能因为单hart就忽略被抢占的读改写。
本批服务接口不允许调用者跨另一消费者保存未提交的peek，不提供多消费者事务票据。
用户FD、标准0/1/2与消费模式在后续批次集中设计，不提前暴露可变全局对象来省事。
尤其若改为独立poll任务生产、用户syscall消费，生产者size_读改写可被timer打断；即使consumer的syscall保持SIE关闭也不足以保护被打断的producer，届时须设计窄临界区或等价串行保证。

## 7. 实现顺序与固定验收

作者实现顺序：

1. 建ConsoleIo/ConsoleDevice头，先写ready/configure，明确失败不改变状态。
2. 写poll_input与read/commit/write，保留InputQueue既有不变量。
3. 建service头/实现，私有constinit对象及六个转发接口。
4. 接UART适配/boot配置，再改task_a与debug输出适配，补主Make对象。
5. 整批集中审查；助手补宿主与QEMU验证，不把下列清单变成作者额外测试作业。

```sh
make -j4
make test
make qemu
```

限时观察可timeout 8s make qemu，交互退出Ctrl+A后X。无需clean。
协作者另运行一次make test-process保持原生命周期回归；不追加压力500或历史矩阵。

| 优先级 | 固定场景与预期 | 方法与学习点 |
| --- | --- | --- |
| P0 | RV/native新头自包含/重复包含、目标build/nm、boot/task C符号与原host通过 | 接口与构建 |
| P0 | 未配poll/read/commit/write返回-1且无回调/无目标修改；无效configure不发布，合法一次成功、重复拒绝且旧输入/回调保持 | 真实设备宿主测试；配置事务 |
| P0 | -1暂空，0/255二进制；每poll最多一次接收，满256不调用输入回调，先消费后可再次接收且FIFO回绕 | 有限输入假回调+真实设备；硬件消费与容量 |
| P0 | 重复peek保持，实际提交/超量拒绝及重试正确；输出空span不回调，非空一次同步输出含NULff | 宿主；复用队列与同步输出 |
| P0 | singleton服务配置一次并保持，默认kdbg五命令help CRLF/cat about/hex worker64/missing/help恢复精确 | 宿主服务接线+真实QEMU |
| P0 | 实际poll经UART回调入长期队列、peek/feed/commit、输出经设备回调；零运行期PMM/池保持/后续至少2 S ticks且安静 | QMP只读状态/隔离观察，不改产品算法或加串口刷屏 |
| P0 | make test-process旧退出0/23/37/0、35free21alloc净14/reap3/池0保持 | 独立旧进程工厂；不恢复普通启动自测 |
| P1 | 口头追踪：poll返回0但队列满，task_a后续为什么仍应read | 可只讨论，不要求另写程序 |
| P2 | IRQ、并发producer/consumer、用户stdin/标准FD、粘贴压力 | 后续专题，本批不实现/不作为完成条件 |

宿主直接创建独立ConsoleDevice，回调用固定存储收发且noexcept；断言留在回调返回后。
服务singleton只在专用用例绑定一次，不能为测试加重绑/reset/getter。输出LONG_MAX前的范围检查静态审查，不构造没有有效存储的超大span。
真实QEMU不承诺高速粘贴无损；源码前后核对，不为通过而替换产品设备/队列算法。

## 8. 功能与P0之后的原三题

1. 服务、ConsoleDevice、InputQueue和task_a分别拥有/借用什么？为什么将设备寿命与task_a分离，任务切换本身会不会使栈上对象失效？
2. poll_input返回0，为什么仍要read_current？返回0与普通文件EOF有什么区别？
3. 为什么传UART回调而不让设备直接包含UART驱动？debug输出的span在谁返回前必须用完，重复配置失败为什么不能清空队列？

完成后一次集中梳理并回答，不要求提前作答或补测试矩阵。

## 9. 当前状态与后续记录

当前仅核对源码、设计任务和接口；作者尚未实现本批设备/服务，不宣称新增功能通过。
独立只读设计审查确认窄批次边界、配置预检/同步回调/负数检查和未来producer临界区问题。
准备证据.cache/batch26-preparation-20261116/results.json：拟定头RV64/native自包含与重复包含、constinit构造、boot/task适配形状编译及C符号通过；只是声明/接线编译，不是设备算法运行验收。
本轮只写任务文档与索引、缓存准备文件，无产品代改、重复既有运行测试或提交推送。
实现修正、固定P0证据与三题原答/集中澄清都继续记录在本文件。

## 10. 作者实现与集中审查（2026-11-16）

作者完成四个新文件、两对象Make接线、boot声明/一次配置和任务A服务收发。
草稿中重复configure当poll、禁止空context、任务提前读UART/向无参poll传字符、直接接size_t和boot误调创建任务等问题均已修正。
当前集中审查没有必须修改的功能问题，协作者未代改产品算法。重复boot_private.h/include顺序/空白仅可选整理，不作为验收门槛。

当前完整执行路径：

1. boot在文件服务之后配置控制台，传入静态read_uart/write_uart回调与nullptr context，先预检再保存；随后才创建并启动任务。
2. 控制台服务的私有constinit ConsoleDevice拥有队列和回调，task_a只持有行控制台及局部32字节交付buffer。
3. 每轮poll服务转设备：满时不读UART，未满时通过read_uart尝试一次，字节入长期队列；返回0仍继续查看已有队首。
4. read服务转设备/队列，保持队首；任务先检查负数再转换count，feed全部实际字节后提交。
5. kdbg输出适配将char span转为只读字节span，服务转设备再经write_uart逐字节输出；回调同步消费，检查完整返回数量，无递归。

配置以已验证的接收指针为ready标记，不需要第二份初始化状态；失败不改原回调或队列。
现仍A唯一访问者，无新增并发生产/消费；尚未开放用户FD/标准流。后续改变执行者时，第6节串行化边界仍须重新设计。

## 11. 全部既定P0验证（2026-11-16）

统一.cache/batch26-final-20261116/summary.json，all_passed=true。
106正式源在审查/控制台/进程回归与最终当前树一致；产品设备、队列、驱动与任务实现没有为验收而替换或修补。

| 项目 | 实际结果与证据 |
| --- | --- |
| 构建/接口 | make -j4成功；两头RV64/native自含/重复、constinit/禁复制移动、两实现RV/native、任务与boot_stages RV、boot头C11无诊断；boot_configure_console/task_a/task_b为C符号，服务无动态初始化节/函数；review/results.json |
| 宿主 | make test/CTest通过，原115+新5=120个具体用例；未配置拒绝/零长度拒绝、配置失败保持/空ctx成功/重复保留旧provider与输入、256满时不调用硬件、恢复/回绕、peek提交/拒绝重试、二进制同步输出及singleton一次绑定转发全过；host.log |
| 默认真实QEMU | console/results.json全部36项通过；原help CRLF一次、cat about18字节、hex实际内嵌worker64、missing拒绝与help恢复保持 |
| 长期拥有与真实输入 | boot/service/device配置各一次，this等于实际私有静态g_console_device符号，输入队列确实属于此设备；回调地址等于真实read_uart/write_uart、context0；55字节经service/device/UART/queue/peek/feed/commit，peek保持且提交后空 |
| 真实输出 | 68次输出经过service/device/write_uart，442字节与实际uart_putchar和完整控制台终端记录数量/hash一致，无递归或绕过链路 |
| 安静与资源 | 运行期PMM分配/释放0，available在本次启动基线31634保持、文件池0/satp保持；最终继续空poll且新增至少2次S-mode timer，串口安静 |
| 独立旧进程回归 | make test-process全部27项通过，退出0/23/37/0；35次合法唯一free、21运行alloc、净恢复14、reap3零PMM、最终池0与后续两S ticks；.cache/process-regression/20261116-214208-3/results.json |

新增持久宿主test_console_device.cpp与CMake/test_main接线由协作者完成；固定收发buffer的noexcept回调不抛断言，返回后再检查；service context为长期静态对象，无重绑/reset/getter。
LONG_MAX转换前的检查按源码与目标编译确认，没有制造不具备有效存储的超大span。
真实QEMU用成员/服务链接观察、只读RAM和QMP命名管道，不向客体串口加探针日志；满足观察后由宿主停止，非客体主动关机。
当前可用页基线因本次镜像布局变化与B25不同，比较的是本次交互前后保持，不把静态镜像增长当作运行期泄漏。

未扩大IRQ/多消费者/用户stdin/粘贴压力或历史矩阵，不承诺终端物理发送完成。协作者只改测试及文档，无产品代改或提交推送。
功能与全部P0完成，接下来仅按第8节原三题集中理解收尾。

## 12. 原三题原答与集中澄清（2026-11-16）

### 作者原答（原样保留）

> service是整个的设备端的中转，拥有实例化的对象转发服务，Device拥有整个输入输出模型，InputQueue有自己的缓冲区负责校验存下外部的输入，任务A拥有程序实体，实际使用需要透过这个程序才能实现，切换任务本身并不使栈上对象失效，因为不同任务有自己的栈，互不干扰
> 返回0是说当前的队列满了，调用read_current能把当前的队列清除，这个0是说当前队列满了，而EOF是文件末尾
> 可以直接执行读和写操作,解耦控制和设备驱动，输出span必须在回调函数前用完，配置失败不应该破坏当前系统正处于合法运行状态

### 集中审查

- 服务持有设备实例并转发、设备封装收发、队列有自己的字节存储，以及切换任务不会让栈对象失效的方向正确。
  精确归属是service直接拥有静态ConsoleDevice，Device拥有InputQueue和ConsoleIo回调记录；
  InputQueue拥有固定数组及head/size，维护容量与消费顺序，不负责用户页权限校验或命令合法性。
  task_a的局部对象是KernelConsole及32字节临时buffer，它是当前服务调用者，不拥有公共设备。
  任务控制结构和栈由现有任务系统管理，不是task_a函数的局部设备资源。
- 保存/恢复不同任务的栈指针不会结束局部对象寿命；当前A长期循环时，原栈上队列也会一直有效。
  把公共设备移到长期服务，是解除设备寿命与某个任务局部对象的绑定，不是修复“每次切换都悬空”的问题。
- poll_input的0有两种正常来源：队列已满，或者调用接收回调后得到-1（硬件暂时没有字符）。
  前者已有待消费数据，后者也不能仅凭这次poll判断此前队列是否有数据，因此仍调用read_current。
  此0表示本轮没有新字节入队，不是永久EOF；普通文件read在正长度请求下返回0通常表示已到文件末尾。
- read_current只复制到临时buffer，不清除或消费队列。任务feed实际字节全部完成后，commit_read才推进head并减少size。
  即使消费后旧数组值仍在，有效区间也已改变，不要求把物理存储清零。
- 回调解耦设备逻辑和UART驱动正确。span必须在输出回调返回前同步消费/复制，不是调用回调之前就用完；
  回调执行期间span仍然有效，回调不得保存借用指针稍后使用。当前write_uart逐字节调用UART，即同步消费。
- 重复或无效配置失败保持原回调、队列和ready状态正确；不能为失败的替换尝试破坏合法运行状态。

### 协作者整理的修正版（不是作者重答）

1. service拥有长期设备，设备拥有队列和回调，队列拥有原始字节缓冲；任务A拥有行控制台与临时交付buffer，调用公共服务。
   任务切换不销毁栈上对象；公共设备改由服务拥有，使其生命周期独立于任务局部对象。
2. poll返回0可能满，也可能硬件暂空，只表示没有新字节入队；继续查看已有队列。
   read_current不消费，处理完成后commit才消费。设备暂空与普通文件EOF不是同一语义。
3. 回调隔离设备策略与UART硬件操作；输出span在回调返回前同步用完，不保存临时指针。
   配置失败保持旧设备合法状态及未消费输入。

本轮仅归档原答和一次集中澄清，不要求重答、不增加题目或测试。第11节全部P0保持，无产品改动、重复测试或提交推送；本批收尾。
