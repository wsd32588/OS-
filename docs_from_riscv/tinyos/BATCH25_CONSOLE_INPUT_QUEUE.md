# BATCH25：控制台原始输入队列

日期：2026-11-16。状态：功能、全部既定P0与原三题原答/集中澄清归档完成，本批收尾，见第10—12节。

## 1. 目标与本批边界

在现有 UART 与 KernelConsole 之间加入固定容量原始字节队列，分开“查看待交付数据”和“提交消费”。
以后用户输入拷出失败时可以不消费输入；本批先在真实 kdbg 路径上建立这层机制。

```text
task_a：uart_try_getchar → InputQueue.try_push
                               ↓
                   read_current(局部buffer，不消费)
                               ↓
                    KernelConsole.feed → 原cat/hex
                               ↓
                      commit_read(实际处理数量)
```

可观察结果：原五条命令和安静交互保持；内部确实经过入队、复制、处理、提交四步。
不新增任务、用户ABI、设备FD、标准流、阻塞等待、PLIC/IRQ、输入模式切换或全局输入服务。
不改用户程序、调度器和文件资源生命周期。已有定时抢占继续工作。

队列容量256字节，与 KernelConsole 的160字符行容量分别属于两层策略。
队列接受全部0..255；回车、退格、非法字符和超长行仍由 console 处理。
软件队列不等于硬件流控：执行 cat/hex 时任务A仍暂停轮询，不承诺快速粘贴绝不丢字节。

## 2. 文件与完整接口

作者新增：

```text
include/console/input_queue.hpp
kernel/console/input_queue.cpp
```

并局部修改 kernel/boot/private/demo_tasks.cpp、主 Makefile。
宿主测试及其构建接线由协作者负责，作者无需另写测试框架。

头文件使用独立 include guard，仅直接包含接口所需的 stddef.h、span：

```cpp
#ifndef TINYOS_CONSOLE_INPUT_QUEUE_HPP
#define TINYOS_CONSOLE_INPUT_QUEUE_HPP

#include <stddef.h>
#include <span>

namespace tinyos::console {
class InputQueue {
public:
    static constexpr size_t kCapacity = 256;

    constexpr InputQueue() noexcept = default;

    bool try_push(unsigned char byte) noexcept;
    size_t read_current(std::span<unsigned char> destination) const noexcept;
    int commit_read(size_t count) noexcept;

    size_t size() const noexcept;
    bool empty() const noexcept;
    bool full() const noexcept;

private:
    unsigned char bytes_[kCapacity]{};
    size_t head_{};
    size_t size_{};
};
}

#endif
```

这是拥有内嵌数组的普通值类型，没有外部资源或自指针，不必额外禁复制/移动。
任务A实际仅建立一个对象；本批不增加 reset/configure/initialized，也不返回指向内部数组的 span。
实现文件先包含自己的头。算法不依赖 UART、FS、Process、页表、PMM、堆、日志或原子操作。

## 3. 状态、不变量与三个核心操作

始终保持：

```text
0 <= head_ < kCapacity
0 <= size_ <= kCapacity
第i个未消费字节 = bytes_[(head_ + i) % kCapacity]，0 <= i < size_
下一写入下标   = (head_ + size_) % kCapacity
```

head_ 是队首下标，size_ 是有效字节数，不是末尾下标。
用 size_ 区分空和满，因此256个槽全部可用，不需要额外牺牲一个槽，也不需要维护第二份 tail_ 状态。
头不必在空时归零；后续写入继续按相同公式即可。这里只对固定小容量索引加法取模，无通用大整数长度问题。

### try_push

- size_ == kCapacity：返回 false，旧数据、head_、size_ 全部保持。
- 否则：算下一写入下标，写入 byte，增加 size_，返回 true。
- 满时拒绝新字节，不能覆盖最旧字节。0与255都是数据。

### read_current

复制数量为 min(size_, destination.size())。按逻辑队首顺序复制，跨数组末尾时回绕。
返回 size_t 数量；空队列或空目标返回0。只修改目标的前 count 字节，不写其余尾部。
这是 const 成员函数，不推进 head_ 或减少 size_。连续查看应得到相同前缀。
span 由调用方提供有效目标；本批是内核内部接口，不在这里检查用户页表或用户指针。

### commit_read

先检查 count > size_：返回-1且队列完全保持。合法时：

```cpp
head_ = (head_ + count) % kCapacity;
size_ -= count;
return 0;
```

count==0 也成功，包括空队列；不需要擦除已消费槽，size_ 已决定哪些字节有效。
getter 分别返回 size_、size_==0、size_==kCapacity。

## 4. “查看后提交”的调用约束

队列不记录上一次查看数量，不提供事务票据。commit_read 只能判断数量是否超过当前有效长度，
调用方仍须只提交已经成功交付/处理的实际数量，不能把剩余输入一起丢掉。

本批唯一生产者和消费者都在任务A；其他任务、timer/trap不访问这个栈上对象。
因此不需要为队列读写关闭中断、加锁、volatile或原子变量。被定时抢占不等于另一个执行者会访问它。
若未来生产改到中断或输入改由多个任务消费，必须另外设计串行化；单hart本身不能保证复合读改写安全。

查看到提交之间不能有另一个消费者改变队首。串行追加且不覆盖旧数据能够保留队首前缀，
但这不构成并发安全承诺。未来 stdin 服务接管时须明确长期拥有者和消费归属，不能让 kdbg 与用户同时抢队首。

未来用户交付的形状是 read_current → user_copy_to → 成功才 commit。
本批尚未实现这一用户路径，只验证原始队列具有保留输入的能力；目标用户buffer失败前缀是否被改动仍遵守已有拷贝契约。

## 5. task_a 接线：保持一个明确的循环

保留原 KernelConsole、UART writer、console.start() 和 delay。新增栈上 InputQueue 与32字节临时数组。
每轮仅尝试接收一个硬件字节，然后消费队列中最多32字节：

```text
若队列未满：
    uart_try_getchar 一次
    若返回 != -1：把转换后的 unsigned char 入队，并检查 try_push 成功

count = read_current(32字节临时数组)
若 count == 0：
    delay，进入下一轮

按顺序 console.feed(buffer[i])，共 count 次
检查 commit_read(count) 成功
进入下一轮
```

注意事项一次列齐：

- 先判断 !input.full() 再读UART。读RBR已消费硬件字节，不能读出之后才发现软件队列放不下并静默丢弃。
- 满了只跳过硬件读取，仍必须走后面的消费路径；不能满了就 continue，否则永远无法腾空间。
- 当前唯一生产者且同任务串行，预检未满后 push 失败属于内部错误，使用已有 PANIC_KERNEL。
- 只有 uart_try_getchar != -1 才转换字节；不把-1转换成255入队。
- feed返回void且当前没有可恢复失败。所有feed返回后才commit，意外commit失败应panic，不能再投递一次而重复执行命令。
- console拒绝非法字符或作废超长行，也算已处理这次输入，应正常消费；不能反复投递坏字符。
- 删除原先“直接从UART调用feed”的分支，确保没有绕过队列的第二条路径。
- 对象都随长期运行的task_a存活，不保存临时buffer的指针到队列或console。
- 每轮最多1次硬件接收意味着通常只积累1字节；回绕和满容量由宿主直接验证，不以批量粘贴作为验收要求。

demo_tasks.cpp 增加 console/input_queue.hpp；内部错误诊断需要 runtime/panic.h。
保留 boot_private.h 提供的task_a/task_b C链接，不重写函数ABI。
主 Makefile 的 OBJS 加 kernel/console/input_queue.o；既有cpp规则能够编译，不需要另设规则。
不改默认trace0或恢复普通启动的旧用户自测。

## 6. 实现顺序与验证命令

1. 建头文件，写getter与 try_push，先区分“容量、有效数量、写入下标”。
2. 写 const read_current，再写 commit_read；用上述不变量检查回绕。
3. 将对象和32字节临时数组接入task_a，检查两项内部返回值，补 Makefile 对象。
4. 作者整批完成后集中审查；协作者补宿主接线与下面P0，集中说明发现的问题。

```sh
make -j4
make test
make qemu
```

交互运行退出仍为 Ctrl+A 后按 X；限时可用 timeout 8s make qemu，超时退出本身不证明命令测试通过。
协作者另运行一次 make test-process，确认独立旧进程回归保持。
不需要 clean、新用户程序、500轮压力或完整历史QEMU脚本迁移。

## 7. 固定验收范围

以下P0由协作者承担，不变成作者额外测试作业。实现尚未完成，目前没有宣称通过。

| 优先级 | 场景与明确预期 | 验证方式/学习点 |
| --- | --- | --- |
| P0 | 目标构建、头自包含/重复包含、task_a/b C符号及原宿主通过 | RV64/native编译；依赖与接线保持 |
| P0 | 新对象空、空读取0、空commit0成功；0/255按序往返 | 实际队列宿主测试；字节与无数据不同 |
| P0 | 256次push成功，第257次false且原256字节保持 | 实际队列宿主测试；满拒绝不覆盖 |
| P0 | 先填充、消费前缀、再追加跨末尾，短目标及跨界读取FIFO正确 | 确定性宿主测试；head/size与回绕 |
| P0 | 两次peek不变；放弃交付不commit后仍得同前缀；partial commit只移除实际数量 | 宿主测试；准备和消费分离 |
| P0 | commit大于size返回-1且原状，合法重试成功；目标尾部和护栏保持 | 宿主测试；拒绝不破坏与复制范围 |
| P0 | 默认真实QEMU原help CRLF、cat about、hex worker前64、missing拒绝、help恢复 | UART经队列到console的现有交互保持，含一次真实入队/peek不变/commit减少观察 |
| P0 | 默认安静、定时抢占仍在、队列零PMM与文件池保持；独立原进程回归保持 | 只读观察与make test-process；不修改被测产品副本来掩盖错误 |
| P1 | 更换小批量消费长度，预测回绕后的队首 | 可只口头推演，无必写代码 |
| P2 | 更广的随机序列、IRQ生产、多消费者、批量粘贴压力 | 后续专题；本批不实现、不作为完成条件 |

正式QEMU内部状态观察由协作者在隔离测试安排，不给生产路径添加刷屏日志或公开调试getter。
不把队列宿主“交付放弃”的结果写成实际用户syscall拷出失败已经验证。

## 8. 功能与P0之后的三道理解题

先实现，完成后一次性梳理并作答；本节不追加后续作业。

1. read_current 已把字节复制到临时buffer，为什么还不能移走队首？如果交付失败或console已经处理成功，各应何时commit？
2. head_与size_如何区分空、满和回绕？满时拒绝新字节为什么能保留之前查看的前缀？
3. 为什么要在uart_try_getchar之前检查容量？未来用户stdin与kdbg为什么不能各自直接消费同一个队首？

## 9. 当前准备与限制

已核对现有 UART、task_a、KernelConsole 及 OpenFile 的读/提交语义；独立只读设计审查确认上述最小接口与单消费者边界。
准备证据：.cache/batch25-preparation-20261116/results.json，拟定头RV64/native重复包含及任务接线形状编译、task_a C符号通过。
最初准备检查的未使用constexpr对象触发Werror，仅给检查变量补maybe_unused后通过；没有修改产品来通过检查。
本轮交付任务与文档，核心实现由作者完成。构建准备的接口检查不等于队列算法运行或整批P0通过。
后续实现修正、验证证据与原三题问答继续集中记录在本文件。

## 10. 作者实现与执行路径梳理（2026-11-16）

作者完成两个队列文件、task_a接线与主Make对象。集中审查未发现必须修改的功能问题，协作者没有代改产品算法。
实现过程中原empty/full判断写反、push未增加size、peek从写入位置开始等草稿错误，均由作者按反馈修正；当前状态以本节和实际验收为准。

一次输入的完整路径：

1. task_a检查队列未满，再调用uart_try_getchar，只有返回非-1才转换成unsigned char入队。
2. try_push写逻辑尾部并增加size，不动head；满时返回false且原数据保持。
3. 不论是否收到新字节，task_a都查看队首，按min(size,32)复制到自己的buffer；read_current不改队列。
4. 顺序feed全部实际字节，console继续负责编辑、拒绝和命令；返回后commit实际数量，才推进head并减少size。
5. 无待处理字节才delay；定时中断仍可抢占。当前没有其他执行者访问队列，因此不新增锁或关中断操作。

队列拥有自己的字节副本，临时buffer是另一份副本；console同步使用feed传入的字节。
成功消费只更新有效区间，不必清零旧槽；空后head保持原下标不会暴露旧数据。
当前feed无可恢复失败，非法输入被console拒绝也算处理，仍提交；意外push/commit失败panic，不延迟后重放命令。

## 11. 全部既定P0验证（2026-11-16）

统一证据：.cache/batch25-final-20261116/summary.json，all_passed=true。
102个正式源文件在审查、真实控制台QEMU、独立进程回归与最终当前树一致；没有以产品副本修补算法来通过验证。

| 项目 | 实际结果与证据 |
| --- | --- |
| 构建/接口 | make -j4成功；真实队列头RV64/native自包含与重复包含、队列与任务RV64对象-Werror无诊断，task_a/task_b C符号保持；review/results.json |
| 宿主 | make test/CTest通过，原110+新增5=115个具体用例；test/test_input_queue.cpp链接真实算法，空与二进制、256满拒绝、跨界FIFO/部分提交、放弃交付重试/超量拒绝、目标护栏及串行追加全部通过；host.log |
| 真实控制台 | console/results.json全部27项通过；help CRLF只提交一次、cat about精确18字节、hex实际内嵌worker前64、missing拒绝、help恢复，输出保持 |
| 真实队列接线 | 55个实际UART字节均经push/peek/feed/commit各55次；首次size0→1，peek1→1且对象字节hash保持，feed完成后commit1→0；同一栈对象，各命令后排空，无观察器失败 |
| 安静与资源 | 运行期PMM分配/释放0、available31635、文件池0、satp保持；最后继续149次空poll、新增2次S-mode timer且串口安静，默认只A/trace0 |
| 独立旧进程回归 | make test-process全部27项通过；退出0/23/37/0、五镜像35次合法唯一free/21次运行alloc/净恢复14、reap3零PMM/最终池0；.cache/process-regression/20261116-174251-3/results.json |

QEMU采用隔离副本、成员链接观察器和只读RAM计数，经QMP命名管道读取，不向串口加观察日志；driver/queue/console/file_view/task函数体与产品一致。
最后由宿主停机，不是客体主动关机。队列满容量/回绕/放弃交付在宿主验证，不宣称真实用户stdin或用户拷出失败已接入。
没有追加高速粘贴、IRQ、多消费者、500轮压力或历史五页矩阵。include顺序、行尾空白等可选样式不作为功能门槛。

协作者仅新增测试及其CMake/测试入口接线和文档；无提交推送。功能与P0完成，接下来只按第8节原三题集中理解收尾。

## 12. 原三题原答与集中澄清（2026-11-16）

### 作者原答（原样保留）

> 可能存在多次读取，不能直接衔接commit_read提交，交付失败时停机，成功就把size_和head_回滚
> head_是起始位置，不是0就是存在数据，size_是当前多少字节未交付，如果== 256就是慢了，如果== 0就是空的，使用% kCapacity直接处理回绕，满了的时候可能是尚未提交，不能随便覆盖旧字节
> 要保证现在还能输入，还有位置存放，用户输入和kdbg是两个东西，kdbg和stdin会互相抢占uart

### 集中审查

- 多次查看、用size区分满空、取模回绕、满时保留旧输入，以及检查剩余容量的方向正确。
- 查看与提交分开的根本原因是复制进内核临时buffer不代表最终交付成功，不只是为了重复读取。
  未来user_copy_to失败通常返回错误且不commit，输入保留，不需要停机。
  当前task_a的feed无可恢复失败；push或commit在既定内部条件下意外失败才panic，二者不可混淆。
- 成功后是推进消费状态，不是回滚：head=(head+count)%capacity，size-=count。
  count表示本次实际交付/处理成功数量。失败时保持队列状态才是不提交。
- head只是物理数组内的逻辑队首下标，不表达是否为空。head非0可以空，head等于0也可以非空或满；
  是否有数据只看size。size==256表示满（原答“慢了”按容量问题澄清），size==0表示空。
  例如head=254、size=2，消费2后head=0/size=0；head=10、size=2，消费2后head=12/size=0。
- UART读取已从硬件消费字节，先判断容量是防止读出却无处保存。未来合理接线是唯一接收者读取UART并入队，
  kdbg/stdin按明确归属消费队列；即使它们不再直接抢UART，同时消费同一队首仍会抢走对方输入或破坏read→commit语义。
  一个消费者先peek，另一个消费者消费队首后，第一个消费者再commit可能丢弃其他字节；单纯加锁一次调用不自动解决整段事务/归属。

### 协作者整理的修正版（不是作者重答）

1. read_current只复制、不消费；最终交付成功才提交实际数量，失败不提交并按上层接口返回错误。
   当前console.feed完成即算处理成功；内部不变量失败的panic是另一类错误。成功消费推进head并减少size，不回滚。
2. head是队首下标，size才是有效字节数：0空、256满；逻辑下标取模回绕。
   满时拒绝新字节，保护尚未消费的旧输入；读过但未提交的前缀仍然有效。
3. 先确认容量再消费UART硬件字节。未来要明确输入归属：一个接收路径入队，活动消费者读取并提交，
   不允许kdbg与stdin同时抢同一个队首。

本轮仅归档原答与一次集中澄清，不要求重答、不追加题目或测试。第11节全部P0结果保持，无产品改动、重复测试或提交推送；本批收尾。
