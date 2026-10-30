# BATCH01：C++ 段复制模块接入

状态：已通过。实施时间：2026-10-26；整理：2026-10-29。

让 user_image 段复制逻辑以 C++ 编译并进入真实内核，保留原 C 接口、复制与补零行为。

这是混合编译和链接试点；后续的状态封装、地址类型及装载模块分别记录在后面的批次。

导航：[批次索引](README.md) · [总路线图](../TINYOS_EVOLUTION.md)

本文的裸源码路径以仓库根目录为基准；可点击的相对链接以本文件为基准。过程性记录保留原时点，当前状态以验收结论为准。

目标：由作者将 `kernel/user/user_image.c` 改为 `.cpp`，保持原C接口和算法，由C代码调用它，最终链接进真实RISC-V内核并运行。重点是编译、链接与ABI；状态封装、类、所有权设计留到后批。

## 当前验收（2026-10-26）

作者已完成.cpp、头文件C ABI、Makefile和CMake源路径。make -B与make test均成功且无警告，目标原C符号与ISA/ABI正确，最终内核无未解析符号，模块无意外C++运行时或动态初始化依赖。一次QEMU正常完成yield/R/sleep/XHello/Data OK/exit，退出后两次S timer，限时返回124符合常驻内核任务行为。
证据：`../../.cache/cpp-pilot-review-pq6dlz62/`；七个源配置前后哈希保持，协作者未代改实现。

作者随后已给tinyos_test_kernel补齐CXX_STANDARD 20、CXX_STANDARD_REQUIRED YES与CXX_EXTENSIONS NO。make test复验0且无警告，编译数据库确认该实现使用-std=c++20；证据同目录host-strict-cpp20.log/json。配置收尾完成，未重复QEMU，功能梳理与三题尚未进行。

## 已核对的工具链

2026-10-26 PATH中没有 `riscv64-linux-gnu-g++`，GCC查询cc1plus也未解析出安装路径。已有Clang 21.1.8；临时C++小程序以如下目标参数成功生成ELF64/RISC-V对象，flags为RVC/soft-float ABI，导出未改名的C符号。此探针只证明目标编译可用，尚未验证本模块与整个内核的混合链接。

本批使用 `clang++ --target=riscv64-unknown-elf`，C与汇编继续用现有GCC规则，最终继续用现有RISC-V ld链接。共同使用 `ARCH_FLAGS` 中的ISA、lp64、medany及禁用relax设置。

## 作者整批修改范围

1. **实现文件**：将 `kernel/user/user_image.c` 改为 `user_image.cpp`，保留参数校验、返回值、复制、补零和失败零写入行为。可将void指针转换写成static_cast；本批不要求为纯函数包装类，不引入堆、容器或动态全局对象。
2. **公共头**：`include/user/user_image.h`继续供C和C++共同包含。用 `#ifdef __cplusplus` 包围 `extern "C" { ... }`，使函数声明在C++下采用C语言链接；实现先包含这个头，定义就沿用该声明的链接方式。普通C调用者无需更名或改参数。
3. **根Makefile**：新增明确的目标C++编译器、C++选项和 `%.o: %.cpp` 规则。现有OBJS仍为 `kernel/user/user_image.o`，不能把旧.c和新.cpp同时编进内核。继续沿用DEPFLAGS，C与汇编规则保持各自语言。
4. **宿主CMake**：将tinyos_test_kernel源列表中的该路径改为.cpp，给这个库的C++部分明确设置C++20、必须满足标准、关闭扩展；原C源仍按C11编译。现有16个段复制场景继续测试同一实现，不另外复制算法。仅C++适用的编译选项用语言条件限定。

目标C++选项参考（ARCH_FLAGS继续复用）：

```text
-std=c++20 -Wall -Wextra -Wpedantic -O0 -g
-ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics
-fno-stack-protector -fno-pie
-fno-unwind-tables -fno-asynchronous-unwind-tables
-Iinclude
```

禁止异常、RTTI等并不意味着自动获得完整C++运行时；本模块没有这些需求。保持直接ld链接，不额外链接libstdc++。如出现未定义辅助符号，应检查原因，不能靠忽略链接错误通过。

`test/c_api.hpp`已有外层extern C，与头内保护兼容；该文件目前“same C objects”的注释可相应更正为相同内核实现。其余启动、ELF装载、PMM与调度代码本批不改。

## 集中验收与协作者承担的P0

作者完成上述修改后集中审查，例行复验由协作者承担：

- `make test`通过既有宿主场景，包括段复制的16个场景；确认CMake实际用C++编译该实现。
- `make -B`通过真实目标编译与链接，确认.cpp由目标Clang处理，其余C仍由GCC处理；不需要make clean。
- `riscv64-linux-gnu-nm kernel/user/user_image.o`导出 `user_image_load_segment`，没有C++改名；`nm -u kernel.elf`无未解析符号。检查对象ISA/ABI及没有意外C++运行时、动态初始化依赖。C端真实调用参与链接，不能仅靠宿主测试判定成功。
- `timeout 8s make qemu`保持yield/R/sleep/X/Hello/Data OK/exit，退出后时钟与A继续；124仍是限时结束观察。
- 保留现有错误输入拒绝与失败不写目标的测试即可，不新增故障矩阵。若暴露真实回归，再补针对性的复现。

临时工具链探针、正式混合链接与运行、严格C++20库配置现均已通过。作者选择继续PMM封装；编译/链接边界的理解可合并到本批梳理，不用再停留于工具链试点。
