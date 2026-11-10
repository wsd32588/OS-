# TinyOS：C++ 标准库基础支持

更新：2026-11-04。最小静态对象运行时已完成实际构建、宿主回归、正常QEMU与受控析构QEMU验证；BATCH09已收尾，BATCH10进行中。尚未接入现成目标标准库或完整C++运行时。

下文2026-10-27的工具链/五头任务与验收均保留历史时点；之后地址、页表和装载器已实际使用C++。2026-10-31工作区另有array/limits/其他兼容头和panic修改，未因本次运行时准备而重新审查或验收，不能用旧五头结论覆盖最新草稿。

本文件单独记录标准库选型、工具链接线、可用组件、运行时边界和实际验收结果。
地址/PTE/页表的功能改造继续记录在 [TINYOS_EVOLUTION.md](TINYOS_EVOLUTION.md)，不在两处重复维护。

2026-11-04作者要求先试O3再精简。runtime头现为include/runtime/cpp_runtime.hpp，注册容量留在实现；已删除_is_ready恒假条件与finalize零次循环前的重复早退，保留防重复初始化及ABI规则。目标O3构建、正式双用户回归与O3受控构造/析构探针均通过；默认仍O0。完整改动、先后顺序及证据统一记录在[BATCH10前置整理](tinyos/BATCH10_USER_EXIT_RESULT.md#作者要求的前置整理o3头文件边界与重复检查2026-11-04)，下文带旧日期的接口/状态保留历史语境。

## 当前插入试验：静态对象生命周期（2026-10-31）

### 实际复验与解释入口（2026-11-01）

此前协作者只是读代码、解释方案，没有检测作者最后加入的一次性保护，也没有执行析构试验；不能把作者“没问题”当作实测结论。作者追问后，本次已实际执行以下复验，未改正式产品/测试源码：

- `make -B -j4`返回0，无警告，最终ELF无未解析符号；runtime和kernel_main的C符号正确，init表处在rodata映射及kernel_end之前。
- 原 `make test CTEST='ctest --verbose'`返回0，75个具体运行组+1汇总、CTest 1/1通过，无警告；runtime未混入宿主CRT。
- 正式 `timeout 8s make qemu`出现D/count=1、Hello TinyOS、Time OK、Hello from user、Data OK和exit，退出后两次S timer，无fail/fault/release错误；124为限时观察。
- 一个受控临时副本保留正式runtime/头/linker/Makefile与entry初始化，临时替换boot/main.cpp为生命周期探针，只在entry最终finalize后加只读trace检查。真实RISC-V对象与QEMU执行，不用宿主C++运行时替代。

| 受控检查 | 实测结果 |
| --- | --- |
| 同TU两个不同全局对象 | 构造trace AB、对象外计数2；init_array只有8字节/一个入口，表项数不等于对象数 |
| 启动参数 | 构造器主动破坏a0/a1后，main仍收到hart=0及有效DTB魔数d00dfeed，entry保存恢复正确 |
| 重复初始化 | 再调init返回-1，trace仍AB、计数仍2，不重构任何对象 |
| 普通局部对象 | 进入作用域记录L，离开记录l，trace正常追加Ll |
| 无效注册 | NULL回调返回-1且trace不变 |
| 混合DSO登记 | x属于A、y属于B、z属于A；finalize(A)执行zx，重复无变化，finalize(B)只执行y |
| 真正全局析构 | finalize(NULL)执行编译器自动登记的ba；再次调用无变化 |
| entry终止入口 | 临时main受控返回，entry再次调用finalize(NULL)，trace仍ABLlzxyba，无重复析构 |

对象反汇编确认自动__cxa_atexit/__dso_handle注册；临时ELF无未解析，构建无警告。59个正式产品源/头/配置哈希保持。短启动探针不启动PMM/分页/调度，用于单独验证对象生命周期；正常系统行为由前面的正式QEMU覆盖。短启动QEMU由脚本3秒后SIGTERM停止，返回0不是客体关机。

证据在仓库根 `.cache/cpp-runtime-verify-2026-11-01/`：build/host/qemu-normal日志及结果JSON、ELF符号/节/布局检查；`qemu/probe_main.cpp`是可读探针，`qemu/run_probe.py`可重跑，`qemu/results.json`和qemu.log保存实际逐项结果。正式源码和暂存区保持核对写入final-results.json。

当前限制明确保留：注册表64槽按累计成功登记计，执行后仅清function，不复用槽位；不是同时存活对象数上限。没有验证满表、终止期间新登记、完整重入/并发、局部static保护、TLS/异常/堆或跨翻译单元依赖。本轮非NULL DSO筛选已满足原P0的一条筛选/拒绝验证，不扩故障矩阵。旧_is_ready冗余状态已于2026-11-04删除，实际防重复仍由s_init_started完成。手工调用无效注册必须处理返回值，正常编译器不会生成NULL析构回调。

正式main中的Test仍是平凡析构，D/count输出本身只说明全局构造；析构支持结论来自上述带非平凡析构的受控目标试验。不为了触发析构去结束正常内核或在用户exit销毁全局服务。cpp_runtime_fini这一自定义中转已按作者选择删除，受控返回后由entry直接调用__cxa_finalize(NULL)；先前提到该中转的任务文字保留为当时方案，不再是必须实现的接口。

### 机制梳理

编译器生成的启动入口构造对象后，调用__cxa_atexit登记三元组function/object/dso。function是可调用的析构入口，object是要传给它的对象地址，dso是所属映像/模块的不透明标识，不是类名，也不是对象值。当前内核没有动态加载共享库，编译器将__dso_handle符号地址作为同一映像的分组标识；它的数据初值为nullptr不等于传入的标识为NULL。

逆序按实际登记顺序，而非按类名、对象内容或init表项数。示例先登记x/A、y/B、z/A，finalize(A)从最后向前：调用z，跳过y，调用x；不能遇到B就break。finalize(NULL)选所有尚未执行项。对每个命中项，先复制记录并清空原function，再调用复制出来的function(object)；后续遍历遇NULL便跳过，所以重复调用不会再次析构，同一次调用中的简单递归也不会重新调用当前项。它不承诺完整处理析构中新增登记或并发协议。

析构表示结束对象生命周期、执行它自己的清理逻辑，不自动等同于归还对象所在物理页。静态对象存储仍属于内核映像；若析构函数管理其他资源，具体归还由该函数实现。当前两个测试对象只记录trace，正常系统main不返回，内核全局对象继续存活。

### 作者首次构造输出（历史，同日）

作者将boot/main改为.cpp，给kernel_main补C链接，接入runtime对象与entry初始化调用，并报告 `D0x0000000000000001` 后正常出现 `Hello TinyOS!`。静态源码核对中Test对象已在函数外，构造设置value='D'、成员volatile count递增；该输出表明全局构造执行后进入了kernel_main。先前“数字1不显示”是把数值强转char后发出控制字符，现改用uart_put_hex观察。彼时未独立重跑；成员count每次构造归零，不能单独证明只执行一次，之后的实际复验已使用对象外计数并检查二次init拒绝。

作者决定先亲手试构造函数、析构函数与init_array，再考虑BATCH08。这里提供完整边界与观察目标，不代写产品代码。C++已真实用于PMM、页表、loader等；本次补的是其中尚缺的静态对象启动/终止支持，不是首次让C++进入内核。

### 先分清三种路径

| 路径 | 谁触发 | 本次需要补什么 |
| --- | --- | --- |
| 普通局部对象构造/正常作用域析构 | 编译器生成直接调用 | 已可用，保留一个小例子观察即可 |
| 命名空间作用域对象/类静态数据成员的动态初始化 | 编译器生成启动入口，启动代码调用 | 显式收集并遍历init_array |
| 静态存储对象的非平凡析构 | 构造成功后的注册与受控终止 | 当前目标的__cxa_atexit / __dso_handle / __cxa_finalize路径 |

constinit省去对象的动态初始化，但非平凡析构仍可能需要init_array入口执行析构注册，不能据constinit断言无需启动表。函数内static也有静态存储期，但通常在首次经过声明时动态初始化，不由启动表统一构造，本轮不验收该路径。局部非static对象的正常析构、全局终止析构、异常展开、被强制结束任务的栈清理不是同一个机制；本次不启用异常、线程安全局部static或任务栈展开。

### 真实目标对象探针

已按当前Clang/RV64/lp64/freestanding和禁异常/RTTI/线程安全static参数，在隔离目录编译一个全局对象、普通局部对象和局部static的例子：

- 默认及显式-fuse-cxa-atexit均生成.init_array，并引用__cxa_atexit和__dso_handle；未生成全局析构函数的.fini_array列表。
- -fno-use-cxa-atexit改为引用atexit，仍不是消除运行时依赖的开关。
- 当前生成的注册调用没有检查__cxa_atexit返回值。不能只在表满时返回失败、随后让启动端继续假装全部析构已登记。
- 另一个constinit+非平凡析构探针仍生成.init_array和__cxa_atexit，确认“常量初始化对象仍可能需要启动时登记析构”。对应constinit-probe.cpp及日志同目录。

证据 `.cache/cpp-runtime-preparation-2026-10-31/`（仓库根）：probe.cpp、三种对象/节/未解析符号/反汇编及results.json。这里只是目标对象检查，没有接入正式内核、运行构造器或验证启动。

### 作者实现的最小范围

建议新增 `kernel/runtime/cpp_runtime.cpp` 与 `include/runtime/cpp_runtime.h`；公开C入口 `int cpp_runtime_init(void)` 和 `void cpp_runtime_fini(void)`，供entry/受控试验调用。也可沿用作者自定的同职责文件，不搬迁已有模块。init成功0、失败非0；初始化只允许一次，重复调用拒绝且不重复构造。

本次可以分两段写，完成后集中审查：

1. **启动构造**：在linker.ld显式收集并KEEP(.init_array和.init_array.*)，以8字节对齐的起止符号描述函数指针区间。需要优先级子节时用SORT_BY_INIT_PRIORITY收集子节，再收集普通项；不把跨翻译单元的未约定顺序作为模块依赖。启动端按表正序调用每个入口一次，不能按“每个入口等于一个对象”计数，一个编译单元入口可能构造多个对象。
2. **静态析构注册/执行**：保持当前默认ABI，提供唯一C链接数据符号__dso_handle，以及实际有作用的__cxa_atexit和__cxa_finalize。以固定容量记录回调/对象/DSO标识，无堆；有效注册成功0，失败非0，禁止空桩。finalize按实际注册逆序执行；参数NULL选全部，非NULL只选匹配DSO；执行前标记已处理，重复finalize不得重复析构。cpp_runtime_fini调用__cxa_finalize(NULL)，终止后不再使用已销毁对象。

ABI签名供核对，不是实现：

```cpp
extern "C" int __cxa_atexit(void (*function)(void*), void* object, void* dso);
extern "C" void __cxa_finalize(void* dso);
```

注册表和运行时自己的状态必须能静态/常量初始化，避免“运行构造表需要先构造注册表”的循环。固定容量本次可取32，边界明确返回失败；编译器不会自动传播注册失败，启动端须能发现失败并停止进入kernel_main，或者在注册失败处明确诊断并停机。不能静默漏掉全局析构。此试验限单hart、受控无并发注册/终止，不引入锁、TLS、动态库加载或完整libc。

### 启动接线中最容易出错的部分

- 当前entry仅设置sp后call kernel_main，没有显式清BSS。linker.ld的BSS包含16KiB boot stack。若要补BSS清零，必须在任何call/栈存储前用无栈汇编完成，再使用boot stack；不能在该栈上的C函数中清整段BSS，把自己的返回地址清掉。当前__bss_start也没有保证8字节对齐，不能直接假设sd步进合法，尾部须正确处理。
- OpenSBI传来的a0/a1分别是hart ID和DTB地址。新增init调用会改调用者保存寄存器，必须跨调用可靠保存，再恢复传给kernel_main；清BSS的临时寄存器也不能破坏这两个参数。栈保存发生在BSS清零之后，并保持16字节栈对齐。
- 推荐关系是：无栈清BSS → 建立栈 → 保存启动参数 → cpp_runtime_init → 检查成功并恢复参数 → kernel_main。调用失败走明确停机分支；不要重复清BSS抹掉构造结果。
- 数组节须放在现有内核映射和__kernel_end之前，例如rodata区域内、__rodata_end计算之前；不能让孤立节落到PMM可分配范围或分页后不可读的位置。构造器入口代码也须位于正常text映射内。
- kernel_main之前PMM、分页、trap、调度和显式服务初始化尚未完成。首个试验构造器只写静态标记/trace，启动后再观察；不把PMM初始化偷偷搬进全局构造器。跨模块的资源初始化继续使用原boot_stages。
- 全局析构的受控终止与用户任务exit不同。正式kernel_main启动调度后不返回，不能在用户exit或任意panic时销毁内核全局服务；在临时短启动副本中让试验main受控返回，再运行fini并停机，观察逆序/不重复。正式路径需要保留清楚的终止入口，但不为验收改成自动关机。

本轮不要求通过.fini_array实现全局C++析构，因为当前编译器走注册机制；若作者另试.fini_array，它是另一张终止函数表，必须明确其内容与调用顺序，避免与注册析构重复执行。preinit、旧.ctors/.dtors兼容和完整C运行时不作为本次门槛。现有用户ELF仍禁止init/fini数组，不据此扩成用户C++运行时。

### 可观察结果和分工

作者只写小对象及核心接线，协作者负责必要P0，不追加大矩阵：

| 等级 | 观察目标 | 负责 |
| --- | --- | --- |
| P0 | 一个确实生成init_array的全局对象，在kernel_main入口前写出预期标记；普通局部对象离开作用域调用析构；启动hart/DTB参数保持 | 作者写小例子，协作者检查目标ELF/正常QEMU |
| P0 | 两个同翻译单元静态对象在受控终止时按注册逆序析构各一次；再次finalize不重复；用一条非NULL DSO筛选或满表失败验证实际注册语义 | 协作者临时短启动副本，不要求作者写新框架 |
| P0 | 完整构建无警告，数组位于映射/PMM边界内，无未解决__cxa/atexit依赖；原75组宿主及正常Time OK/Hello/Data OK/exit保留 | 协作者例行复验，运行时自身不混入宿主crt制造重复__dso_handle |
| P1/P2 | 更多初始化优先级、局部static、终止时新注册/重入及其他运行时扩展 | 按需求再定，不阻塞本次试验 |

保留Makefile现有Clang目标规则，仅接入新实现/试验对象，不改全仓语言；现有panic和兼容头改动保留但不据此宣称其全部已复验。不clean/stage/commit/push，不提前迁移B08调度或地址空间代码。

参考：[C++ ABI的DSO终止接口](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#dso-dtor-runtime-api)、[GNU ld输入节保留](https://sourceware.org/binutils/docs/ld/Input-Section-Keep.html)、[输入节排序](https://sourceware.org/binutils/docs/ld/Input-Section-Wildcards.html)。实际符号与本机目标对象探针共同判断，构造表和析构注册分开验收。

### 首次草稿集中审查（历史，2026-11-01，彼时尚未接通）

作者已新增runtime头/实现和init_array输出节。构造表的正序遍历方向正确；协作者只读检查并在隔离目录编译/链接，没有代改产品：

- linker定义init_array_start/end，而实现引用__init_array_start/end，名称须统一。实际ld还把 `init_array_end=` 当成输入文件名，报cannot find init_array_end=；赋值写成 `__init_array_end = .;`。通配建议明确分成 `.init_array.*` 与 `.init_array`；第二条KEEP当前漏前导点，但前面的 `.init_array*` 已覆盖普通项，不能误报成完全未收集。
- __cxa_finalize的声明/定义误为int，应为ABI约定的void。项目自己的cpp_runtime_fini返回int且头/定义一致，可保留，不能与ABI入口混为一谈。
- __cxa_atexit和__cxa_finalize目前只是返回0的空桩，没有实际登记、按DSO筛选、逆序调用、执行前标记、不重复及失败处理，不能算析构已生效。
- cpp_runtime_fini引用__fini_array_start/end，但链接器没有这些符号。本次默认Clang走析构注册，最小路径应由fini调用真正的__cxa_finalize(NULL)，不必为这些引用补另一套表。init还须一次性状态保护。
- Makefile尚未包含runtime对象，entry仍直接调用kernel_main；启动BSS/栈/a0-a1保存、init调用与失败停机尚待作者接线。

__dso_handle的当前nullptr初值本身不是上述阻塞：探针中编译器传的是该唯一符号的地址，不是读取其值后传NULL。保持实际注册/筛选使用同一不透明标识。

当前目标单文件编译成功但有4处空桩未使用参数警告，隔离链接按上述脚本错误失败；未做整仓构建/宿主/QEMU，不把未加入构建图的普通make当运行时验收。证据 `.cache/cpp-runtime-review-2026-11-01/`（仓库根）含对象/诊断/结果和5个正式文件前后哈希，独立只读审查一致。

## 为什么先补这一层

当前地址类型已经需要类型判断、比较和可失败的返回值。优先采用现成标准库的
freestanding 子集，减少重复实现基础工具的工作；目前不自行建设一套 STL。

标准库可以改善接口表达。地址范围、VPN/PPN 位宽、符号扩展和资源归属仍由内核代码保证。
接入库不会自动修复现有 address.hpp 的复合赋值、溢出或转换错误。

## 已核实的环境

| 项目 | 当前事实 |
| --- | --- |
| C/汇编编译 | riscv64-linux-gnu-gcc 15.2.0 |
| 内核 C++ 编译 | Clang 21.1.8，--target=riscv64-unknown-elf，C++20 |
| ISA / ABI | rv64imac_zicsr_zifencei / lp64 |
| 其他目标约定 | medany、禁用 relax、关闭异常与 RTTI，使用原 RISC-V ld 链接 |
| 当前成功模块 | user_image.cpp、pmm.cpp 已进入真实内核并通过各自验证 |
| 目标标准库头 | concepts、compare、cstddef、cstdint 当前不可用；stddef.h、stdint.h 可用 |
| 头搜索路径 | 当前只有 Clang resource 基础头；预期的目标 clang-runtimes 目录不存在 |
| 宿主环境 | 本机 C++ 标准库可用，不能用宿主成功替代目标验证 |
| 可安装候选 | 本机 APT 索引有 RISC-V GNU C++ 开发包和 libc++ 开发包；尚未安装或认定适合当前裸机 ABI |

曾直接测试目标包含 concepts/cstddef/cstdint，均报找不到头；默认比较与 PageNum::from
实际实例化还暴露 compare/assert 依赖。地址草稿尚未接入页表，原 make 和宿主测试通过不代表新头通过。
相关探针记录见 `../.cache/address-review-frn_tl28/`；concepts 头可用性另由同日独立命令复核。

## 首轮组件清单

以下是待验证目标，不是已经可用的承诺。记录到具体组件及所用操作，不笼统宣布整套 STL 已支持。

| 组件 | 主要用途 | 使用边界 |
| --- | --- | --- |
| cstddef、cstdint、limits | 基础类型和数值边界 | 与目标位宽、ABI 一致 |
| type_traits、concepts | 类型约束和标签区分 | 区分语言 concept/requires 与标准库 std::same_as |
| compare | 地址比较类别、默认三路比较 | 验证真正实例化后的目标代码 |
| utility | move、forward、pair 等值工具 | 采用的具体操作不依赖尚未提供的运行时 |
| optional | 对齐、转换和偏移失败 | 检查有值后访问；空值访问的终止/异常路径不默认为已经支持 |
| array、span、string_view | 固定数组和非拥有视图 | 检查所用操作；视图不延长底层存储生命周期 |

vector、string、动态关联容器等，留待内核堆及分配失败策略确定后接入。
线程、文件、流和本地化等系统服务不属于本轮目标。

## 构造与析构：当前支持边界

2026-10-27只读核对启动汇编、链接脚本与当前内核符号：尚未接入通用C++启动/退出运行时。

| 对象或路径 | 当前情况 |
| --- | --- |
| 普通局部对象 | 编译器可生成构造和正常离开作用域时的析构调用；不需要全局构造表遍历 |
| 静态初始化、平凡析构对象 | 当前PMM实例采用constinit，已验证无动态初始化/析构运行时依赖 |
| 动态初始化的全局对象 | 启动入口只建立sp后调用kernel_main；没有遍历构造表，当前不可依赖 |
| 静态存储对象的非平凡析构 | 没有__cxa_atexit/__dso_handle等注册支持，也没有内核退出时的统一析构路径 |
| 函数内动态static | 已关闭线程安全静态初始化保护；这不等于完整支持其析构注册和并发初始化 |

linker.ld没有显式收集preinit/init/fini数组，当前kernel.elf也没有观察到这些节和相关运行时符号。
没有出现未解析符号，只能说明当前已用路径未产生该依赖，不能据此宣布全局对象生命周期已支持。
constinit不禁止非平凡析构；将来给静态对象增加析构函数后，仍需重新检查注册依赖。

首轮值工具优先用于普通局部对象，不必为了optional、array等先实现完整全局生命周期。
若确实需要动态全局对象，再明确构造表保留/调用、初始化时机、析构注册和退出策略；
不能用空的__cxa_atexit桩假装析构已生效。正常作用域析构与异常栈展开、任务强制终止是不同路径，
本项目关闭异常，不能假定后两者自动执行局部析构。具体运行时接入结果继续记在本节。

## 作者新增的五个兼容头

2026-10-27作者已修正cstdint文件名，并将 `-Ifreestanding` 加入目标CXXFLAGS。
当前目录包含cstddef、cstdint、type_traits、utility、span，均为手写、无扩展名的头文件，
不是编译产生的库文件。include按路径读取文本，头文件不要求.hpp；模板定义通常需保留在头中供调用方实例化。

独立目标探针包含全部五个头，实例化is_same/is_integral和基本span访问，编译通过。
这只验证已用子集，不代表完整标准兼容或全部边界正确。两个已检查的问题：

- utility的右值forward重载缺少拒绝T为左值引用的约束；实际 `forward<int&>(123)` 能通过当前目标编译，可能返回引用临时对象的悬空引用。应先补类型约束；对照本机libstdc++的bits/move.h有相应静态断言。
- span::subspan在请求超长时静默截短、offset越界时返回空，与标准span的范围前提不同。内核可明确设计返回失败的接口，但不应把请求截短后仍当作完整成功。当前只能视为项目子集，尚无完整标准契约验收。

后续按实际需要完善基础类型工具和这些既有接口，再返回地址类型；不自动扩写动态容器。
若继续自写兼容头，需要明确与现成标准库接入路线的边界，避免两套std定义混用。

### 当前五头的最小完善任务

作者问“怎么完善”后，范围限定在已有五头，不增加容器、分配器或全局生命周期框架。
作者主导实现，协作者承担下面列出的例行验证；先一次给齐要求，完成后集中审查。

1. **type_traits**：把utility中已有的remove_reference/remove_reference_t移到这里，避免重复定义；在已有true_type/false_type上补is_lvalue_reference及_v。is_same和is_integral保留。当前is_integral依赖Clang内建，目标与独立兼容层测试暂按Clang验证，不声称已支持所有编译器。
2. **utility**：包含type_traits，继续保留move和两个forward。作者已经加了右值forward的静态断言，但is_lvalue_reference_v尚未定义，头依赖也未接好；先补齐依赖，不能只看断言文字是否存在。
3. **span**：明确契约。合法子范围返回请求的精确offset/count；offset越界或count超出剩余区间时不能静默截短、返回空后假装成功。本轮可用__builtin_trap作为契约失败路径，不引入尚未准备好的panic/异常运行时；接收用户可控输入的调用方先校验，确有需求再加返回失败的try_subspan。
4. **span的已有接口一起检查**：operator[]在idx>=size时契约失败；指针构造拒绝nullptr配非零count；避免size_bytes乘法溢出；空视图的end和零长度子视图明确处理。先判断offset>size，再用count>size-offset，避免offset+count本身溢出。实际存储大小、指针有效性和生命周期仍由调用者保证，这些检查不能证明任意指针可访问。
5. **cstddef/cstdint**：当前需要的类型别名先保留，暂不补齐整个标准接口。std::byte的完整运算、所有整数别名等按实际调用需求再扩展，不作为当前门槛。

span只保存指针和长度，不拥有、不释放存储。const span<T>仍可修改T元素；只读数据应使用span<const T>。
本轮只实现当前动态长度子集，不宣称完整std::span兼容。

P0验证由协作者执行，不要求作者搭建新框架：

- 五头分别单独包含，以及组合包含，均能在当前RISC-V目标参数下编译。
- 编译期检查引用移除、左值引用识别、move/forward的返回引用类型；非法forward<int&>(右值)必须因对应约束失败，而非依赖缺失。
- span覆盖空、数组构造、正常修改原数组、精确子视图、末尾零长度；越界契约失败采用隔离探针观察，不能把验证进程意外终止当成功。
- 检查目标对象没有意外__assert_fail/异常/堆/动态全局初始化依赖；只在真实接入内核后按影响范围复验，不重复完整历史矩阵。

这些通过后返回地址类型，不继续自动扩写标准库。

### 首次集中检查：尚未通过

作者修改后，协作者使用实际RISC-V目标参数分别包含五头：cstddef/cstdint成功，
type_traits/utility/span失败。证据 `../.cache/stdlib-five-review-xqrv5_ov/`，包含完整诊断与五源文件哈希核对；未代改代码。

- type_traits：is_value_reference须统一为is_lvalue_reference；ture_type拼错；_v错误引用未定义is_rvalue_reference；true_type/false_type被用作基类时尚未定义。先放integral_constant与布尔别名，再定义依赖它们的trait。
- utility：仍重复定义已搬入type_traits的remove_reference三种模板；应只保留一份，依赖通过include取得。
- span：kernel_panic声明三参数、调用只有一参数，且仓库没有实现；本轮使用既定__builtin_trap即可，不新增panic框架。SIZE_MAX需包含定义头；溢出判断应为count大于上限，当前小于等于会误杀合法构造。
- subspan末尾仍用已删real_count，应改为返回上方算好的start和原请求count；operator[]仍未检查idx>=size。
- 作者额外新增decay/pair/make_pair：decay目前仅移除引用，没有移除cv或处理数组/函数退化。const int左值的make_pair会保留const，字符串或函数输入也无法按预期退化。可暂缓接入新增部分；若保留标准名称与相应用法，应另行补齐，不把它当作已经正确的标准实现。

当前存在编译阻塞，尚未进行运行期边界验收；不能把普通make未使用这些头的成功当作通过。

### 修改后复验：span通过，traits依赖仍阻塞

作者已修正span构造判断、去掉实际panic调用、补定义头、精确subspan和下标检查，
utility的重复remove_reference也已移除。独立目标包含cstddef/cstdint/span成功；
span探针生成真实RISC-V对象成功且无未定义符号。隔离宿主探针的正常路径和7条非法路径均符合预期：
空/数组/只读视图/修改原数组/精确子视图/尾部空子视图正常；null+非零数量、size_bytes溢出、
越界下标、越界offset、超长count、极大offset和空视图下标均触发trap。

当前剩余编译阻塞集中在type_traits：integral_constant完整定义仍放在使用它的true_type/false_type之后；
trait仍命名is_value_reference，_v仍引用不存在的is_rvalue_reference，utility却要求is_lvalue_reference_v。
应先定义integral_constant，再定义布尔别名，再统一定义is_lvalue_reference及_v；修好后才能验收forward。
此前新增decay/make_pair的未完成语义仍保留，不因基础span通过而视为已完成。

证据 `../.cache/stdlib-five-recheck-4d2prmrx/`，五源文件前后哈希一致；未代改源码或运行QEMU。

### 引用工具复验：逻辑通过，正式头仍缺命名空间闭合

作者已修正integral_constant定义顺序与is_lvalue_reference命名，目标类型检查通过。
但注释pair/make_pair时把namespace std的右花括号也注释掉，正式utility包含报expected '}'。
协作者仅在临时副本末尾补闭合：五头组合及move/forward引用类型检查通过，
forward<int&>(123)因预期静态断言被拒绝，证明约束正确。正式文件未改，不能把临时通过写成正式头已通过。
证据 `../.cache/stdlib-forward-review-_uxpgpj4/`。
另新增limits目前仍为空草稿且namespace拼为namespcae，独立包含失败；不计入已验证组件。

### 五头基础子集收尾

作者已把namespace std闭合移出注释。正式freestanding搜索路径下五头组合与move/forward类型检查通过，
非法forward<int&>(123)因预期静态断言失败；不再依赖临时修补副本。
证据沿用上方目录，补有production-invalid-forward.log/json。结合此前span目标对象及隔离边界验证，
本轮基础支持可收尾。pair/make_pair保持注释，未完整实现的decay不作为支持接口；limits草稿不计入本轮，也不阻塞返回地址实现。
这不是完整STL或现成目标标准库验收，后续仅随实际调用补组件。

## 选型与接线原则

1. 首选评估现成 libstdc++ 的 freestanding 配置；libc++ 作为候选。最终选择取决于当前版本、目标配置和实际组件验证，不只看宿主是否装有头文件。
2. 为 RISC-V/当前 lp64 目标取得或生成匹配的库配置。不能把宿主 x86_64 的目标配置头直接加入搜索路径，也不复制任意几份标准头来绕过其内部依赖。
3. 保留当前 ISA、ABI、代码模型和 C ABI 边界。若更换目标编译驱动，先验证兼容性并明确记录，不悄悄把 lp64 改成 lp64d。
4. 标准库头与运行时分开核对。头文件形式不代表没有外部依赖；模板实例化、断言、终止、构造/析构或辅助函数可能产生新符号。
5. 只补有实际需求的内核适配：例如断言失败接内核诊断/停机。分配、new/delete、异常和全局初始化的规则另行明确，不通过空桩或吞错满足链接。
6. 目标库配置与宿主测试库配置分别记录。内核仍不自动链接宿主/Linux 的动态 C++ 库；现有 freestanding、无异常/RTTI等约定继续保留，除非另有明确设计。
7. 固定最终选用的版本、来源、构建选项和校验信息；第三方许可与所需文件一并保留。完成选择前不虚构版本锁定或安装路径。

## 实施顺序和验收

先做工具链层的独立验证，再接入构建，最后回到地址类型。例行验证由协作者承担，
作者继续主导地址/页表功能实现，不追加一套标准库实现作业。

- 确定候选与目标配置，以首轮组件清单做最小编译/实例化探针。使用与内核相同的目标参数，不能只测试 include 或只计算常量。
- 用实际运行时输入验证 optional 的成功/失败、比较和视图访问等所需路径；检查目标对象的符号依赖，明确哪些操作可用。
- 以独立配置项接入 Makefile，必要时接入宿主 CMake。保持原 C 文件按 C 编译，避免全仓切换编译器或无关重构。
- 检查最终内核链接、ELF ABI、未解析符号、动态依赖和初始化节。出现新依赖就解释并处理，不用忽略错误的方式通过。
- 运行原宿主回归和一次 QEMU 正常路径，保持用户输出、退出和内核继续运行；没有必要重跑全部历史故障矩阵。
- 地址类型真正包含并使用新组件后，再验证该模块；原 address.hpp 待修项继续按改造文档集中处理。

## 决策与结果记录

- 2026-10-27：作者要求将相关改造独立成文；创建本文，仅记录计划与已核实环境。
- 随后作者自行新增 `freestanding/cstddef` 和 `freestanding/csdtint` 两份类型兼容头。后者文件名拼错，若通过 `<cstdint>` 包含需改名；目标CXXFLAGS尚未加入该目录。独立目标探针确认两份内容按实际文件名包含均可编译，加 `-Ifreestanding` 后自写cstddef可用，但 `<cstdint>` 因文件名仍找不到。未代改文件或正式构建配置。
- 自写兼容头只覆盖当前列出的类型，不等同完整标准库接入。若继续使用，搜索路径先限目标C++构建，不覆盖宿主标准库；将来选用现成实现时应统一替换，避免与同名std定义混用。
- 待记录：最终库/版本、取得方式、目标配置、受支持操作、实际依赖、构建接线与验证证据。
- 协作者当前未执行：安装包、下载/构建标准库、修改编译器配置或正式代码。作者新增兼容头不代表现成目标标准库已经接入；没有把计划写成已完成结果。

## 官方参考

- [libstdc++ freestanding 支持与链接说明](https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dynamic_or_shared.html)
- [libstdc++ 配置选项](https://gcc.gnu.org/onlinedocs/libstdc++/manual/configure.html)

官方文档说明 libstdc++ 可提供 freestanding 子集；具体可用范围取决于所选版本和配置。
上方候选组件仍须在本项目目标上验证，不能据此认定所有组件和成员操作都已受支持。
