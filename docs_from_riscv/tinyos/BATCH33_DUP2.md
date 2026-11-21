# BATCH33 用 dup2 将打开文件绑定到指定 FD

2026-11-19 交付并完成。固定 P0 见第 7 节，三题原答和澄清见第 8 节。

## 1. 为什么 shell 需要重绑定 FD

B32 的 shell 能把路径和参数交给前台程序。新程序通常从 FD 0 读输入，并通过 FD 1 和 FD 2 写输出。
现在如果 shell 打开一个只读文件，文件服务会从调用进程的 FD 表中选择最小的空槽。在标准流已经占用
0、1、2 时，打开文件通常得到 FD 3，目标程序仍会从 FD 0 读控制台。

后续若要支持输入重定向，shell 的子进程需要能把 FD 0 改绑到已打开的文件，再执行目标程序。
本批新增的 `dup2` 接受一个源 FD 和一个目标 FD。它让目标槽再持有一条指向源打开对象的引用。
目标槽原先有打开对象时，文件层归还这一槽持有的旧引用。shell 解析重定向符号留到后续批次。

### FD 槽和打开对象的关系

`include/process/process.hpp` 的 `tinyos::Process` 各自持有一张 FD 表。FD 数字是这张表的索引。
`include/fs/open_file.hpp` 的 `tinyos::fs::OpenFile` 记录打开对象的访问模式和普通文件的当前位置。
因此，把 FD 3 复制到 FD 0 后，两格可以指向同一个打开对象。从 FD 3 读取推进的文件位置，随后从
FD 0 读取时也会用到。通过 `user/syscall.S` 的全局入口 `user_close` 关闭 FD 3 只归还一条引用，
FD 0 仍可继续使用该对象。

```text
子进程在调用 dup2 之前
  FD 0 → 只读控制台打开对象
  FD 3 → /etc/about 的打开对象

子进程完成 dup2(3, 0) 之后
  FD 0 ─┐
        ├→ 同一个 /etc/about 打开对象，共用访问模式和文件位置
  FD 3 ─┘
```

隔离演示中，父进程先 fork，子进程随后打开 `/etc/about` 并改绑自己的 FD 0。父子有各自的 FD 表，
因此重绑定只影响子进程。子进程用原 FD 读走文件的前 6 字节 `TinyOS`，关闭原 FD，再通过
`user/syscall.S` 的全局入口 `user_exec_path` 执行 worker。
worker 从 FD 0 读到剩余的 12 字节 `" builtin fs\n"`，下一次读取返回 0，表示该普通文件到达 EOF。

## 2. 接口与工程约定

`user/syscall.S` 定义全局入口 `user_dup2`，C 声明放在 `user/user_api.h`。
在 `include/task/syscall.h` 定义 `SYSCALL_DUP2 = 21`。

```c
long user_dup2(long source_fd, long target_fd);
```

| 条件 | 接口行为 |
| --- | --- |
| 系统调用输入 | `a7 = 21`，`a0 = source_fd`，`a1 = target_fd`。两个 FD 在用户 ABI 中是有符号 `long` |
| 成功和失败 | 成功返回 `target_fd`，因此目标为 0 时返回 0。普通失败返回 `-1` |
| 编号和源槽 | 两个编号均须在 0 到 7。源槽必须已有打开对象。检查完成后才把用户提交的 `long` 转成 `int` |
| 目标槽为空 | 让目标槽持有源打开对象的一条新引用 |
| 目标槽持有另一个对象 | 先保留源引用，成功后再替换目标槽并归还旧目标引用 |
| 源与目标相同 | 源槽有效且两个编号相同，或两个槽已指向同一打开对象时，直接返回目标编号。原槽、引用数和位置保持 |
| 普通失败 | 编号无效、源槽为空、服务未就绪或无法保留源引用时，调用进程的 FD 表和已有引用保持 |
| 对象与访问模式 | 两槽共享源打开对象、访问模式和普通文件位置。源 FD 保持有效。标准 0、1、2 和其他编号使用相同规则 |

本批只实现指定目标的 `dup2`。现有内核仍使用每进程 8 槽和服务层的 40 项打开对象池。
本操作只调整已有对象的引用，使用空闲目标槽时也无需申请新的打开对象或物理页。正常的
fork、exec、exit 和 SPAWN 继续遵守 B28 的 FD 生命周期。

## 3. 代码位置、执行阶段和实现顺序

### 3.1 先做 FD 表本身的替换

在 `include/fs/file_descriptor_table.hpp` 声明，在 `kernel/fs/file_descriptor_table.cpp` 实现
`tinyos::fs::FileDescriptorTable::duplicate_to`。调用者传入其表所使用的 `OpenFilePool`，
表本身负责检查索引、判断源槽是否有效、判断两槽是否已指向同一个打开对象。这一层可以用宿主测试
直接观察两格的指针、对象引用数与文件位置。

```cpp
int duplicate_to(OpenFilePool& pool, int source_fd, int target_fd) noexcept;
```

对两个对象不同的槽，先让 `kernel/fs/open_file.cpp` 的
`tinyos::fs::OpenFilePool::retain` 为目标保存一份源引用。如果保留失败，表的方法直接返回，
旧目标槽此时仍然有效。保留成功后，表的方法让目标槽指向源对象，再把旧目标对象交给同文件的
`tinyos::fs::OpenFilePool::release` 归还一份引用。目标原先为空时省掉归还旧引用的步骤。
池负责在最后一份引用归还时清空旧对象。这里的成功与失败都可以通过关闭两个 FD 的先后顺序观察。

保留原来的 `open` 选空槽和 `close` 单槽语义。表的方法接受两个经过明确校验的槽索引，
核心实现由作者完成。

### 3.2 让进程和文件服务连接正确的表与池

在 `include/process/process.hpp` 和 `kernel/process/process.cpp` 增加
`tinyos::Process::duplicate_file(int source_fd, int target_fd)`。该方法把调用进程自己的
`files_` 传给 `include/fs/file_service.hpp`、`kernel/fs/file_service.cpp` 中新增的
`tinyos::fs::duplicate_file`。服务函数确认文件服务已就绪，并把自己持有的对象池传给
`tinyos::fs::FileDescriptorTable::duplicate_to`。复用现有文件服务对池的封装，调用链可以用
同一层已有的 `close_file` 调用作定位参照。普通失败应原样向上返回。

### 3.3 接入 21 号系统调用

用户包装函数在 U 态把两个 FD 放进参数寄存器，再用 `ecall` 进入 S 态。CPU 进入 S 态时仍使用
调用进程的根页表。两个参数都只是整数，因此本次系统调用无需访问用户缓冲区或切换根页表。

1. `kernel/task/syscall.c` 的全局 `syscall_handle` 增加 21 号分支，以 `long` 读取
   `frame->a0`、`frame->a1`，把下层结果写回 `frame->a0` 后结束分支。新增
   `include/task/sched.h`、`kernel/task/sched.cpp` 中的全局 `sched_duplicate_file`，
   沿用同文件 `sched_close_file` 的当前 USER 任务和 S 态中断关闭检查。
2. `sched_duplicate_file` 把当前任务拥有的 `tinyos::Process` 传给
   `include/fs/file_syscall.hpp`、`kernel/fs/file_syscall.cpp` 中新增的
   `tinyos::fs::syscall_dup2`。该函数先以 `long` 检查两端范围，再调用
   `tinyos::Process::duplicate_file`。这样类似 `1L << 32` 的编号也会被拒绝。
3. 目前单 hart 的内核在这段系统调用内关闭 S 态中断，调用链也不主动调度。表的替换和引用调整在
   同一段执行中完成。系统调用结束后，调用者从原 U 态执行点继续，可用返回的目标编号做下一次读写。

正式 shell 和 worker 的行为继续使用 B32。验证时，协作者在隔离 user 构建中使用临时程序
直接调用新接口。核心方法与接线由作者完成。

## 4. 固定 P0 验收

| 场景 | 通过条件 |
| --- | --- |
| 构建和既有回归 | kdbg 和 user 完整构建，`make test` 通过，`make CONSOLE_MODE=user test-process` 通过 |
| 宿主 FD 表操作 | 复制到空槽后两个 FD 指向同一对象，引用数加一，池的已用对象数保持。覆盖已占用目标时只归还旧目标那一份引用。自复制及同对象两槽间复制保持引用数和偏移。空源、越界返回 -1，原表和引用保持。关闭各槽后池归零 |
| 隔离 QEMU 拒绝 | 子进程已打开 `/etc/about` 得到 FD 3。尝试从空 FD 7 复制到 FD 0，返回 -1，旧控制台 FD 0 仍有效。传入超出 `int` 范围且低位为 0 的目标编号同样返回 -1，两个槽及引用保持 |
| 隔离 QEMU 合法替换 | 同一子进程调用 `user_dup2(3, 0)` 返回 0，两个槽指向同一个只读打开对象。子进程从 FD 3 读 6 字节 `TinyOS` 并关闭 3，执行 worker 后 FD 0 仍有效，读出 12 字节剩余内容，再读返回 EOF 0 |
| 隔离 QEMU 生命周期 | dup2 拒绝与成功均零页面申请和释放，成功时池不分配新对象。父进程的 FD 0 仍指向原控制台。worker 通过原 stdout 报告成功并退出 0，父进程收集对应子任务，最终退出后池归零 |

协作者负责 P0 的用例与执行。宿主用例在已有 `test/test_open_file.cpp` 的文件表测试基础上补一小组。
隔离 QEMU 继续使用正式内核和已有 `/etc/about`，临时调用者和 worker ELF 都保留有效的可写加载段。
P1 选做可观察源保留被拒绝时如何保持已占用目标，或验证全表占用时覆盖已有目标。P0 的范围以上表为准。

```sh
make -j4 CONSOLE_MODE=kdbg
make test
make CONSOLE_MODE=user test-process
make -j4 CONSOLE_MODE=user
make CONSOLE_MODE=user qemu
```

最后一条命令在正式树运行原 B32 shell，在装有 B33 测试用户镜像的隔离树运行 FD 重绑定演示。

## 5. 实现后的三道理解题

1. 子进程的 FD 3 指向普通文件对象 A，FD 0 和 FD 2 都指向控制台对象 B，且只有这两格持有 B。
   CPU 在 S 态处理第一次 `dup2(3, 0)` 时，保留 A 的引用失败。第二次调用成功。
   两次调用各自返回时，FD 0、FD 2 及 B 的引用数会怎样？表层何时可以修改目标槽？
2. 子进程打开 `/etc/about` 得到 FD 3，复制到 FD 0，再独立打开同一个文件得到 FD 4。
   子进程从 3 读取 6 字节，从 4 读取 2 字节，随后关闭 3 并执行新镜像。
   worker 第一次从 0 和从 4 读取时，各自从哪个字节位置开始？说明哪些操作使用了同一个打开对象。
3. 父 shell 和子进程最初各用自己的 FD 0 持有同一个控制台打开对象，这个对象只有这两份引用。
   子进程把自己的 FD 0 改绑到只读文件并成功 exec。父进程的 FD 0、控制台对象的引用数与
   子进程新 FD 0 的访问模式分别怎样变化？

可选阅读《操作系统：设计与实现》第三版第 5 章 5.1“文件”。阅读时对应进程的 FD 槽和内核打开文件对象，
重点看为什么打开位置能在多个描述符之间共享。

## 6. 首轮调用链审查（2026-11-19）

用户包装、21 号分发、`sched_duplicate_file`、文件系统调用层、进程和文件服务均已接线。
隔离 user 构建无警告。内核 ELF 的 C 入口 `sched_duplicate_file` 和 C++ 入口
`tinyos::fs::syscall_dup2` 均解析，两个用户 ELF 均有全局符号 `user_dup2`。
证据位于 `.cache/batch33-chain-DOWXvKBY/` 的构建和符号日志。

`kernel/fs/file_syscall.cpp` 的 `tinyos::fs::syscall_dup2` 把两个上界比较写成了
`fd < kCapacity`，有效编号会被拒绝，某些无效编号会走到隐式窄化。应先以 `long`
拒绝 `fd < 0` 或 `fd >= kCapacity`，再显式转成 `int` 交给
`kernel/process/process.cpp` 的 `tinyos::Process::duplicate_file`。
表层的上界、相同对象路径和引用替换顺序已静态核对。作者随后修正系统调用层的范围判断，
正式 P0 结果见第 7 节。

## 7. 全部固定 P0 验收（2026-11-19）

作者修正 `kernel/fs/file_syscall.cpp` 的 `tinyos::fs::syscall_dup2`。该函数现在先在
`long` 范围内排除无效值，再把两个编号传入 `tinyos::Process::duplicate_file`。
协作者在 `test/test_open_file.cpp` 增加两组宿主用例，并在隔离 QEMU 中核对真实用户调用。

| 验证项 | 实测结果 |
| --- | --- |
| 构建与旧进程回归 | kdbg 和 user 完整构建均无警告、无未定义符号。旧 `test-process` 的 27 项通过，运行申请 21 页、释放 35 页，净恢复 14 页，文件池归零。 |
| 宿主 FD 表 | 3 项 CTest、139 个独立用例通过。新增两组覆盖空槽复制、覆盖旧槽、偏移和只读模式共享、自复制、同对象复制、无效输入保持原状态，以及所有槽关闭后池归零。 |
| QEMU 拒绝 | 子进程用空源 FD 7 和超范围目标分别调用 21 号，两次都返回 -1。8 个槽的绑定、引用数及偏移保持，父子原 FD 0 都仍指向控制台。 |
| QEMU 替换 | 同一子进程的 `user_dup2(3, 0)` 返回 0。FD 0、3 指向同一只读普通文件对象，持有两份引用。子进程的旧控制台对象少一份引用，父进程的引用保持。三次 dup2 均零 PMM 申请和释放、零打开对象申请。 |
| QEMU 跨 exec | 子进程从 FD 3 读取 `TinyOS` 后关闭 FD 3，再以 19 号执行 worker。内核切换到 worker 的用户根，保留 FD 0 的文件绑定与偏移 6。worker 通过 FD 0 读取剩余 12 字节，再读得到 EOF 0，并经原 stdout 报告成功。 |
| QEMU 收集与回收 | 父进程的 FD 0 仍指向原控制台，父进程按对应子任务 ID 收集退出状态 0，最后自身退出 0。运行期申请 12 页、释放 18 页，净恢复启动时的 6 页，打开文件对象池归零。 |

基础构建和旧进程回归汇总在 `.cache/batch33-build-process-nzMIJLKK/results.json`。
宿主日志在 `.cache/batch33-host-Cq9SkgGV/host-verbose.log`。
QEMU 专项 16 项检查全部通过，结果和 UART 记录在 `.cache/batch33-dup2-qemu-QrMeEJS8/`。
交叉构建和隔离 QEMU 两组验收中，108 个正式源文件的哈希均保持一致。
QEMU 测试使用临时调用者和 worker ELF，内核观察器记录调用与资源变化，实际调用仍由正式内核实现执行。

## 8. 三题原答与集中澄清（2026-11-19）

### 作者原答

> FD0的引用会变成对象A,FD2不变,文件B的引用数-1，表层在当前是SIE关闭，且FD0指向不为null,源文件目标文件的fd都在槽内的时候

> 子进程从0在offset 0时开始，从4在offset 6时开始，exec_path_args,dup

> 父进程的FD0引用不变，控制台对象引用数-1，新进程的访问模式变成了ReadOnly

### 集中澄清

1. 第一次保留 A 的引用失败时，`kernel/fs/file_descriptor_table.cpp` 的
   `tinyos::fs::FileDescriptorTable::duplicate_to` 直接返回 `-1`。FD 0 和 FD 2 都仍指向 B，
   B 保持两份引用。第二次调用先保留 A 的引用，成功后才替换 FD 0，再归还旧目标持有的 B 引用。
   此时 FD 2 仍指向 B，B 由两份引用减为一份。SIE 关闭是调度层的执行前提，表层检查两个 FD 的索引
   和源槽状态，目标槽可以为空。
2. FD 3 和 FD 0 指向同一个打开对象，从 3 读取 6 字节会将这个对象的偏移推进到 6。关闭 3 后，
   FD 0 持有的引用让该对象继续有效。第二次独立打开创建的 FD 4 指向另一个对象，只推进了 2 字节。
   `user/syscall.S` 的全局入口 `user_exec_path_args` 发起镜像替换时，子进程保留两格及其对象，
   所以 worker 首次读取 FD 0 从偏移 6 开始，读取 FD 4 从偏移 2 开始。
3. 父进程的 FD 0 继续指向原控制台对象。子进程的 FD 0 改绑时，控制台对象的引用数由 2 减为 1。
   `kernel/fs/open_file.cpp` 的 `tinyos::fs::OpenFilePool::acquire` 在打开普通内置文件时，
   已把该文件的打开对象设为 `ReadOnly`。dup2 让子进程的 FD 0 指向这个对象。
   `kernel/task/user_task_exec.cpp` 的 `tinyos::user_task` 内部函数 `exec_impl` 更新同一子进程的
   用户空间和现场，保留 FD 表，所以新镜像经 FD 0 读取只读文件。这个场景里子原先的控制台 FD 0
   也是只读，实际变化是 FD 0 绑定的对象。

B33 功能、固定 P0 和三题均已收尾。
