# 宿主测试

本批用户内存回收与失败回滚的范围和验收条件见
[USER_MEMORY_REQUIREMENTS.md](USER_MEMORY_REQUIREMENTS.md)。H1-H8 宿主用例与 Q1/Q2 的 QEMU 回归均已实现。

运行 `make test`。需要 CMake 3.20+ 和支持 C++20 的 Linux/WSL 宿主编译器，
可用 `HOST_CC`、`HOST_CXX` 指定。根 Makefile 只转交配置、构建和 CTest 运行；
测试的构建规则集中在 `test/CMakeLists.txt`。
测试由 C++ 编译；实际 `kernel/pmm.c`、`kernel/vm.c`、`kernel/user_memory.c`、`kernel/user_access.c`
仍由宿主 C 编译器分别编译，再通过 `c_api.hpp` 的 C 链接接口调用。
宿主构建产物保存在 `test/build/cmake/`，与 RISC-V 内核对象分开。

也可以独立配置和运行测试，无需先构建内核：

```sh
cmake -S test -B test/build/cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build test/build/cmake
ctest --test-dir test/build/cmake --output-on-failure
```

直接查看各用例的 PASS 输出可运行 `test/build/cmake/tinyos_tests`。
切换编译器时使用新的构建目录，例如 `make test TEST_BUILD_DIR=test/build/clang HOST_CC=clang HOST_CXX=clang++`。

当前测试目录顶层的 `.cpp` 通过 `GLOB CONFIGURE_DEPENDS` 自动加入构建，
新增文件后直接构建即可。该规则只用于这个小测试项目，不递归扫描构建目录；
新增测试函数仍需在 `test_main.cpp` 中调用，已有模块的新用例需接到模块入口。
参与宿主测试的内核 C 文件仍显式列出，以免误编译 CSR、trap 等架构代码。
自动发现依赖生成器支持，当前 Unix Makefiles 已验证；更换生成器时需复验。

CMake 导出宿主 `test/build/cmake/compile_commands.json`。
`make compdb` 用 Bear 生成内核命令后合并宿主命令，根数据库包含两种编译环境。

- `test_pmm.cpp`、`test_vm.cpp` 保留原有 PMM 和 Sv39 边界、压力及页表回收场景。
- `test_support.hpp` 提供类型安全的相等检查、失败定位、用例运行、前后状态观察模板。
- `host_support.cpp` 集中提供 UART/flush 替代，以及 unmap/free 的链接包装、调用计数和事件记录。
- `user_memory_fixture.hpp` 集中准备页池、根页表、实际拥有的代码/栈页和映射，并提供资源释放检查。
- `test_user_memory.cpp` 覆盖 H1 正常释放、H2 重复释放及初始空描述释放、H3 代码/栈各自 PA 不符或缺 U、H4 栈页释放失败后的重试、H5 手动解除叶映射后的释放、H6 非法参数、H7 保留其他映射，以及 H8 两处 flush 失败后的重试。
- `test_user_access.cpp` 集中运行 12 个用户内存复制场景：带偏移/NUL 的单页、虚拟连续而物理分隔的跨页、第二页缺映射/U/R、起点缺映射、NULL、零长度、溢出、起点/末字节非 canonical，以及 UINTPTR_MAX 处的单字节。检查源数据、页表、映射、页数、调用记录和目标哨兵，失败允许合法前缀；完整范围见 [USER_ACCESS_REQUIREMENTS.md](USER_ACCESS_REQUIREMENTS.md)。

H1 检查两处映射消失、四页归还、描述全空、根页表没有被 free，
以及 unmap/flush/free 的顺序。H2 使用公共模板比较返回值及完整状态快照：
描述、空闲页数、调用计数和映射必须保持不变。
H3 先确认异常映射的 PA/flags，再验证拒绝释放时完整状态保持不变；
恢复正确映射后还需成功释放并归还四页。
H4 在 free 包装中对指定栈页注入失败，首次仅归还代码页并保留剩余描述；
关闭注入后重试归还栈页与两级中间表。两个事件区间精确检查操作、地址和返回值，
确认代码页没有重复释放、栈页一次失败一次成功、根页表未被释放。
H5 先手动解除两处叶映射，确认物理页和中间表仍占用四页；
随后释放接受 NOT_MAPPED 并归还四页。完整八条事件验证两个 unmap、两个 flush，
以及代码、栈、L0、L1 的释放顺序、地址和返回值。
H6 验证 nullptr 参数和非空描述缺 root 时返回 INVALID_ARGUMENT，
完整状态及调用计数不变；缺 root 时通过保留的真实页表指针观察映射。
两个用例随后均确认正常释放成功。
H7 在同一 L0 中保留第三处独立映射，用户释放仅归还两页；检查第三映射、
两级父 PTE 不变，及 2/2/2 次调用与完整六条事件。随后测试清理第三页和中间表，
确认最终页数恢复，根页表仍保留。
H8 用 host_set_flush_failure 指定接下来第几次 flush 失败一次，0 关闭注入。
两个用例分别检查第一次、第二次失败；失败时叶映射已解除，但描述、物理页、
中间表均保留，没有 free。第一次失败会短路第二次 flush，因此调用增量分别为
2/1/0 和 2/2/0；撤销注入后重试均为 2/2/4，并检查完整事件及四页归还。

新增场景可按以下方式复用检查，具体期望值仍需由场景定义：

```cpp
tinyos_test::expect_no_change(
    [&] { return fixture.snapshot(); },
    [&] { return user_memory_release(&fixture.memory); },
    SV39_OK
);
```

对于应当改变状态的操作，用 `observe(capture, operation)` 获得
`before/result/after`，再检查预期变化。操作只执行一次；整数比较能区分
负错误码和无符号最大值。失败报告包含用例名、位置及可打印的实际/期望状态。

PMM 使用全局状态，测试顺序运行，每次只允许一个活动页池；不要嵌套创建页池。
宿主包含路径使用 `-iquote`，避免项目 `sched.h` 覆盖标准库需要的系统头文件。

宿主 `uart_puts()` 输出到 stderr，`sv39_flush_page()` 验证地址、记录调用并支持故障注入。
写 `satp`、执行真实 `sfence.vma`、验证 page fault/trap/调度仍需 QEMU。

完整回归可执行：

```sh
make test CTEST='ctest --verbose'
make
make test-qemu
```

`make test-qemu` 顺序运行以下两个脚本，需要 Python 3、RISC-V GNU 工具链和 QEMU：

- `python3 test/verify_prepare_rollback.py`：Q1 的四个分配失败、两个 flush 失败、两个映射冲突；随后正常演示 exit，至少继续两次 S-mode timer tick。
- `python3 test/verify_task_creation_rollback.py`：Q2 通过真实 task_create 填满普通槽，确认 task_create_user 返回 -1 且调用者描述保留，再验证 kernel_main 原有失败分支清理成功、恢复四页、描述全空、用户路径消失。根页表没有 free 尝试，satp、内核各段恒等映射和 root/UART 映射保留；随后停在原失败分支。

脚本各自建立并清理临时源码副本，探针不会写入工作区内核。
通过由内核探针检查实际返回值、描述、页数和映射后输出标记；缺少工具、构建失败、
检查失败或限时内缺少所需标记均返回非零。限时结束观察本身不表示通过。

WRITE 的 Python 教学脚本为 `verify_syscall_write.py`，作者已实现并分批验证
12 个场景：baseline、length_zero、unmapped_start、length_256、length_257、
binary_bytes、noncanonical、address_overflow、kernel_missing_u、
cross_page_unmapped、execute_only、cross_page_valid。
运行 `python3 -u test/verify_syscall_write.py`，
也可在命令末尾指定场景名称；未知名称会报错。脚本复制源码到临时目录，
基线使用当前用户镜像，生成场景替换临时副本的 user_entry.S，再构建、运行 QEMU。
每场景的 prepare_main 从仓库原始 main.c 开始，应用可选的 main_replacements，
防止上一例的权限变化污染后例；execute_only 去除用户代码页的 R 位，
cross_page_valid 临时分配间隔页并检查代码/栈 PA 不相邻。
用户汇编比较返回值，通过/失败分别输出 0x01/0x02；WRITE 前后输出 0x03/0x04，
Python 精确比较两者之间的原始字节，零长度要求空字节串 `b""`。
目前这些控制字节保留作探针标记，新增测试数据应避开 0x01..0x04。
保留原始 bytes 是为了后续检查 NUL；构建日志使用 text，UART 日志不做文本解码。
WRITE 本批功能验收已完成；root getter 边界在临时 QEMU 副本验证，尚未持久化。
WRITE 脚本尚未加入 make test-qemu，完整回归需要另行执行上述命令。

独立 C 用户程序已验收，构建和使用范围见
[USER_PROGRAM_REQUIREMENTS.md](USER_PROGRAM_REQUIREMENTS.md)。基线使用
user/main.c 独立链接的镜像，经 objcopy 和 kernel/user_entry.S 的 incbin 嵌入内核。
三个 QEMU 脚本均复制 user 源码并忽略 build，临时工程从源码重建镜像。
2026-10-07 完整 12 个 WRITE 场景与宿主回归通过；旧 Q1/Q2 由作者报告通过。
