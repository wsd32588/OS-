# TinyOS：C++ 标准库基础支持

更新：2026-10-27。状态：手写五头的当前基础子集已通过验证，返回地址/页表改造；尚未安装、构建或接入现成目标标准库。

本文件单独记录标准库选型、工具链接线、可用组件、运行时边界和实际验收结果。
地址/PTE/页表的功能改造继续记录在 [TINYOS_EVOLUTION.md](TINYOS_EVOLUTION.md)，不在两处重复维护。

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
