# 用户数据页：初值、BSS 与退出回收

状态：2026-10-10 作者决定收住本批测试范围，按现有验证继续功能学习，不再追加测试作业。四项数据页 QEMU 场景已通过；同期已写出的宿主 D1-D7 全部保留，现有宿主测试共 61 个具体场景通过，CTest 1/1 通过，无编译警告。旧 Q1/Q2 三页适配及额外镜像/链接边界未完成，作为已知覆盖限制记录，不冒充已通过。下文未实现的扩展清单留作参考，不阻塞继续学习。

## 2026-10-08：Python 场景字典练习入口

已提供 `test/verify_user_data.py` 的执行框架；初建时仅完成 `baseline`，其余三个 `user_source` 留空供作者填写。2026-10-09 作者已填写并运行 --all，四项均 PASS；空草稿拒绝机制继续保留。2026-10-10 其余扩展验证按页首范围调整冻结。

框架为每个场景新建临时副本，复制实际内核/用户源码，按字典替换副本的 `user/main.c`，构建并观察 QEMU 八秒。临时 linker wrapper 检查真实 EXIT/fault 调用的 release：准备的三页 PA/权限正确、栈顶空洞未映射、归还五页、七字段清空、三处映射和空用户页表分支消失、根页表没有被 free、satp 保持；结束后 A 和至少两次 S-mode tick 继续。正常场景必须 EXIT，fault 场景必须出现一次预期 fault。检测到根页表 free 尝试时 wrapper 拒绝该尝试并报告失败。

填字典时只需要关心这些字段：

| 字段 | 写什么 |
| --- | --- |
| `name` | 命令行选择的场景名 |
| `user_source` | 用 `r'''...'''` 放完整用户 C 程序；`None` 表示保留真实用户程序，空字符串表示尚未完成 |
| `image_kind` | `data` 检查 bin 为 0x3000..0x4000 字节；`short` 检查 1..4096 字节 |
| `expected_output` | 应恰好出现一次的 bytes 元组，如 `(b"BSS OK\n",)`；一项也保留尾逗号 |
| `expected_fault` | 正常填 `None`；fault 填寄存器整数值字典，至少包含 `scause` |
| `forbidden_output` | 绝不能出现的 bytes 元组，通常放故障指令之后的标记 |

`expected_output` 检查指定标记，不把整个 UART 日志当成相等比较，因为内核 A/timer 日志也会输出。`image_kind` 检查长度，不独立解析 ELF；初值、BSS 和睡眠保持需由用户 C 的检查分支实际验证。

一次补齐以下三个场景；每个程序包含 `user_api.h`，需要地址整数转换时包含 `stdint.h`。用户代码不能直接调用 PMM/UART。

1. `pure_bss`：只新增未初始化的 `static volatile unsigned char buffer[32]`，不要新增可写非零初值。检查全部为零，写入 `i+1`，检查 `user_sleep(2)==0`，唤醒后验证修改保持，再 WRITE `BSS OK\n`，检查长度后正常返回。任意失败输出 `[write] failed\n` 并调用 `user_exit()`。
2. `data_execute`：把 `0x40003000` 转成 `void (*)(void)` 并调用；后面写 `UNREACHABLE\n`。预期 cause 12、sepc/stval 都为数据 VA，不应抵达后面的 WRITE。数据整页清零，bin 应仍是短镜像。
3. `code_write`：通过 `volatile unsigned char*` 向 `0x40000000` 写一个字节；后面写 `UNREACHABLE\n`。预期 cause 15、stval 为代码 VA。sepc 是 store 指令位置，暂不写死。

数据页取指表达式的形状：

```c
void (*entry)(void) = (void (*)(void))(uintptr_t)0x40003000;
entry();
```

代码页写入表达式的形状：

```c
volatile unsigned char* code =
    (volatile unsigned char*)(uintptr_t)0x40000000;
*code = 1;
```

测试框架不自动修补功能源码。2026-10-09 已静态核对：`kernel/main.c` 的两次段装载及 `fence.i` 均位于首次映射之前，随后检查三次 flush，再交付输出描述。QEMU 场景通过不能替代这一源码顺序要求。

```sh
python3 -u test/verify_user_data.py --list
python3 -u test/verify_user_data.py baseline
python3 -u test/verify_user_data.py pure_bss
python3 -u test/verify_user_data.py data_execute code_write
python3 -u test/verify_user_data.py --all
```

无参数默认只运行 baseline；`--all` 要求四个场景都已填写。未知名称或尚未填写的场景在任何构建前报错。每个场景从干净的源码副本构建，上一例不会污染下一例；框架的默认构建日志只在失败时显示。

## 目标与分工

可见结果：原 yield/R/sleep/X/Hello 演示继续成功，用户程序额外输出一次 `Data OK\n`，随后 EXIT；全局变量初值正确，BSS 初始为零，修改后的值经过 sleep 后仍保留。

原分工为作者完成链接脚本、用户 C 检查、内核准备/回滚、UserMemory 回收和宿主 D1-D7，协作者补集成验证。2026-10-10 作者收缩范围，当前按页首状态保留已完成部分，其余不再自动下发作业。

范围仍是一页代码/只读数据、一页栈、一页数据/BSS，共享根页表；不解析 ELF，不增加 libc、动态堆或语言迁移。构建仍输出 flat demo.bin 并用现有 incbin 嵌入。

## 地址与权限

| 用途 | 用户 VA | 页权限（另有 V） |
| --- | --- | --- |
| 代码和只读常量 | 0x40000000 | U/R/X/A |
| 栈 | 0x40001000 | U/R/W/A/D |
| 空洞，栈顶位置 | 0x40002000 | 不映射 |
| .data + .bss | 0x40003000 | U/R/W/A/D，不能有 X |

栈顶仍为 0x40002000，不移动原来的 WRITE 坏指针。数据和栈使用相同权限，但拥有不同 PMM 页。

kernel/main.c 增加 DEMO_USER_DATA_ADDRESS=0x40003000。镜像数据偏移可以定义为 DEMO_USER_DATA_ADDRESS - DEMO_USER_CODE_ADDRESS，即 0x3000。

## 1. user/user.ld：分开两个装载段

这份脚本骨架已在临时工程编译验证；直接用它替换旧的一页限制脚本，再阅读每个符号的含义：

```ld
OUTPUT_ARCH(riscv)
ENTRY(_start)
PHDRS {
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0x40000000;
    __user_code_start = .;
    .text : {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } :code
    .rodata : {
        *(.rodata .rodata.* .srodata .srodata.*)
    } :code
    __user_code_end = .;

    . = 0x40003000;
    __user_data_start = .;
    .data : {
        *(.data .data.* .sdata .sdata.*)
    } :data
    __user_data_end = .;
    .bss (NOLOAD) : {
        __user_bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        __user_bss_end = .;
    } :data

    .unsupported : {
        *(.got .got.* .igot .igot.* .tdata .tdata.* .tbss .tbss.*)
        *(.init_array .init_array.* .fini_array .fini_array.*)
    } :data
    /DISCARD/ : {
        *(.eh_frame .eh_frame.* .comment .note.*)
    }

    ASSERT(_start == 0x40000000, "user entry must be at code start")
    ASSERT(__user_code_end - __user_code_start <= 4096, "user code exceeds one page")
    ASSERT(__user_bss_end - __user_data_start <= 4096, "user data and BSS exceed one page")
    ASSERT(SIZEOF(.unsupported) == 0, "unsupported user runtime sections")
}
```

FLAGS(5) 为 R/X，FLAGS(6) 为 R/W；真正运行时 PTE 权限仍由内核设置。.bss 的 NOLOAD 不需要在 flat bin 中保存整段零字节。禁用 small-data/relax 的现有 USER_CFLAGS 保留，用户 gp 仍不设置。

镜像若有 .data，objcopy 会在代码和 .data 之间填充文件空洞，数据首字节位于文件偏移 0x3000。这些填充字节只存在于镜像，不代表要在用户地址空间把空洞映射出来。

若只有代码或纯 BSS，demo.bin 仍可能小于一页。正常布局不需要新增 Python 元数据生成器；本批固定把数据运行区的整页剩余部分清零，BSS 及对齐空隙因此都为零。之后 ELF 加载器再使用实际的段内存大小。

## 2. UserMemory 增加可选数据页

include/user_memory.h 在原字段后追加：

```c
void* data_page;
uintptr_t data_address;
```

root 是借用的，三个 PMM 页归任务拥有。task_create_user 现有的按值复制描述和清空调用者会自动带上新字段，检查实际执行路径；不要只手动复制旧五个字段。

数据页是可选的，以便现有 H1-H8 和复制 fixture 保持两页构造。约定：

- 两个数据字段全空表示未启用数据页，release 不查询/撤销/flush VA=0。
- data_page 非空时，data_address 必须非零、canonical、页对齐，并与代码/栈 VA 不同；三个拥有的非空物理页也应互不相同。不符合要求时，在任何释放操作前拒绝。
- 部分释放后允许 data_page=NULL、data_address 保留；它记录仍需要完成的清理路径。
- release 判断完全空描述、缺 root 和最终清空时必须覆盖全部七个字段。

正常 prepare 始终创建数据页，即使 .data/.bss 都为空。普通内核任务仍使用全零 UserMemory。

## 3. prepare_demo_user_memory：切分 flat 镜像

kernel/main.c 包含 user_image.h。先校验 image_end>image_start，算出 image_size。只接受：

- 1..4096 字节：代码/旧汇编探针，数据没有文件初值。
- 0x3000..0x4000 字节：代码占前一页，数据初值从文件偏移 0x3000 开始。

4097..12287、0、超过 16384 字节都拒绝，而且在 PMM 分配之前拒绝。合法镜像如何产生由上面的链接断言约束；内核不假装解析其中的 ELF 信息。

通过范围检查后计算：

```c
size_t code_file_size = image_size;
size_t data_file_offset = 0;
size_t data_file_size = 0;

if (image_size > SV39_PAGE_SIZE) {
    code_file_size = SV39_PAGE_SIZE;
    data_file_offset = 0x3000; /* 实现时使用你的偏移常量。 */
    data_file_size = image_size - data_file_offset;
}
```

小镜像时 data_file_offset=0 很关键：file_size 虽为零，也不能把 0x3000 作为超出镜像范围的偏移传给已经验收的函数。

固定顺序申请 code_page、stack_page、data_page；之后建立映射时申请 L1/L0。代码/栈/数据 VA 共享相同的 L0/L1，因此正常 prepare 总共消耗五页。

使用段装载函数处理两个缓冲区：

```c
int load_result = user_image_load_segment(
    code_page, SV39_PAGE_SIZE,
    user_image_start, image_size, 0, code_file_size, SV39_PAGE_SIZE
);
/* TODO: 非 OK 时设置 result=SV39_ERR_INVALID_ARGUMENT，goto clean_up。 */

load_result = user_image_load_segment(
    data_page, SV39_PAGE_SIZE,
    user_image_start, image_size,
    data_file_offset, data_file_size, SV39_PAGE_SIZE
);
/* TODO: 同样检查结果。 */
```

这两个 destination 是内核可访问的物理页指针，不是用户 VA。保留复制完成后的 fence.i。随后按 code -> stack -> data 映射，按同一顺序执行三次 flush；全部成功后再写 memory_out 的七个字段。

失败时 memory_out 完全保持调用前的值。给数据页也加独立的 mapping_attempted/mapped 标记，依照现有 clean_up 的阶段扩展：

1. 仅撤销本次成功建立的映射，不能撤销已有冲突映射。
2. 对尝试过映射的用户 VA 完成 flush，包含映射失败但可能留下父页表的路径。
3. 三个已申请 PMM 页各自释放一次。
4. 回收尝试路径中的空 L0/L1；非空表必须保留。
5. 清理成功返回原错误码，清理失败报告对应错误，不能隐藏。

保持原栈分配语句 `stack_page = pmm_alloc_page();` 的唯一位置，WRITE 的分隔 PA 探针目前以它为替换锚点。

## 4. user_memory_release：第三个页也走完整流程

可沿现有函数添加数据分支，也可以用一个很小的私有辅助函数减少重复；本批不需要通用资源框架。

顺序仍然是：

```text
校验描述及所有启用页的 PA/U
    -> 撤销所有启用叶映射
    -> flush 所有启用 VA
    -> free code、stack、data
    -> reclaim 空 L0/L1
    -> 清空描述
```

查询数据页发现 PA 不匹配、缺 U 或其他查询错误时，必须在 code/stack 被撤销之前返回。NOT_MAPPED 可继续清理，保持此前手动 unmap 与部分释放重试语义。

任意 flush 失败都不能开始 free。每个 free 成功后立即清空对应 page 指针；失败保留 root/VA 和尚未释放的页，使重试可完成。根页表不能 free。

有数据页的正常 release：3 次 unmap、3 次 flush、5 次 free（3 个内容页 + L0/L1）。无数据页时保留旧的 2/2/4，不要改写 H1-H8 的预期数值来掩盖行为变化。

## 5. 用户 C 中观察结果

在 user/main.c 原有基线上增加：

```c
static volatile unsigned long counter = 7;
static volatile unsigned char buffer[32];
static char data_message[] = "data OK\n";
```

volatile 用于让演示确实从内存读写；本批不把它当作同步机制。保留原 Hello、坏指针和所有 syscall 返回值检查。

作者完成这些检查：

1. main 开始时 counter==7，buffer 的全部 32 字节均为零。
2. 将 counter 改成 8，buffer[i] 写成 i+1；把 data_message[0] 改成 'D'。
3. 执行原 yield/R/sleep(2)/X；恢复后再逐项确认 counter、buffer 的修改值仍然存在。
4. 输出原 Hello；再 user_write(data_message,sizeof(data_message)-1)，检查返回长度，并观察 `Data OK\n`。
5. 任意不符合预期时走原 fail()，打印 `[write] failed` 并退出；不要只 return 1，因为当前 EXIT 不记录退出码。

用户源码不包含 pmm.h/vm.h，也不直接调用 UART。数据页没有 X，栈页仍没有 X，代码页没有 W。

## 6. 宿主测试参考：保留已完成的 D1-D7，不再追加作业

test/host_support.hpp/.cpp：MemoryDescriptor 增加 data_page/data_address，capture_user_memory 实际复制七个字段，输出助手也显示新字段。UserMemoryState 增加 data_mapping；只在启用数据路径且 root 非空时查询它。用空数据字段时 data_mapping 保持默认的 NOT_MAPPED，不能随意查询 VA=0。

UserMemoryFixture 可增加 `explicit UserMemoryFixture(bool with_data = false)`：默认继续构造 code/stack；with_data 时再申请数据页、设 `data_address = code_address + 3 * PAGE_SIZE` 并映射 U/R/W/A/D。记录所拥有的页数或 with_data，让 expect_released 的页数和第三映射检查正确。Pool<8> 足够；不得同时嵌套两个 PMM fixture。

新增测试可以放 test/test_user_data.cpp/.hpp，并从 test_main.cpp 接入；复用现有快照、事件和失败注入助手。先完成 snapshot 的新增字段，再写用例，避免“检查了空描述但忘记数据字段”。

| 场景 | 要验证的事实 |
| --- | --- |
| D1 三页正常回收 | PA/权限准备正确，归还五页，七字段清空，三个映射消失；3/3/5，事件先全部 unmap、再全部 flush、最后 free，root 从未 free |
| D2 三页释放后重复调用 | 第二次返回 OK，完整快照及调用/事件数量不变 |
| D3 数据 PA 或 U 错误 | 两个独立场景，返回 INVALID_ARGUMENT，所有七字段/映射/PMM/事件保持不变；恢复正确映射后正常清理 |
| D4 数据页 free 失败及重试 | 第一次 code/stack 成功、data 失败，返回 -7；只归还两页，三个映射已消失，data 指针和所有 VA/root 保留。第一次调用增量 3/3/3；撤销注入重试为 3/3/3（data+L0+L1），再归还三页。code/stack 各 free 一次，data 两次，root 零次 |
| D5 第三次 flush 失败 | 三个映射消失，但三页指针与 PMM 页数都保留；3/3/0。撤销注入重试 3/3/5 并恢复五页；所有 free 都发生在重试 flush 成功后 |
| D6 保留非拥有映射 | 在 code+2*PAGE_SIZE 再映射一个外部页；release 仅归还三个内容页，父表/外部 PA/flags 不变，3/3/3；测试末尾自行释放外部页并 reclaim，恢复全部页数 |
| D7 数据描述参数 | root=NULL 且仅数据字段非空不能被当作空描述；data_page 非空/VA=0、非对齐/非canonical数据VA、与 code/stack VA 重叠、物理页重复都提前拒绝。观察操作前后无变化，修复描述后正常清理 |

D4/D5 的事件范围在额外清理前取 end。D3/D7 的故障准备、恢复、辅助页释放在 observation 范围外完成。测试失败注入最终恢复关闭，避免污染后例。

D3/D7 需要同时检查被改坏的描述和真实映射：捕获真实映射时使用保留下来的 `fixture.root` 及固定 code/stack/data VA，不能只查询已经被改坏的 `memory.root/data_address`。比较完整快照之外，再显式比较事件数量前后不变。D4/D5 操作返回后先关闭失败注入，再做断言，避免断言失败后污染下一例。

测试组织沿用现有助手：D3 的 PA/U 错误与 D7 的非法参数适合简单枚举场景表；D1/D2/D4/D5/D6 保留明确的 `observe` 执行过程。D4 第一次与重试的事件各 9 条；D5 第一次 6 条、重试 11 条。数据测试可直接加入 `test_user_memory.cpp` 复用 require_events；若独立为 `test_user_data.cpp/.hpp`，只需在 test_main.cpp 接入口，CMake 已自动发现顶层 .cpp。

H1-H8、user_copy_from 的 12 个场景、USER_SEGMENT 的 16 个场景必须保持通过。

## 7. 集成扩展参考：未完成项冻结

这些是本批已知验收项，作者实现完成后不会再临时追加新要求。

### 原 Q1/Q2 适配

verify_prepare_rollback.py 的描述比较增加数据字段：

- 分配失败由四个变成五个：code、stack、data、L1、L0。
- flush 失败由两个变成三个。
- 冲突地址由两个变成三个；每次确认其余两个用户映射均未留下、已有冲突 PA/flags 不变。
- 共 11 个故障场景，更新 Python 期待的汇总标记与注释。
- 失败输出七字段保持哨兵，所有新增路径没有泄漏；注入关闭后正常运行 Hello/Data OK/EXIT/timer。

verify_task_creation_rollback.py 同步 prepared/empty 比较与数据映射，四页计数更新为五页，UART 标记和 Python 期待一起更新；用户创建失败仍完整保留描述，caller 清理恢复五页，root/satp/恒等映射保持。

WRITE 的 12 个场景保留。baseline 增加 Data OK 的输出检查；生成的短汇编镜像仍走合法小镜像分支，代码/栈跨页案例不使用数据页，不改 0x40002000 坏指针。

### 数据页专项 QEMU

协作者补持久 test/verify_user_data.py，在临时源码副本构建并检查：

1. 正常 C 程序：初值/BSS/写入/睡眠保持/从可写数据页 WRITE，Hello 和 Data OK 各一次，无 fail/fault/halt。
2. 纯 BSS C 变体：无可写非零初值，bin 小于一页，检查零初值及修改保持，正常退出。
3. 数据页取指：跳到 0x40003000，得到 cause 12，sepc/stval 为该地址；探针后的标记不可出现。
4. 代码页写入：写 0x40000000，得到 cause 15，stval 为该地址。
5. 在上述正常退出与两种 fault 路径检查实际 user_memory_release：归还五页，三个映射消失、描述清空、根不被 free；随后至少两次 S-mode timer，A 继续运行。

fault 场景不用要求 syscall EXIT 日志，因为它们由 fault 终止。临时探针不留下生产调试打印。

### 构建和镜像边界

临时副本添加超一页代码/rodata、.data、.bss，分别被链接断言拒绝；GOT/TLS/构造段继续拒绝。临时非法长度镜像验证 prepare 在分配前拒绝 0、4097、12287、16385 字节且输出不变。

实际 ELF 的 code/data LOAD 分别 RX/RW，entry 为 0x40000000，无 gp 引用或未定义符号；初始化字节与 bin 的 0x3000 后内容一致。修改 user/main.c 后 bin、嵌入对象及 kernel.elf 自动重建。

## 作者本批验证顺序

```sh
cd /home/asus/tinyos
make -j4
riscv64-linux-gnu-readelf -lW user/build/demo.elf
riscv64-linux-gnu-nm -n user/build/demo.elf
make test CTEST='ctest --verbose'
make qemu
git diff --check
```

make qemu 看完后 Ctrl+A，再按 X。不需要 make clean。

完成上述作者部分后整批交审；协作者适配集成脚本后运行：

```sh
make test-qemu
python3 -u test/verify_syscall_write.py
python3 -u test/verify_user_data.py
```

上面的命令是原完整验收方案，当前已验证范围与后续安排以页首状态为准；后续再考虑解析 ELF 中的实际段布局。
