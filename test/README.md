# 宿主测试

## 单槽块缓存

`test_block_cache.cpp` 链接真实缓存实现，用内存磁盘检查命中和干净替换、延迟写回、
写回与回填失败后的重试顺序，以及无效参数与只读写入的拒绝。检查完整扇区内容、调用方
哨兵及实际回调顺序。真实 VirtIO 写回由 B37 的隔离 QEMU 探针验证。

## 打开文件对象与FD表模型

`test_open_file.cpp`链接真实FS、OpenFile/Pool和FDTable，验证独立打开与共享继承（含fd空洞）、
准备读取/提交实际count、EOF/空文件、关闭最后引用、8个fd及40池对象满额拒绝/重试、adopt与状态保持。
类型禁止复制移动，模型操作检查零PMM；retain引用溢出仅审查正式实现，不改私有字段伪造溢出。
宿主采用正式`runtime/panic.h`，不变量失败的C桩直接abort，不通过noexcept抛异常；这不替代目标panic/QEMU验证。

`test_file_syscall.cpp`先绑定一个长期static提供者，`test_process_files.cpp`复用同一真实文件服务：
验证Process文件引用阻止reset、失败元数据保持、clone/adopt及候选撤销、仅释放地址空间不关FD、
显式关闭后reset，以及真实拥有/映射页面上的typed OPEN/READ/CLOSE和拷出失败后位置不变。
服务池保持私有，没有测试reset/getpool钩子；复杂U-mode生命周期与跨页失败仍由独立QEMU验证。

## 只读内置文件表

`make test`通过真实`kernel/fs/builtin_fs.cpp`运行`test_builtin_fs.cpp`：初始化全预检与重试、
重复路径/重复初始化、描述值复制和借用指针、完整路径/组件与127/128字节边界、空文件、
二进制/分段/短读哨兵及EOF/SIZE_MAX/空目标读取。原始名字和字节保持有效，测试不伪造超大span。
这部分是纯算法宿主验证；boot从文件查找结果取得ELF、原用户生命周期与页面计数由QEMU验证。

`test_file_syscall.cpp`再链接真实`file_syscall.cpp`、文件表、usercopy与页表：验证绑定前拒绝、
未初始化绑定失败后的正确重试、重复绑定保持原提供者、7+11分段读取，以及短读只复制实际count、
EOF/极端offset/零length不访问目标；检查读取零PMM及映射/路径保持。测试提供者及底层字节为静态长期存储，
不新增生产reset接口。它直接调用C桥接，不替代真实U-mode寄存器分发和跨页错误的QEMU验证。

## TaskTable循环压力与测试工厂

```sh
make test
make test-stress
# 调整真实QEMU循环数（宿主记录层固定500轮）：
make test-stress TASK_STRESS_ROUNDS=1000
```

`make test`现包含`test_task_table.cpp`：测试专用`TaskFactory`每轮创建5个空用户空间的
Task/Process记录，满槽拒绝、终止、回收后再创建，500轮累计2500个新ID。
检查最小空槽复用、旧ID失效、高水位保持、对象/内核栈地址稳定、结果与父关系清空，以及零PMM调用。
`host_task_context.cpp`仅模拟现场布局；这不是用户代码执行或RISC-V硬件验证。

`make test-stress`先执行宿主测试，再运行`verify_task_table_stress.py`，默认真实QEMU500轮。
测试在`.cache/task-table-stress/`新建隔离源码副本，保留原-O0和一页用户代码断言，
只在副本中省略两S演示，使用1个固定USER收养者加4个真实工作进程占满5槽。
收养者每轮通过测试工厂创建一批、等待所有结果、reap，再开始下一批；最后自己退出。
这是当前创建权限允许的连续运行方式，不新增内核任务回收或收养者重启规则。

用户层测试工厂位于`test/user_stress/task_factory.{h,c}`，只调用真实用户接口：
偶数轮4次SPAWN，奇数轮4次FORK；每4轮包含一次EXEC替换与一次预期load fault13。
每轮还尝试一次槽满SPAWN并验证失败，检查实际返回ID连续、父子数据/栈隔离、结果正确及旧ID不能重收。
fork子分支直接执行工作函数并退出，不再次进入父的循环。

副本中的`observer.inc`借用既有PUTCHAR检查点，只读观察任务表，并包装实际PMM调用：
每轮检查4个槽全部UNUSED、父/结果清空、Task/Process地址稳定、页面余额回到基线；
新分配PA登记、free必须对应一个仍活跃的PA，检测重复释放或遗漏。
默认500轮为2000个子进程、1000次SPAWN、1000次FORK、125次EXEC、125次FAULT、500次满槽拒绝；
运行期分配/回收18375页次，最后根再释放7页，最终free18382页次。
检查的是周转页次数，非同时占有18375个物理页。

可直接运行：

```sh
python3 test/verify_task_table_stress.py --rounds 500 --timeout 120
```

每次保存隔离源码、构建/布局/QEMU日志、源哈希及results.json；同名输出目录拒绝覆盖。
出现FAIL、超时、构建问题或断言不符会失败；完成根退出并观察两次S-mode idle timer后外部停止QEMU。
原正常用户演示与产品策略均不被脚本修改；压力测试不自动代替所有批次P0或多hart验证。

多内置ELF接入后，副本仍保留DEMO/WORKER两镜像构建与统一registry配置，只替换DEMO为压力程序。
压力负载继续通过旧SPAWN8/EXEC10默认选择DEMO，因而同时回归旧ABI；WORKER的新选择路径由正常演示及当批专项验证。

本批用户内存回收与失败回滚的范围和验收条件见
[USER_MEMORY_REQUIREMENTS.md](USER_MEMORY_REQUIREMENTS.md)。H1-H8 宿主用例与 Q1/Q2 的 QEMU 回归均已实现。

运行 `make test`。需要 CMake 3.20+ 和支持 C++20 的 Linux/WSL 宿主编译器，
可用 `HOST_CC`、`HOST_CXX` 指定。根 Makefile 只转交配置、构建和 CTest 运行；
测试的构建规则集中在 `test/CMakeLists.txt`。
测试由 C++ 编译；实际 `kernel/mm/pmm.c`、`kernel/mm/vm.c`、`kernel/mm/user_memory.c`、`kernel/mm/user_access.c`、`kernel/user/user_image.c`
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
- `test_user_image.cpp` 用表驱动运行 S1-S10 共 16 个 `user_image_load_segment` 场景：初值加零尾部、恰好填满、纯 BSS、合法空段、文件比内存长、容量不足、三种镜像越界、SIZE_MAX 极端参数、两类 NULL 指针和零长度非法偏移。`Scenario` 每行给出镜像、目标偏移与容量、三个长度和期望结果，`run_scenario` 自动用 0xcc 哨兵准备缓冲区，成功时比较整个目标与源镜像，失败时要求目标与镜像逐字节不变；完整范围见 [USER_SEGMENT_REQUIREMENTS.md](USER_SEGMENT_REQUIREMENTS.md)。

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
user/main.c 独立链接的镜像，经 objcopy 和 kernel/user/user_entry.S 的 incbin 嵌入内核。
三个 QEMU 脚本均复制 user 源码并忽略 build，临时工程从源码重建镜像。
2026-10-07 完整 12 个 WRITE 场景与宿主回归通过；旧 Q1/Q2 由作者报告通过。

## 进程与文件描述符隔离回归

完整进程/FD回归负载保存在 `test/process_regression/main.c` 与 `worker.c`，
从原生产用户程序按字节迁入。正常启动只保留控制台任务；回归可单独运行：

```sh
make test-process
# 或指定证据位置：
python3 test/verify_process_regression.py --output-dir .cache/my-process-regression
```

脚本复制当前产品源码，在隔离副本换入保存的两个用户程序和 `boot_tasks.cpp`
测试工厂，创建原两个内核任务、两个用户任务及收养关系，再使用 `KERNEL_TRACE=1`
构建并运行真实QEMU。保留fork快照/隔离、spawn、exec、旧READ_FILE与FD读日志和
退出0/23/37/0检查；观察器验证五次镜像各释放七个唯一物理页、35次free、运行期
21次分配、净恢复14页、三次reap零PMM、文件调用零PMM、最终池空和后续两次S tick。
默认每次在`.cache/process-regression/`新建独立目录；输出目录保存构建/串口日志、前后正式源码SHA256与结果，脚本不修改正式产品或
复用`.cache`里的实现。看到最终账本标记后由宿主停止QEMU；限时结束本身不算通过。
本入口不迁移历史五页脚本，也不追加压力、边界或OOM矩阵。

正常`make qemu`默认`KERNEL_TRACE=0`，只启动控制台任务，不自动执行上述负载。
测试在隔离副本使用`KERNEL_TRACE=1`保留过程日志，实际定时中断/调度与错误诊断始终存在。
生成的`build/kernel_config.h`使trace值变化时自动重编内核对象，无需清理构建。
压力测试同样换入自己的`test/user_stress/boot_tasks.cpp`，不再按生产boot文本位置剪掉任务。

B24 的 `test_kernel_console.cpp` 链接真实 `console.cpp`/`file_view.cpp`，在已有
文件服务永久绑定后复用 `/etc/about` 与 `/empty`，不新增配置重置接口。
六组用例检查cat二进制65字节按64+1同步输出、空文件、hex精确格式/64字节预览，
以及命令与错误、CR/LF/CRLF、BS/DEL、160/161字符边界和非法字节整行拒绝后恢复。
输出回调使用固定缓冲同步复制字节与记录块长，回调中不抛异常；断言在返回后执行。
每组核对文件描述/源数据和PMM计数保持。UART寄存器、真实定时抢占与终端输入
仍由独立QEMU验证，不由这些宿主用例代替。

B25 的 `test_input_queue.cpp` 链接真实 `kernel/console/input_queue.cpp`。
五组确定性用例检查空/0/255、256字节满容量与拒绝保持、跨数组末尾FIFO、部分提交、
重复查看/放弃交付后重试、超量提交拒绝和目标护栏/未用尾部保持；每组核对零PMM操作。
“放弃交付”只验证不commit可保留输入，不宣称已经接入用户stdin或验证真实用户拷出失败。
当前队列由task_a独占，真实UART经队列到console的接线仍由QEMU验证。

B26 的 `test_console_device.cpp` 链接真实 `console_device.cpp`/`console_service.cpp`。
五组用例检查未配置拒绝、配置预检与空context、重复配置保留回调/待输入、每次最多接收一个字节、
满256时不读硬件、消费后恢复接收与回绕、peek/提交拒绝重试及同步二进制输出。
有限输入/输出假回调使用固定存储且noexcept，断言在回调返回后执行；每组核对零PMM操作。
服务专用用例使用长期context，仅成功配置一次，不添加测试重绑或暴露私有设备。
设备内部暂空返回0，不代表用户EOF；本批不接用户FD或stdin。真实服务/UART与kdbg路径继续由QEMU验证。

B27 的 `console_fd/main.cpp` 增加独立 `tinyos_console_fd_tests`，CTest分别以kdbg/user参数启动两个进程。
每个进程的文件服务和控制台服务各绑定一次，沿用真实对象/池/FD/Process/页表和syscall算法，
不添加生产reset或全局getter，也不改变原120组用例的配置顺序。
两模式各四组，user另两组，共十组：配置失败保持/模式互斥、混合池与满表/槽复用、
独立打开和继承引用共享永久输入流、精确路径及普通只读文件保持、暂空/零长/复制失败重试、
写入实际长度/二进制与完整复制失败零输出。合法源/目标使用真实用户页映射，操作前后核对零PMM，
fixture结束恢复全部物理页。真实U态SIE状态、寄存器ABI和UART交付继续由隔离QEMU验证。

B28继续扩展同一`console_fd/main.cpp`，沿用每个进程各绑定一次的服务fixture。
两模式各新增三组，user另加一组，共七组：标准0/1/2的独立对象与R/W/W权限、
None及关闭后能力清空、原无参consoleopen保持RW、两个可用池槽时第三项准备失败的完整撤销与重试、
非空目标拒绝、继承空洞/共享引用与adopt移交、独立fresh表、关闭标准号码后普通文件复用、
最后关闭仍保留设备输入，以及标准syscall的方向检查先于零长成功/poll/复制。
stdout/stderr分别检查实际二进制长度，所有新增文件操作核对零PMM，真实映射fixture最终完整回收。
服务未ready及kdbg准备拒绝在原配置用例内检查，不增加reset或公开私有池。
实际boot/SPAWN创建、fork/exec与退出的标准FD关系由独立QEMU验证，宿主表测试不替代该执行路径。
