# BATCH07：安全写回用户内存

状态：功能、P0验证及三题原答/审查/修正版均已归档，本批完成。日期：2026-10-30。

导航：[批次索引](README.md) · [上一批](BATCH06_MULTI_PAGE_DATA.md) · [总路线图](../TINYOS_EVOLUTION.md)

## 实现与验收（2026-10-30）

作者完成写回核心、C/C++接口、原始time读取、GET_TIME分发和用户包装；讨论中修正了复制方向、`try_add`失败判断、canonical半区、const source、成功返回0及user_exit汇编兜底位置。作者随后明确让协作者继续收尾：协作者保留核心算法和命名，仅把返回long的GET_TIME助手失败统一为-1、取得有效root后再读取局部const时间值；为正式用户演示补齐两次普通栈采样的递增比较和检查返回长度的 `Time OK` 输出。没有新增整数组扫描或把临时跨页探针放进正式main。

独立产品审查无阻塞项。正式 `make -B -j4` 无警告；`user_copy_to` / `timer_read_time` 保持C符号，最终内核无未解析符号、无init/fini/preinit数组。新用户ELF为RX filesz=memsz `0x302`、RW filesz `0x11` / memsz `0x1038`，仍是一页代码、两页数据/BSS和一页栈。

`make test CTEST='ctest --verbose'` 通过75个具体运行组（另1条汇总），无警告。新增5组精确覆盖页内偏移与0/0xff、PA分隔的3+5写回、首目标缺W/缺U零写入、后页只读保留合法前缀、零长度/NULL和三种范围拒绝。原70个场景和原12组读取测试语义保持；完整目标/隔离页/相关页表、源、映射、页数、调用计数和事件均按应改字节构造期望后比较。独立测试审查确认没有削弱旧回归。

正式 `timeout 8s make qemu` 输出 `Time OK`、Hello、Data OK和exit各一次，无fail/fault/halt/release错误，退出后至少两次S timer；124是限时观察。

协作者的一个临时源码副本完成剩余真实QEMU P0：

| 检查 | 实测结果 |
| --- | --- |
| 四次真实GET_TIME | 正式两次栈采样，加临时3+5跨页及代码目标拒绝各一次；均通过真实time CSR、U-mode trap和正式复制算法 |
| 3+5跨页写回 | 每个结果字节等于该调用内核source；左右保护字节及四张内容页的其他字节保持 |
| 用户写权限 | 代码目标返回-1，完整4KiB代码页保持，用户随后继续执行 |
| 内容与映射分离 | 每次copy的alloc/free/flush增量均0，root/L1/L0完整表、拥有描述及satp保持 |
| 退出回收 | 4内容页+2中间表共6页恢复，4张用户映射撤销，描述全空，root完整恢复且仍活动，5处内核恒等映射样本保持，后续两次S timer正常 |

探针前后56个正式产品源/头/配置文件哈希一致。QEMU使用实际分配布局，分隔PA的精确复制由宿主测试证明；不把默认PA布局说成强制分隔。临时QEMU由脚本8秒SIGTERM停止，返回0不代表客体主动关机。

证据集中在仓库根 `.cache/batch07-finish/`：`before/`保留作者收尾前草稿，`build.log` / `default-results.json` / `qemu-default.log`记录正式构建运行，`host.log` / `host-results.json`记录宿主结果，`symbols.log` / `undefined.log` / `sections.log` / `user-layout.log`记录ELF检查。`qemu/`的 `run_probe.py`、`probe_hooks.h`、`probe_impl.inc`和build/qemu日志及 `results.json`可追溯临时验证；正式源哈希另见 `product-hashes.json`。

既定限制保持：逐页失败可能留下合法前缀，调用者失败后不能使用整项结果；内核source有效/足长/不重叠、PTE指向可访问RAM、复制期间映射不并发更改由调用路径保证。GET_TIME只有原始time计数，没有频率换算或日期语义；A/D不由软件copy设置。P1/P2未扩展，不阻塞功能验收。功能验收后完成下方三题集中纠错与归档，没有追加程序验证作业。本批收尾未提交、推送、clean或创建worktree。

## 当前执行路径梳理

用户把自己变量的地址放进a0；汇编包装设置a7并ecall。syscall取得当前用户root，在内核栈上读取一个uint64_t时间值，把其8个字节交给user_copy_to。写回核心先检查整个VA范围，再逐页query并检查V/R/W/U，按各页实际PA写入，成功返回0；分发把状态写回frame->a0，由sret恢复给用户。

用户仅在返回0时读取自己的变量。改变的是目标内容字节；PTE、内容页归属和PMM页数保持。时间值与状态走两条输出路径：变量内存拿到时间，a0拿到0/-1。本批GET_TIME选用内存输出是为了练通写回机制，并不表示单个64位值都必须经缓冲区返回。

## 理解题与答案归档（2026-10-30）

### 第一题：const页表与用户写权限

题目：为什么root仍可为const，函数却能改变用户内容？既然内核能经PA别名写，为什么还必须检查用户PTE的W和U？

作者原答：

> 因为root只存地址，而指向的物理页其他内容和它其实不相干，能直接写入但是不能让他随便写，该检查的还得检查，更何况写权限和用户空间这两个还经常会爆出恶意bug

审查：区分页表和被映射内容、认识到仍须限制写入权限，这两点正确。若“root只存地址”指C入口的root指针变量本身，这句话成立；但被const约束的是它指向的页表对象。页表对象实际含512个PTE，PTE包括页号与标志，可能指向下级表；C++核心通过const引用访问该表。const约束通过该接口不修改表对象，不会让经地址翻译找到的内容页也自动变成const；表与内容通过映射关联，并非完全无关。

修正版：查询过程只读页表和权限信息，最终通过实际PA的内核恒等映射别名写入目标内容，所以不需要修改root或PTE。这个内核别名的可写性不能证明用户VA有写权限；必须用W检查该用户映射允许写，用U检查它允许用户访问，连同V/R验证本批合法可写用户叶映射。软件检查负责让内核只代用户完成当前root下允许的访问，不能因为内核能写就跳过策略。W/U检查本身不额外证明多个用户任务共享root时的任务间归属隔离。

### 第二题：失败前缀与两条输出路径

题目：8字节目标位于页界前3字节，第二页只读，哪些字节会改变、返回什么、用户能否使用结果？与WRITE的整次UART输出策略有什么区别？

作者原答：

> 第一页最后的3字节，返回0，不能，write复制之后还能换个地方暂存然后输出，但直接写不能

审查：第一页末3字节已写入、整项时间结果不可用，判断正确；返回值应是失败-1，不能返回0。底层user_copy_to返回INVALID_ARGUMENT，GET_TIME转换为long -1。WRITE已经先用内核buffer暂存，不需要再找另一处；关键是UART输出发生在整次读取成功之后。这里的直接写回采用单遍复制且不撤销前缀，这是当前实现约定，并不表示其他设计永远不能预检或回滚。

修正版：先将前3字节写入第一页，处理第二页时因缺W失败，后5字节保持原值；user_copy_to返回-1，GET_TIME也返回-1。目标可能混合新旧字节，用户只能在成功返回0后使用整个时间值。WRITE使用本次调用的固定内核栈buffer；user_copy_from失败也可能已改该buffer的合法前缀，但syscall_write会直接失败，不向UART输出这些字节；完整读取成功后才输出。因此它可以保证本次复制失败时UART零输出，而copy_to并不保证失败时用户内存零变化。

### 第三题：时间、现有映射与资源变化

题目：原始time计数、sleep(2)的2、输出的8字节分别是什么？为什么GET_TIME不需要新页或TLB刷新？

作者原答：

> 原始time是直接从硬件计时器拿东西，sleep(2)是我们定义出的大小的2，本来就只是从硬件计时器拿到的时间，自然不需要分配新页凑数，因为根本就找不到映射的那一页

审查：time来自计时来源的方向正确，须进一步区分计数单位、调度器睡眠次数和存储字节数。“找不到映射”与成功前提相反：目标必须已经有可写用户映射，找不到就失败。本次无需分配或刷新，与数据来自计时器没有直接关系；决定因素是只改已有页的内容，没有创建/更换映射或修改PTE。

修正版：timer_read_time读取time CSR原始计数，其增长频率由平台/执行环境决定，本批不换算秒；计数自行推进，GET_TIME没有主动增加它。sleep(2)表示当前调度器处理两次timer中断后唤醒，不是time增加2，也不保证睡满精确的两秒。8字节是一个uint64_t结果的存储大小，和前两个计数单位无关。用户目标已由装载流程分配并映射，copy_to逐页查询、检查后写入现有内容页，目标未映射则返回失败，不替用户补页。正常写回改变目标8字节和返回a0，正常syscall还推进保存的sepc；PTE、VA→PA关系、拥有记录、PMM页数和root/satp保持，所以无需分配新页或调用sfence.vma。用户退出时才由原释放流程回收内容页和空中间表。

三题原答、审查与修正版已集中归档；本批理解收尾完成，不要求作者重答或追加程序测试。此次仅改文档和索引/记忆，沿用前述已通过验证，未重复构建或QEMU，未自动进入下一批。

## 本批解决什么问题

BATCH06 已经能装载跨页数据/BSS，并记录、转移和释放每张内容页。当前 `user_copy_from` 只解决用户 → 内核的读取，WRITE 使用它接收用户缓冲区。内核如果要把查询结果返回到用户提供的地址，还缺少反方向的权限检查与逐页复制。

本批只推进“内核 → 用户的安全写回”：作者实现 `user_copy_to`，再用一个很小的 GET_TIME 系统调用写回 8 字节时间计数，形成真实 U-mode 调用、S-mode 复制、U-mode 读取的闭环。时间读取复用现有 `timer.c` 的 `time` CSR 读取，不增加时钟状态或时间换算。

暂不做区域描述、独立 root、堆、READ/UART 输入、动态任务或调度器迁移。它们各有独立的生命周期问题；当前方向能直接复用 BATCH06 的页集合与跨页数据区。`UserMemory` 的拥有关系和 `user_copy_from` 的原接口、失败语义保持。

作者亲手完成核心和用户演示，整批完成后集中审查；协作者负责解释、少量必要宿主场景和例行 QEMU 复验。本任务没有预先代写实现或新测试框架。

## 已核对的源码起点

以下是 2026-10-30 只读核对结果，不是本批新功能已通过验证：

- `include/mm/user_access.h/.hpp` 和 `kernel/mm/user_access.cpp` 只有 `user_copy_from` / `tinyos::copy_readable_user`；现有循环按页 query，再从实际 PA 复制。
- `Sv39PageTable::query` 是 const，返回的 PA 已包含 VA 的页内偏移；它查询映射，不证明此访问符合用户写权限。
- `sched_current_user_root()` 返回当前 RUNNING 任务借用的 const root；普通内核任务/idle 没有用户 root。写内容并不需要修改页表对象，可继续使用此入口。
- `kernel/task/timer.c` 已有私有 `read_time()`，供 `timer_set_next()` 使用；当前 syscall 编号 1—5，用户包装通过 `a7` 和 `a0/a1` 调用。
- 默认代码 `0x40000000` 为 RX/U，栈 `0x40001000` 为 RW/U，`0x40002000` 是空洞，数据从 `0x40003000` 起占两页；现有 `buffer[4096 + 32]` 可用于真实跨页写回。
- 只读当前已有 `user/build/demo.elf` 的符号：buffer 为 `[0x40003018, 0x40004038)`，本批建议的8字节目标为 `0x40003ffd`，确实跨界3+5且两侧保护字节在buffer内。这是旧产物的布局核对，作者实现后仍须以新ELF重新检查。
- BATCH06 的 70 个具体宿主场景、默认 6 页回收和真实 QEMU 结果已完成归档，证据仍见上一批。本次准备任务不重复运行，也不把历史探针当作新功能测试。

## 文件与分工

| 文件 | 本批工作 | 负责 |
| --- | --- | --- |
| `include/mm/user_access.h` | 新 C 接口及长度、失败语义注释 | 作者 |
| `include/mm/user_access.hpp` | 新类型化写回接口 | 作者 |
| `kernel/mm/user_access.cpp` | 入口校验、逐页权限检查与复制 | 作者 |
| `include/task/timer.h`、`kernel/task/timer.c` | 公开一个原始时间读取入口，复用原 CSR 读取 | 作者 |
| `include/task/syscall.h`、`kernel/task/syscall.c` | GET_TIME 编号、私有处理函数和分发 | 作者 |
| `user/user_api.h`、`user/syscall.S` | 用户声明和薄汇编包装 | 作者 |
| `user/main.c` | 正常/跨页写回、只读目标拒绝及可观察输出 | 作者 |
| `test/test_user_access.cpp` 及必要的局部 fixture | 少量写回用例，保留原读取用例 | 协作者 |
| 本文件、索引和 `MEMORY.MD` | 集中记录审查、验证、理解收尾 | 协作者 |

既有 `user_access.cpp` 已参与目标和宿主构建，timer/syscall 已参与目标构建，通常不用改 Makefile/CMake 源列表。若另加测试文件，现有顶层 `.cpp` 自动发现仍需显式注册运行入口。不要把含 CSR 的 timer/sched/syscall 加入宿主算法库。

## 公开接口

### 复制层

在原 C 链接保护块中新增：

```c
int user_copy_to(
    const Sv39PageTable* root,
    uintptr_t destination_address,
    const void* source,
    size_t length
);
```

C++ 头的 `tinyos` 命名空间新增：

```cpp
int copy_writable_user(
    const Sv39PageTable& root,
    VirtAddr destination,
    std::span<const unsigned char> source
);
```

`source` 是内核可读字节；`destination_address` 是用户 VA 数值，不能直接作为内核可写指针。成功返回 `SV39_OK`；参数、地址范围、查询或权限失败统一返回 `SV39_ERR_INVALID_ARGUMENT`，不透传各种 query 错误码。const root 约束不改页表，并不把它映射的内容页变成 const。

### 原始时间与 syscall

`timer.h` 显式包含 `<stdint.h>`，在 C 链接保护中新增：

```c
uint64_t timer_read_time(void);
```

把现有私有 `read_time()` 改成此窄入口，或保留私有实现由它转发，二选一即可；`timer_set_next()` 继续复用同一个读取来源，不复制一份汇编，不改变下一次中断的设置方式。

在 `syscall.h` 的汇编可见区域新增 `SYSCALL_GET_TIME = 6`（沿用现有宏写法）。ABI 固定如下：

| 项目 | 约定 |
| --- | --- |
| `a7` | `SYSCALL_GET_TIME` |
| 输入 `a0` | 可写用户目标 VA，容量由用户保证至少 8 字节 |
| 其他输入 | 无长度参数，忽略 `a1` 等剩余参数 |
| 输出缓冲区 | 一个 `uint64_t` 原始 `time` 计数的 8 字节表示；当前 RV64 小端构建，低字节在前 |
| 返回 `a0` | 成功 0，失败 `(u64)-1`，用户 C 看到 `long -1` |

`user_api.h` 增加 `long user_get_time(void* destination);`。使用 `void*` 允许普通 `uint64_t` 对象，也允许未对齐的字节缓冲区；syscall 不能把用户目标转成 `uint64_t*` 后直接存储。

这个接口返回原始计数，不是日期、Unix 时间、秒/毫秒，也不是 `sleep_ticks`。`sleep(2)` 仍按调度器收到的 timer 中断次数睡眠，本批只要求两次采样跨越睡眠后后值更大，不要求精确差值。不公开/硬编码 10 MHz 到用户 ABI，不解析 DTB 频率。

## 复制层契约与不变量

### 输入与范围

1. C 入口先处理 `length == 0`：直接成功，允许 NULL root/source 和任意目标数值，不构造引用或非空 span。C++ 核心的空 source 同样成功；引用本身仍须指向有效对象。
2. 非零长度先拒绝 NULL root/source，再构造引用/span。内核调用者保证 source 有效、足长，且不与目标物理字节区重叠；本批不实现 memmove 或任意内核指针探测。
3. 在任何复制前检查目标起点、最后字节和整个连续范围：使用 checked `destination + (length - 1)`，拒绝整数溢出、非 canonical 地址及跨 Sv39 canonical 空洞。起止都 canonical 仍可能分属两半，须确认二者位 38 相同；只调用 `try_add` 不足以证明这一点。
4. 不要求目标对齐，也不额外禁止 VA 0；非零复制能否访问由实际映射与权限决定。使用最后字节检查，保留 `UINTPTR_MAX` 单字节这种端点策略；不要用要求 one-past 可表示的 `PageRange` 工厂替代它。

### 按页翻译与写入

每轮分别计算当前 VA、查询该页、检查权限，再复制这一页以内的字节：

```text
current = destination + copied
query(current) -> physical_address, flags
required = V | R | W | U
chunk = min(source.size - copied, PAGE_SIZE - current.page_offset)
写 source[copied, copied + chunk) 到本轮 physical_address
copied += chunk
```

这是执行关系提示，循环与错误处理由作者实现。特别注意：

- 每页均须 query 成功并满足 `V|R|W|U`；不能只验证第一页。`W=1/R=0` 是非法组合，X 不能代替 W，内核恒等映射的 RW 页缺 U 也必须拒绝。
- query 返回的是带页内偏移的 PA，不能再次加 VA 偏移。下一页重新 query，不能把首 PA 视为连续缓冲区。
- 经 `PhysAddr` 获取当前恒等映射可访问的内核指针，逐字节写入；软件检查用户权限后，实际存储通过内核的物理页别名进行，硬件不会替这个别名重新检查用户叶 PTE 的 W 位。
- A/D 不作为此软件复制的额外门槛，也不由本函数设置。它不是对一次硬件用户 store 的完整模拟；默认 loader 已给可写页设置 A/D。
- 函数只改成功复制的目标内容字节；内核 source、其他内容、页表、UserMemory、PMM 页数和资源事件保持。没有 alloc/free/map/unmap/reclaim、TLB flush、`fence.i` 或 SUM 开关。
- 当前保证来自单 hart、复制期间没有其他执行路径更改这些映射/回收这些 PA 的运行方式。内核维护的 PTE 必须指向可访问 RAM；不把任意伪造 PA 当作可安全解引用的输入。

### 失败可能留下合法前缀

沿用现有 `copy_from` 的逐页语义：范围算术检查失败时零写入；第一目标页拒绝时零写入；后续目标页拒绝时，前面合法页内的前缀可能已写入，失败页及之后不写。返回值不提供已复制长度，调用者失败后应把整个结果视为不可用。

GET_TIME 同样遵守此约定：跨页输出若第二页不可写，可能先写入第一页的部分计数字节，再返回 -1。不得把返回 -1 理解为目标完全没变，也不承诺事务性写入。本批不增加两遍预检、回滚缓冲区或通用事务框架。原 WRITE 的“复制全成功后才输出 UART”策略继续保持。

## 系统调用路径提示

`user_get_time` 的薄包装沿用现有汇编结构：设置 a7、ecall、ret，目标由 a0 原样传入。内核私有 `syscall_get_time(uintptr_t destination_address)` 建议按以下关系组织：

1. 取得 `sched_current_user_root()`，没有当前用户 root 返回 -1。
2. 读取一次 `timer_read_time()` 到完整初始化的局部 `uint64_t`。
3. 把该对象的恰好 `sizeof(uint64_t)` 字节交给 `user_copy_to`，用复制返回值决定 0/-1。不要输出局部指针本身、邻近栈字节或未初始化的填充结构。

分发先取出调用者保存的目标地址，再覆盖 `frame->a0`；沿用 `syscall_handle` 开头统一的 `sepc += 4`，不再次递增、不触发调度，也不改 trap_frame/汇编偏移。为新增 case 使用局部花括号，避免跨 case 的局部声明冲突。

这一条执行链应能在代码中追踪：

```text
U: user_get_time -> ecall
S: trap_handler -> syscall_handle -> syscall_get_time
   -> timer_read_time -> user_copy_to -> 逐页 query/检查/写 PA
U: sret 后取返回值，再读取自己的输出内存
```

## 用户演示与可观察结果

2026-10-30 根据作者对重复buffer检查的反馈，收简作者演示；本节替代原先要求作者实现跨页采样、保护字节和代码页拒绝的安排。这些必要验证转由协作者在临时副本完成。

保留BATCH06已有的初值/BSS和睡眠后buffer内容检查，以及yield/R/sleep/X、Hello/Data OK和exit。BATCH07不新增整数组扫描，也不要求作者再改buffer：

1. 在原 `sleep(2)` 之前，将第一次GET_TIME写入栈上 `uint64_t`，检查返回0。
2. 睡眠后将第二次GET_TIME写入另一个栈上 `uint64_t`，检查返回0，且后值大于前值。
3. 通过后用原 `user_write` 输出一次 `Time OK\n`，检查返回长度，保持正常exit；失败走已有fail路径。

默认新增的只是栈上小对象和RX常量，已有数据/BSS仍两页；协作者用新ELF实际核对，不能拿旧长度作硬断言。若编译布局变了，优先调整演示局部存储，保持代码一页、数据两页的本批范围。

协作者的最小临时QEMU探针仍验证真实跨页和写权限：在原buffer模式核对后，取其起址之后的第一个4KiB边界，将8字节输出放在边界前3字节形成 **3+5**，核对两侧各一字节保护值；逐字节还原小端值，不解引用未对齐的 `uint64_t*`。检查目标和保护字节均在buffer内，观察保留volatile读取。另以代码页 `0x40000000` 为目标，确认返回-1、代码不变且程序继续。此探针及结果由协作者承担，不要求作者编写或保留在正式main中。

## 分级验收与分工

P0 是本批完成所需、由协作者承担的验证；作者只需实现功能并跑通常的构建/演示，不要求手工编写以下测试表。每组可合并少量表项，不追求新增场景数。P1/P2 均可跳过，不阻塞下一批。

| 等级 | 场景与具体预期 | 学习点 / 新代码量与负责 |
| --- | --- | --- |
| P0 | `make -B -j4` 无警告；`make test` 原 70 个具体场景语义保留，新写回场景通过；C ABI 导出 `user_copy_to`、`timer_read_time`，最终内核无未解析/新增构造运行时依赖 | 接口接线；协作者例行执行，不让作者重写旧矩阵 |
| P0 | 宿主：非零页内偏移写入包含 0/0xff 的字节；跨页目标从页尾前3字节写8字节，目标 PA 中间有隔离页。精确3+5字节正确，两侧与隔离页、source/页表/PMM计数/事件不变 | VA→PA、偏移与复制方向；协作者给原 access 测试增加少量用例 |
| P0 | 宿主：第一页缺 W 的 U/R/X 映射拒绝且目标不变；RW但缺 U 同样拒绝；第二页只读或未映射（二选一即可）时返回 INVALID_ARGUMENT，只允许前三字节前缀改变，后五字节和页表/源/页数不变 | 软件写权限与前缀语义；复用同一跨页 fixture，不扩故障矩阵 |
| P0 | 宿主入口组：零长度且 root/source=NULL 成功；非零 NULL root/source 拒绝；一条地址加法溢出、一条末字节非 canonical、一条两端合法但跨 canonical 空洞均在写入前拒绝，哨兵不变 | 完整范围检查；协作者集中少量参数表 |
| P0 | 真实QEMU：正式演示睡眠前后两次栈输出返回0、后值更大；`Time OK`/Hello/Data OK/exit各一次，无fail/fault/halt，退出后至少两次S timer。协作者另在最小临时副本验证buffer内3+5跨页输出、保护字节及代码目标返回-1 | 真实CSR、trap与U/S复制闭环；作者只写两次普通采样，跨页/拒绝探针与复验由协作者承担 |
| P0 | 本次真实运行的临时观测确认仍4内容页+2中间表，复制阶段0次分配/释放/flush；exit回收6页、描述全空、用户映射撤销、root/satp及内核映射保留 | 改内容与改映射的区别；协作者最小临时探针，不要求新产品计数器 |
| P1 | 目标 `UINTPTR_MAX` 的单字节合法映射写回；A/D清零但V/R/W/U齐全的宿主映射仍能复制且PTE不变 | 端点策略、软件访问边界；约几十行宿主练习，可只讨论 |
| P1 | 临时QEMU让8字节输出的第二页不可写，确认-1、合法前缀及后续内核仍运行 | 失败输出的使用约定；需临时页表探针，协作者可协助，非作者必做 |
| P2 | 更多跨页长度、拒绝组合或适配旧WRITE/Q1/Q2脚本 | 额外回归维护；可能数十至上百行，按需要安排 |

检查复制时的零资源事件要在调用前后取差；宿主 fixture 建表和真实退出本来就有资源事件，不能计入复制阶段。PA隔离的精确内容验证放宿主，真实 QEMU 验证当前实际映射和CSR/trap，不宣称默认PMM必然产生分隔PA。

常用命令（仓库根目录）：

```sh
make -j4
riscv64-linux-gnu-readelf -lW user/build/demo.elf
timeout 8s make qemu
```

整批完成后的协作者回归：

```sh
make -B -j4
make test CTEST='ctest --verbose'
riscv64-linux-gnu-nm -u kernel.elf
riscv64-linux-gnu-nm -g --defined-only kernel.elf
```

QEMU 的 timeout 124 表示观察限时结束，需结合完整输出判断；不代表用户失败，也不是客体主动关机。必要的6页计数与零复制资源事件由协作者在临时源码副本观测，不让作者添加常驻诊断框架。原 `make test-qemu` 只运行旧回滚脚本，不代表本批验收；不自动适配并重跑完整历史矩阵。禁止为本批例行验证执行 `make clean`，保留工作区改动与暂存状态。

## 建议实现顺序与收尾

先完成 access 两头/实现，再接 timer窄入口和syscall，最后补用户包装与演示。每一部分围绕上述接口推进，不先搭新类或泛化双向模板；原读取循环重复少量代码可以接受。作者整批完成后，协作者一次列全问题，再修正并复验。

功能与P0通过后，先梳理“输入地址 → 翻译与软件权限 → 物理写入 → 返回寄存器 → 用户读取”，再用约三题收尾，原答/集中审查/修正版继续归档本文件：

1. 为什么 root 仍可为 const，函数却能改变用户内容？既然内核能经PA别名写，为什么还必须检查用户PTE的W和U？
2. 8字节目标位于页界前3字节，第二页只读：哪些字节允许变化、返回什么，用户应如何处理结果？与WRITE的整次UART输出策略有什么区别？
3. GET_TIME改变了哪些状态，哪些资源应保持？原始time计数、sleep(2)的2、输出的8字节分别表示什么？

本批功能与P0已通过，三题原答/审查/修正版已在上方归档；不要求重答BATCH06，也不再增加本批程序验证作业。

## 准备记录与参考

2026-10-30：完整阅读约定/记忆/BATCH06/路线图并核对真实源码与git status；内部子代理完成方向审查和整份任务只读复核，无阻塞问题，重点核对const root、PA偏移、权限、前缀语义及范围风险。本轮只交付任务、索引和简短记忆，无产品/测试修改，也未运行新功能构建或QEMU。本地文档链接/空白检查通过，88个源/配置文件哈希及Git暂存区记录保持；结果见 `.cache/batch07-task-preparation/`。

架构知识仅按本批问题查阅：[RISC-V Supervisor规范](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)说明页权限与SUM；[Zicntr计数器规范](https://docs.riscv.org/reference/isa/v20260120/unpriv/counters.html)说明time为实时时间计数、计数频率由执行环境决定。本批采用项目自己的8字节原始计数ABI，不声称兼容其他系统的GET_TIME。rCore仅作具体原理补充，不自动进入第五章。
