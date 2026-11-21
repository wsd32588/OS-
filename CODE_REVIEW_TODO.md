# TinyOS 代码审查待定表

> 状态说明：本文所有条目默认 **待定**，只记录问题、原因和建议，不代表已经采纳或已经修改。
>
> 审查方式：静态阅读当前核心代码，按 P0（正确性/资源管理）、P1（健壮性/一致性）、P2（性能/数据结构）、P3（构建/测试/清理）排序。代码行号以当前工作区为准，后续重构后可能变化。

## 汇总表

| ID | 优先级 | 类型 | 位置 | 一句话问题 | 状态 |
|---|---|---|---|---|---|
| P0-1 | P0 | 正确性/资源 | `kernel/mm/user_address_space.cpp:139-252` | clone 失败统一回滚（local candidate + out_release） | 已完成 |
| P0-2 | P0 | 正确性/资源 | `kernel/mm/vm.cpp:157-180` | map 中间页表分配失败留下空表 | 待定 |
| P0-3 | P0 | 健壮性 | `kernel/task/trap.c:32-38` | U-mode 非页错误异常会 kernel halt | 待定 |
| P0-4 | P0 | 正确性/测试 | `kernel/mm/user_memory.cpp:96-153` | release 隐式重试状态机复杂 | 待定 |
| P0-5 | P0 | 契约/可维护性 | `kernel/task/user_task_exec.cpp:295-311` | exec 提交阶段不可回滚但未显式说明 | 待定 |
| P1-1 | P1 | 性能/清晰 | `kernel/mm/user_access.cpp:48-50,105-107` | 用户复制逐字节循环 | 待定 |
| P1-2 | P1 | 性能 | `kernel/task/user_task_exec.cpp:95-107` | 参数字符串每字节一次页表查询 | 待定 |
| P1-3 | P1 | 性能/API | `kernel/task/syscall.c:37-39` | WRITE 逐字节 `uart_putchar` | 待定 |
| P1-4 | P1 | 可读性 | `kernel/task/syscall.c:265-270` | 同一表达式读写 `frame->a0` | 待定 |
| P1-5 | P1 | 健壮性 | `kernel/task/sched.cpp:45-89` | change_page fatal 路径统一为 sched_fatal | 已完成 |
| P1-6 | P1 | 契约/测试 | `kernel/task/user_task_exit.cpp:44-64` | 先关文件再释放地址空间，失败语义需明确 | 待定 |
| P1-7 | P1 | 资源契约 | `kernel/process/builtin_program.cpp:108-147` | 失败返回部分初始化的地址空间 | 待定 |
| P1-8 | P1 | 健壮性 | `include/fs/open_file.hpp:42-48` | `OpenFile` 字段允许非法组合 | 待定 |
| P1-9 | P1 | 正确性/测试 | `kernel/fs/file_descriptor_table.cpp:140-201` | `dup2`/引用计数路径缺少系统测试 | 待定 |
| P1-10 | P1 | 行为一致性 | `kernel/fs/file_syscall.cpp:76-93` | 控制台读 `poll`/`WOULD_BLOCK` 语义分散 | 待定 |
| P2-1 | P2 | 数据结构/性能 | `kernel/mm/pmm.cpp:35-55,192-209` | 免检重复释放是 O(n)，没有 refcount | 待定 |
| P2-2 | P2 | 性能/清晰 | `kernel/mm/pmm.cpp:121-125,142-146` | 手写逐字清零循环 | 待定 |
| P2-3 | P2 | 数据结构/性能 | `include/task/private/task_table.hpp:8-32`、`kernel/task/task_table.cpp:25-38` | 固定 5 槽 + 线性 find_slot | 待定 |
| P2-4 | P2 | 数据结构 | `include/mm/user_address_space.h:9-13`、`include/mm/user_memory.h:18-28` | root 被两个结构重复持有 | 待定 |
| P2-5 | P2 | 数据结构 | `include/mm/user_memory.h:9-21` | 固定 16 页，页记录信息不足 | 待定 |
| P2-6 | P2 | 数据结构/性能 | `include/fs/file_descriptor_table.hpp:14,37`、`include/fs/open_file.hpp:51-70` | FD/OpenFile 固定容量 + 线性扫描 | 待定 |
| P2-7 | P2 | 数据结构/性能 | `include/console/input_queue.hpp:10-25`、`kernel/console/input_queue.cpp:23,42` | 环形缓冲用 `%` 且逐字节读 | 待定 |
| P2-8 | P2 | 数据结构/类型安全 | `include/mm/vm.h:78-100` | 页表项仍大量裸 `uint64_t` 位运算 | 待定 |
| P2-9 | P2 | 可维护性 | `kernel/mm/user_loader.cpp:132-290` | 好的回滚模式没有复用 | 待定 |
| P2-10 | P2 | 性能/扩展 | `include/mm/address.hpp:307-330` | `validate_no_overlap` 是 O(n²) | 待定 |
| P3-1 | P3 | 测试脚本 | `test/verify_syscall_write.py` | `"setup"` 行少逗号导致 `SyntaxError` | 待定 |
| P3-2 | P3 | 测试可靠性 | `test/verify_syscall_write.py` | 标记之间可能插入 timer/A 输出 | 待定 |
| P3-3 | P3 | 测试集成 | `Makefile:202-204` | `test-qemu` 未接入新 WRITE 脚本 | 待定 |
| P3-4 | P3 | 构建 | `Makefile:1-4` | C 用 GCC、C++ 用 Clang | 待定 |
| P3-5 | P3 | 构建质量 | `Makefile:13` | 默认 `-O0`，没有 sanitizer/优化档位 | 待定 |
| P3-6 | P3 | 清理 | `kernel/boot/main.cpp:6-19` 等 | 调试残留、重复声明/包含/return | 待定 |
| P3-7 | P3 | 清理 | 多处 | 拼写/命名问题 | 待定 |
| P3-8 | P3 | 测试可维护性 | `test/test_main.cpp:11-20` | 手工声明一长串测试入口 | 待定 |
| P3-9 | P3 | 覆盖率 | `test/` | 缺少 OOM/部分失败/fault 测试 | 待定 |
| P3-10 | P3 | 工程化 | `Makefile`、`.gitignore` | 源码树内构建产物、clean 靠通配符 | 待定 |

---

## P0：正确性与资源管理

### P0-1 `user_address_space_clone()` 失败统一回滚（已完成）

- **状态**：已完成（本轮）
- **改动量**：中
- **位置**：`kernel/mm/user_address_space.cpp:198-233`
- **原因**：函数已经创建了新的 root 页，并且可能已经分配/映射了若干用户页，但 `pmm_alloc_page()` 或 `map()` 失败时直接 `return`。调用方必须额外调用 `user_address_space_release(output)`，否则会泄漏 root 页、物理页和页表。目前 `fork` 路径会调用 `discard_fork_candidate()` 兜底，但这是隐式契约，新增调用方很容易漏掉。
- **核心问题代码**：

```cpp
const int result = user_address_space_create(source->kernel_root, output);
if (result != SV39_OK) {
    return result;
}

for (size_t i = 0; i < source->user_memory.page_count; ++i) {
    void* new_page = pmm_alloc_page();
    if (new_page == nullptr) {
        return SV39_ERR_NO_MEMORY;      // 已建 root / 已分配页未回收
    }
    ...
    const int map_result = memory.root->map(
        source_page.virtual_address,
        reinterpret_cast<uintptr_t>(new_page),
        page_flags[i] & SV39_PTE_LEAF_FLAG_MASK);

    if (map_result != SV39_OK) {
        return map_result;              // 同上，output 处于部分初始化状态
    }
}
```

- **建议**：在函数内部统一走 `clean_up`，失败时先 `user_address_space_release(output)`，再返回原错误；或者引入 `UserAddressSpaceTransaction`，析构自动回滚、成功时 `commit()`。这样调用方不需要知道部分初始化的细节。
- **实现记录（本轮）**：改为局部 `UserAddressSpace candidate{}` + 单一 `out_release` 标签；`pmm_alloc_page()` / `map()` 失败只 `goto out_release`；成功后才 `*output = candidate`；只有 cleanup 失败才 `PANIC_KERNEL`。同时修复了一个更严重的问题：原代码 `new_page == nullptr` 后 release 但没有 `return`，会继续解引用空指针。
- **修复后核心代码**：

```cpp
UserAddressSpace candidate{};
int result = user_address_space_create(source->kernel_root, &candidate);
if (result != SV39_OK) {
    return result;
}

for (...) {
    void* new_page = pmm_alloc_page();
    if (new_page == nullptr) {
        result = SV39_ERR_NO_MEMORY;
        goto out_release;
    }

    // 先记入 candidate，再复制/映射
    ...
    result = candidate.root->map(...);
    if (result != SV39_OK) {
        goto out_release;
    }
}

*output = candidate;
return SV39_OK;

out_release:
    if (user_address_space_release(&candidate) != SV39_OK) {
        PANIC_KERNEL("clone rollback failed");
    }
    return result;
```

- **当前验证**：单文件编译通过；`make test CTEST='ctest --verbose'` 3/3 通过。clone 专用的 OOM 注入测试仍待补（见 P3-9）。
- **验收**：在 `pmm_alloc_page()` 第 N 次调用失败、`map()` 第 N 次调用失败两种注入下，检查 `pmm_available_pages()` 恢复到初始值，且 `output` 全零。

### P0-2 `sv39_map_page()` 中间页表分配失败留下空表

- **状态**：待定
- **改动量**：中
- **位置**：`kernel/mm/vm.cpp:126-211`，重点 `157-180`
- **原因**：`sv39_walk_to_leaf(create=1)` 在遍历过程中逐级分配页表。如果 L1 分配成功、L0 分配失败，函数返回 `SV39_ERR_NO_MEMORY`，但 L1 已经挂在父页表上，且为空，物理页被永久占用。低内存或故障注入时尤其明显。
- **核心问题伪代码**：

```text
walk(L2):
    if L1 missing:
        L1 = pmm_alloc_page()
        attach L1 to root
walk(L1):
    if L0 missing:
        L0 = pmm_alloc_page()
        if L0 == NULL:
            return NO_MEMORY
            # 此时 L1 仍然 attached，且为空
```

对应代码：

```cpp
Sv39PageTable* next_table = (Sv39PageTable*)pmm_alloc_page();
if (next_table == NULL) {
    *error_out = SV39_ERR_NO_MEMORY;
    return NULL;
}
...
*entry = PageTableEntry::from_address(
    PhysAddr(next_table), SV39_PTE_V).val();
```

- **建议**：在 `map()` 失败路径调用 `reclaim_empty_tables(virtual_address)`；或者让 walk 记录“本次新建了哪些表”，失败时只回滚本次新建部分。最干净的是页表事务对象。
- **验收**：注入 L1/L0 分配失败，检查 `pmm_available_pages()` 与失败前一致，且目标 VA 路径上没有残留空表。

### P0-3 `trap_handler()` 只把 4 类 U-mode 异常当作用户故障

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/task/trap.c:32-38`
- **原因**：用户在 U-mode 触发其它异常时（指令地址未对齐、load/store 地址未对齐、断点、非法 CSR 访问等），`is_user_fault()` 返回 false，`trap_handler()` 会走到最后的 `kernel halt`。一个用户程序不应该让内核停机。
- **核心问题代码**：

```c
static int is_user_fault(unsigned long code) {
    return
        code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_INSTRUCTION_PAGE_FAULT ||
        code == EXCEPTION_LOAD_PAGE_FAULT ||
        code == EXCEPTION_STORE_PAGE_FAULT;
}
...
if (!is_interrupt &&
    (frame->sstatus & SSTATUS_SPP) == 0 &&
    is_user_fault(code)) {
        return sched_on_user_fault(frame);
    }

uart_puts("kernel halt.\n");
for (;;) { asm volatile("wfi"); }
```

- **建议**：对 `sstatus.SPP == 0` 且不是 user ecall 的所有同步异常，统一走 `sched_on_user_fault(frame)`；具体 `scause` 只用于日志。这样用户 fault containment 才完整。
- **验收**：QEMU 临时用户程序执行 `ebreak`、未对齐 load/store、非法 CSR 访问，都应打印 `[user fault]` 并退出任务，而不是 `kernel halt`。

### P0-4 `user_memory_release()` 的失败重试语义复杂

- **状态**：待定
- **改动量**：中
- **位置**：`kernel/mm/user_memory.cpp:96-153`
- **原因**：函数把 unmap、flush、free、reclaim 分成四段，每段失败都可能提前返回，下一轮又依赖 `NOT_MAPPED`、`physical_page == nullptr` 等隐式状态继续。当前 H4/H8 覆盖了主要路径，但加入数据页、共享页、COW 后会迅速变复杂。
- **核心问题代码**：

```cpp
for (const UserMemoryPage& page : active_pages) {
    const int result = sv39_unmap_page(root, page.virtual_address);
    if (result != SV39_OK && result != SV39_ERR_NOT_MAPPED) {
        return result;
    }
}

for (const UserMemoryPage& page : active_pages) {
    if (sv39_flush_page(page.virtual_address) != SV39_OK) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
}

for (UserMemoryPage& page : active_pages) {
    if (page.physical_page != nullptr) {
        if (pmm_free_page(page.physical_page) != 0) {
            return SV39_ERR_PAGE_FREE_FAILED;
        }
        page.physical_page = nullptr;
    }
}

for (const UserMemoryPage& page : active_pages) {
    const int result = root->reclaim_empty_tables(page.virtual_address);
    if (result != SV39_OK && result != SV39_ERR_NOT_MAPPED) {
        return result;
    }
}

*this = {};
```

- **建议**：把释放阶段显式化（例如 `ReleasedStage` 枚举或 `UserMemoryReleaseTransaction`），记录已完成的阶段；补充“free 失败后再次失败”“flush 失败后重试”“部分页已释放”的故障注入测试。
- **验收**：每种失败注入下，重试最终都能回到 descriptor 全零、页数恢复、root entry 清零。

### P0-5 `exec` 提交阶段不可回滚但未显式说明

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/task/user_task_exec.cpp:295-311`
- **原因**：`exec` 在新镜像准备完成后，先切回 kernel root、释放旧地址空间、adopt 新空间、再切新 root。这个顺序本身是对的，但中途任何一步失败都只能停机，因为旧空间已经释放或 satp 已经切走。代码里有 `exec_commit_fail()`，但没有把“不可回滚”写成明确契约。
- **核心问题代码**：

```cpp
result = sv39_activate(kernel_root);
if (result != SV39_OK) {
    exec_commit_fail("exec: activate kernel root failed\n");
}

result = current.proc->release_address_space();
if (result != SV39_OK) {
    exec_commit_fail("exec: release old space failed\n");
}

current.proc->adopt(candidate);
frame = fresh_frame;

result = sv39_activate(current.proc->root());
if (result != SV39_OK) {
    exec_commit_fail("exec: activate new root failed\n");
}
```

- **建议**：保留 fail-stop，但在函数注释和设计文档中明确“提交阶段失败只能停机”；同时把能提前做的校验全部放在提交之前，保证进入提交阶段后失败概率最低。
- **验收**：代码审查确认所有可能失败的准备步骤都在提交前完成；提交阶段只保留 activate/release/adopt。

---

## P1：健壮性与一致性

### P1-1 `user_access` 逐字节复制

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/mm/user_access.cpp:48-50`、`105-107`
- **原因**：逐字节循环语义清楚，但复制大块数据时性能一般，也不利于编译器稳定优化。`user_copy_from` 是 WRITE、open path、exec argv 等路径的公共底层，收益会被放大。
- **核心问题代码**：

```cpp
for (size_t i = 0; i < chunk; ++i) {
    destination[copied + i] = src[i];
}
...
for (size_t i = 0; i < chunk; ++i) {
    target[i] = source[writed + i];
}
```

- **建议**：每个页 chunk 使用 `std::copy`/`memcpy`。如果内核没有 `memcpy`，可在 `lib/string.c` 提供并复用。
- **验收**：宿主测试覆盖单页、跨页、NUL、非对齐 offset；QEMU 正常 WRITE/READ 回归。

### P1-2 `copy_exec_argument()` 每字节一次页表查询

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/task/user_task_exec.cpp:95-107`
- **原因**：每读一个参数字节都调用一次 `user_copy_from`，每次都要走三级页表查询。最多 4 个参数 × 64 字节 = 256 次查询，虽然能工作，但没有必要。
- **核心问题代码**：

```cpp
for (size_t offset = 0; offset < sizeof(argument.bytes); ++offset) {
    VirtAddr byte_address;
    if (!start.try_add(offset, byte_address) ||
        user_copy_from(old_root, &argument.bytes[offset],
                       byte_address.val(), 1) != SV39_OK) {
        return false;
    }

    if (argument.bytes[offset] == '\0') {
        argument.length = offset + 1;
        return true;
    }
}
```

- **建议**：按页 chunk 拷贝到内核缓冲，再在内核缓冲里扫描 NUL；每次最多拷贝 `min(剩余, 页内剩余)` 字节。
- **验收**：参数跨页、NUL 在页边界、无 NUL 三种测试。

### P1-3 WRITE 逐字节 `uart_putchar`

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/task/syscall.c:37-39`
- **原因**：这符合“不要用 `uart_puts(buffer)`”的要求，但每次 `uart_putchar` 都要重新等待 UART LSR.THRE。对 256 字节 WRITE 来说，函数调用和状态检查重复很多次。
- **核心问题代码**：

```c
for (size_t i = 0; i < length; ++i) {
    uart_putchar(buffer[i]);
}
```

- **建议**：在 `drivers/uart.c` 增加 `void uart_write(const void* data, size_t length);`，syscall 层改成 `uart_write(buffer, length)`。这样语义明确按长度输出，不依赖 NUL，也方便以后做发送缓冲。
- **验收**：WRITE 17 字节、A/NUL/B、256 字节输出逐字节一致。

### P1-4 `SYSCALL_OPEN` 在同一表达式读写 `frame->a0`

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/task/syscall.c:265-270`
- **原因**：虽然赋值右侧先求值，读到的应是旧 `a0`，但同一表达式里读写同一个字段可读性差，也容易被静态分析误报。
- **核心问题代码**：

```c
case SYSCALL_OPEN: {
    frame->a0 = (uint64_t)sched_open_file(
        frame,
        (uintptr_t)frame->a0,
        (uint64_t)frame->a1);
    break;
}
```

- **建议**：先保存局部变量，再调用：

```c
const uintptr_t path_address = (uintptr_t)frame->a0;
const uint64_t path_length = (uint64_t)frame->a1;
frame->a0 = (uint64_t)sched_open_file(frame, path_address, path_length);
```

同样适用于 `SYSCALL_READ`、`SYSCALL_WRITE_FD`、`SYSCALL_DUP2` 等分支。

- **验收**：行为不变；`-Wall -Wextra` 无新 warning。

### P1-5 `change_page()` fatal 路径统一（已完成）

- **状态**：已完成（本轮）
- **改动量**：小
- **位置**：`kernel/task/sched.cpp:63-91`
- **原因**：日志写的是“user task has valid root”，实际含义是“缺少 root”；而且 `root == nullptr` 时函数继续执行 `sv39_activate(root)`，最终在 activate 的错误分支里 wfi。日志和实际故障点不一致，调试困难。
- **核心问题代码**：

```cpp
if (root == nullptr) {
    uart_puts("[Error] user task has valid root");
    // 内部状态错误：用户任务缺少私有根。
}

int res = sv39_activate(root);
if (res != SV39_OK) {
    uart_puts(" change page failed");
    for (;;) {
        asm volatile("wfi");
    }
}
```

- **建议**：修正日志为 `missing root`，并在 `root == nullptr` 时直接 fail-stop，避免把内部状态错误伪装成 activate 失败。
- **实现记录（本轮）**：新增 `[[noreturn]] sched_fatal(const char* reason)`；`change_page()` 的 process/root/activate 失败统一走该函数，并修正为 `user task has no process`、`user task has no root`、`activate page table failed`。
- **当前验证**：`make -j4` 通过且无 warning；后续 QEMU 正常路径回归待 `make test-qemu` 覆盖。
- **验收**：构造 `task->proc == nullptr` / `root == nullptr` 的临时探针，日志能直接指出根因。

### P1-6 `task_destroy()` 先关文件再释放地址空间

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/task/user_task_exit.cpp:44-64`
- **原因**：当前顺序是：

```cpp
task->proc->close_files();
const int res = task->proc->release_address_space();
```

如果 `release_address_space()` 失败，任务处于“文件已关、地址空间未完全释放”的状态，随后直接 wfi。当前 fail-stop 语义下不会泄漏 fd，但“关闭文件”和“释放地址空间”的失败契约需要明确。

- **核心问题代码**：

```cpp
void task_destroy(struct task* task, const Sv39PageTable* kernel_root) {
    task->state = TASK_DEAD;
    if (sv39_activate(kernel_root) != SV39_OK) {
        uart_puts("[sched] activating kernel root failed\n");
        for (;;) { asm volatile("wfi"); }
    }

    task->proc->close_files();
    const int res = task->proc->release_address_space();

    if (res != SV39_OK) {
        uart_puts("[user memory] release failed: ");
        uart_put_hex((unsigned long)res);
        uart_puts("\n");
        for (;;) { asm volatile("wfi"); }
    }
}
```

- **建议**：保留 fail-stop，但在注释中明确“地址空间释放失败后进程不可重试”；如果以后要支持重试，先释放地址空间，再关闭文件，或者把两者做成可恢复事务。
- **验收**：故障注入 `release_address_space()` 失败，确认系统行为符合文档预期。

### P1-7 `prepare_builtin_user_space()` 失败返回部分初始化地址空间

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/process/builtin_program.cpp:108-147`
- **原因**：函数先 `user_address_space_create()`，再 `user_memory_load_elf()`。如果 loader 失败，`output` 里可能已经有 root，甚至部分页；函数直接把错误返回给调用方，依赖调用方 `user_address_space_release(&candidate)`。`spawn` 和 `exec` 目前确实这么做，但契约是隐式的。
- **核心问题代码**：

```cpp
const int create_result = user_address_space_create(g_root, &output);
if (create_result != SV39_OK) {
    return create_result;
}

const int load_result = user_memory_load_elf(
    output.root,
    program.file->bytes.data(),
    program.file->bytes.size(),
    &program.layout,
    &g_memory_layout,
    &output.user_memory);

if (load_result == SV39_ERR_PAGE_FREE_FAILED) {
    uart_puts("ELF loader rollback failed\n");
    for (;;) { asm volatile("wfi"); }
}

if (load_result != SV39_OK) {
    return load_result;   // output 可能已经包含 root/部分页
}
```

- **建议**：函数内部失败时自动 `user_address_space_release(&output)`；或者在头文件明确写“失败后调用方必须 release”，并让所有调用方统一走同一个 helper。
- **验收**：ELF 解析失败、loader 第 N 页分配失败两种注入下，页数恢复且 output 全零。

### P1-8 `OpenFile` 字段允许非法组合

- **状态**：待定
- **改动量**：中
- **位置**：`include/fs/open_file.hpp:42-48`
- **原因**：`kind_`、`access_`、`file_` 是三个独立字段，允许出现 `kind_ == None` 但 `file_ != nullptr`，或者 `kind_ == Builtin` 但 `file_ == nullptr` 等非法状态。目前靠 `OpenFilePool` 的 acquire/release 维护不变量，后续新增路径容易破坏。
- **核心问题代码**：

```cpp
private:
    friend class OpenFilePool;
    OpenFileAccess access_{OpenFileAccess::ReadOnly};
    OpenFileKind kind_{OpenFileKind::None};
    const BuiltinFile* file_{};
    size_t offset_{};
    size_t reference_count_{};
```

- **建议**：增加私有 `bool invariant_ok() const` 并在关键入口/出口断言；或者改成 tagged union/`std::variant<Monostate, Builtin, Console>`，从类型上禁止非法组合。
- **验收**：`acquire_console`、`acquire`、`release`、`retain` 之后不变量始终成立；测试覆盖重复 release、池外指针、引用计数 0 等边界。

### P1-9 `dup2`/引用计数路径缺少系统测试

- **状态**：待定
- **改动量**：小-中
- **位置**：`kernel/fs/file_descriptor_table.cpp:140-201`
- **原因**：`inherit_from()`、`duplicate_to()` 同时处理“目标为空/目标已占用/源目标相同/retain 失败”等分支，引用计数正确性对 fork/exec/dup2 很关键。当前测试覆盖不足。
- **核心问题代码**：

```cpp
if (!pool.retain(entry)) {
    pending.close_all(pool);
    return -1;
}
pending.entries_[i] = entry;
...
if (!pool.retain(source_file)) {
    return -1;
}
entries_[target_fd] = source_file;

if (target_file == nullptr) {
    return target_fd;
}

pool.release(target_file);
```

- **建议**：补测试：

  - 两个 fd 指向同一 `OpenFile`，关闭一个后另一个仍可读；
  - `dup2` 到已占用 fd，旧文件引用计数减一；
  - `dup2(source == target)` 不改变引用计数；
  - `inherit_from` 中途 `retain` 失败，已 retain 的条目全部释放；
  - 对象池已满、进程的 FD 表仍有空槽时，`open` 因无法取得新的 `OpenFile` 返回 `-1`，空槽保持为空。
  - 在同一满池条件下，用 `dup2` 将有效的源 FD 复制到一个不同的空目标 FD。预期返回目标 FD，两个槽指向同一 `OpenFile`，源对象的引用计数加一，`OpenFilePool::in_use()` 保持原值。

- **验收**：`OpenFilePool::in_use()` 在每组操作前后符合预期。

### P1-10 控制台读 `poll`/`WOULD_BLOCK` 语义分散

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/fs/file_syscall.cpp:76-93`
- **原因**：控制台读的“无数据”判断同时依赖 `poll_console_input()`、`read_current()` 返回 0、`SYSCALL_ERR_WOULD_BLOCK` 等多个分支，行为不易一眼确认。
- **核心问题代码**：

```cpp
if (is_console && console::poll_console_input() < 0) {
    return -1;
}

unsigned char buffer[SYSCALL_READ_FILE_MAX]{};
const long count = file->read_current(
    std::span<unsigned char>(buffer, static_cast<size_t>(length)));

if (count < 0 || static_cast<uint64_t>(count) > length) {
    return -1;
}
if (count == 0) {
    if (file->kind() == OpenFileKind::Console) {
        return SYSCALL_ERR_WOULD_BLOCK;
    }
    return 0;
}
```

- **建议**：抽成 `prepare_console_read()` 或 `read_console_into()`，统一返回“数据/无数据/错误”三态；`syscall_read` 只负责把结果映射到 ABI。
- **验收**：无输入、输入 1 字节、输入超过缓冲区、设备未就绪四种情况都有明确测试。

---

## P2：性能与数据结构

### P2-1 PMM 重复释放检测是 O(n)，且没有引用计数

- **状态**：待定
- **改动量**：中
- **位置**：`kernel/mm/pmm.cpp:35-55`、`192-209`
- **原因**：intrusive free list + bump allocator 的分配/释放都是 O(1)，设计很轻量。但 `release_page()` 需要线性扫描 free list 检测重复释放；如果以后支持 COW/fork 共享物理页，还需要 per-page refcount。当前设计无法表达“同一物理页被多个地址空间共享”。
- **核心问题代码**：

```cpp
struct free_page {
    free_page* next;
};

int release_page(void* page) {
    ...
    for (struct free_page *current = free_list;
         current != nullptr;
         current = current->next) {
        if (current == page) {
            return -1;      // 重复释放检测 O(n)
        }
    }

    free_page* free_page = static_cast<struct free_page*>(page);
    free_page->next = free_list;
    free_list = free_page;
    ++recycled_page_count;
    return 0;
}
```

- **建议**：增加 1 bit/页的位图，重复释放检测和“是否已分配”判断变成 O(1)；如果准备做 COW，再增加 2 字节/页的 refcount。32768 页对应位图约 4 KiB，refcount 约 64 KiB，代价可接受。
- **验收**：重复释放、非法地址、跨 PMM 区间释放都返回 -1；页数统计与位图一致。

### P2-2 PMM 手写逐字清零循环

- **状态**：待定
- **改动量**：小
- **位置**：`kernel/mm/pmm.cpp:121-125`、`142-146`
- **原因**：手写 512 次 `uint64_t` 赋值，语义上就是 `memset(page, 0, PAGE_SIZE)`。显式 `memset`/`std::fill` 更清楚，也更容易被编译器优化。
- **核心问题代码**：

```cpp
uint64_t* p = reinterpret_cast<uint64_t*>(page);
for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i) {
    p[i] = 0;
}
```

- **建议**：改用 `memset(page, 0, PAGE_SIZE)` 或 `std::fill`。如果出于安全考虑必须清零，保留默认清零行为，不要为了性能省略。
- **验收**：分配出的页内容全零；`pmm_alloc_page` 前后页数正确。

### P2-3 `TaskTable` 固定 5 槽 + 线性 `find_slot`

- **状态**：待定
- **改动量**：小
- **位置**：`include/task/private/task_table.hpp:8-32`、`kernel/task/task_table.cpp:25-38`
- **原因**：当前容量 5，线性扫描成本可以忽略。但 `find_slot`、`slot_of`、`allocate` 都是 O(n)，容量增大或频繁 `waitpid/reap` 时会变明显；`task_id` 和槽位没有直接映射，调试也不直观。
- **核心问题代码**：

```cpp
class TaskTable {
public:
    static constexpr int kCapacity = 5;
    ...
private:
    task tasks_[kCapacity]{};
    Process process_[kCapacity]{};
    int high_water_{};
    uint64_t next_task_id_{};
};

int TaskTable::find_slot(int task_id) const noexcept {
    if (task_id < 0) {
        return -1;
    }
    for (int i = 0; i < high_water_; ++i) {
        if (tasks_[i].state != TASK_UNUSED &&
            tasks_[i].task_id == task_id) {
            return i;
        }
    }
    return -1;
}
```

- **建议**：增加 `int slot_by_id_[kCapacity]` 或 `task_id -> slot` 表；如果任务数会增长，使用 `task_id` 作为槽位 + generation 计数避免 ABA。把 `kCapacity` 做成配置常量。
- **验收**：`find_slot` O(1)，`waitpid/reap` 行为不变；任务创建/回收压力测试通过。

### P2-4 `UserAddressSpace` 和 `UserMemory` 重复持有 root

- **状态**：待定
- **改动量**：中
- **位置**：`include/mm/user_address_space.h:9-13`、`include/mm/user_memory.h:18-28`
- **原因**：`UserAddressSpace.root` 和 `UserMemory.root` 指向同一个根页表。代码里到处检查 `space->user_memory.root == space->root`，释放/加载/克隆路径都要维护这个不变量，容易出现不一致。
- **核心问题代码**：

```cpp
typedef struct UserAddressSpace {
    Sv39PageTable* root;                 // Owned private root page.
    const Sv39PageTable* kernel_root;    // Borrowed permanent kernel template.
    UserMemory user_memory;
} UserAddressSpace;

typedef struct UserMemory {
    Sv39PageTable* root;                 // 根页表（与 UserAddressSpace.root 重复）
    size_t page_count;
    UserMemoryPage pages[USER_MEMORY_MAX_PAGE];
} UserMemory;
```

- **建议**：只保留一个 root 所有者。两种方案：

  1. `UserAddressSpace` 拥有 `root`，`UserMemory` 只保存 `pages/page_count`；
  2. `UserMemory` 拥有 `root`，`UserAddressSpace` 只借用。

  无论哪种，其它地方用 const 引用/借用指针，删除重复不变量检查。
- **验收**：`user_address_space_create/clone/release`、`user_memory_load_elf`、`Process::adopt` 全部编译并通过现有测试；不再需要 `memory.root == space.root` 检查。

### P2-5 `UserMemory` 固定 16 页，页记录信息不足

- **状态**：待定
- **改动量**：中
- **位置**：`include/mm/user_memory.h:9-21`
- **原因**：固定 16 页适合当前“代码 1 页 + 栈 1 页 + 数据最多 14 页”的设计，避免释放路径再分配。但 `UserMemoryPage` 只有 VA+PA，没有 flags/length/所有权状态；`validate_mappings()` 需要重新 query 页表，释放逻辑也无法区分“代码页/栈页/数据页”。
- **核心问题代码**：

```cpp
constexpr unsigned int USER_MEMORY_MAX_PAGE = 16U;

typedef struct UserMemoryPage {
    uintptr_t virtual_address;
    void* physical_page;
} UserMemoryPage;

typedef struct UserMemory {
    Sv39PageTable* root;
    size_t page_count;
    UserMemoryPage pages[USER_MEMORY_MAX_PAGE];
} UserMemory;
```

- **建议**：页记录增加 `uint64_t flags`、`size_t length` 或 `PageKind kind`（Code/Stack/Data）；容量改成配置常量，或者“固定容量 + 溢出拒绝”。如果以后要支持更大程序，再改为从 PMM 分配页数组。
- **验收**：代码/栈/数据页的 flags 可直接校验；`validate_mappings()` 不再需要额外 query 判断权限。

### P2-6 FD/OpenFile 固定容量 + 线性扫描

- **状态**：待定
- **改动量**：中
- **位置**：`include/fs/file_descriptor_table.hpp:14,37`、`include/fs/open_file.hpp:51-70`
- **原因**：FD 表固定 8 项，OpenFile 池固定 40 项，`acquire/retain/release` 都线性扫描。当前规模很小，成本可忽略；但容量是硬编码的，扩展或压力测试时会成为瓶颈。
- **核心问题代码**：

```cpp
class FileDescriptorTable {
public:
    static constexpr size_t kCapacity = 8;
    ...
private:
    OpenFile* entries_[kCapacity]{};
};

class OpenFilePool {
public:
    static constexpr size_t kCapacity = 40;
    ...
private:
    OpenFile files_[kCapacity]{};
};

// acquire 线性扫描
for (OpenFile& entry : files_) {
    if (entry.kind_ == OpenFileKind::None) {
        ...
        return &entry;
    }
}
```

- **建议**：增加空闲链表/位图，O(1) 找空槽；容量改成配置常量；`OpenFilePool` 可以维护 `in_use_` 计数，避免 `in_use()` 每次扫描。
- **验收**：fd 表/文件池满时返回失败且无引用计数泄漏；`in_use()` 与扫描结果一致。

### P2-7 `InputQueue` 环形缓冲的取模和逐字节读取

- **状态**：待定
- **改动量**：小
- **位置**：`include/console/input_queue.hpp:10-25`、`kernel/console/input_queue.cpp:23,42`
- **原因**：`kCapacity = 256` 是 2 的幂，但代码用 `% kCapacity`；`read_current` 逐字节复制。对 256 字节缓冲来说性能影响很小，但可以更直接。
- **核心问题代码**：

```cpp
size_t position = (head_ + size_) % kCapacity;
...
for (size_t i = 0; i < count; ++i) {
    const size_t position = (head_ + i) % kCapacity;
    destination[i] = bytes_[position];
}
```

- **建议**：加

```cpp
static_assert((kCapacity & (kCapacity - 1)) == 0);
```

  使用 `& (kCapacity - 1)`；`read_current` 分两段 `memcpy`（head 到末尾、0 到剩余）。
- **验收**：回绕、空队列、满队列、一次读超过 size 的测试通过。

### P2-8 页表项仍大量裸 `uint64_t` 位运算

- **状态**：待定
- **改动量**：中
- **位置**：`include/mm/vm.h:78-100`、`kernel/mm/vm.cpp` 多处
- **原因**：`PageTableEntry` 已经封装了 PTE 编解码，但 `Sv39PageTable::entries` 仍是裸 `uint64_t`，`sv39_walk_to_leaf`、`map`、`unmap`、`reclaim_empty_tables` 里大量直接 `*entry & SV39_PTE_V`、`PageTableEntry(*entry)` 混用。类型安全性和可读性还有提升空间。
- **核心问题代码**：

```cpp
typedef struct Sv39PageTable {
    Sv39Pte entries[SV39_PTE_COUNT];
    ...
} Sv39PageTable;

if ((*entry & SV39_PTE_V) == 0) { ... }
else if (!PageTableEntry(*entry).is_valid()) { ... }
else if (PageTableEntry(*entry).is_leaf()) { ... }
```

- **建议**：统一通过 `PageTableEntry` 访问（或至少提供 `entry_is_valid/entry_is_leaf/entry_ppn` helper），减少重复位运算；为将来支持 2 MiB/1 GiB 大页，给 `physical_address()` 增加页大小参数或 `is_superpage()`。
- **验收**：现有 VM 测试全过；新增大页/非法 PTE 测试有明确行为。

### P2-9 `PreparedPage + rollback_order` 好模式没有复用

- **状态**：待定
- **改动量**：中
- **位置**：`kernel/mm/user_loader.cpp:132-290`
- **原因**：`PreparedPage` 记录 `physical_page`、`mapping_attempted`、`mapped`，配合 `rollback_order` 做逆序回滚，这是项目中最好的错误处理结构之一。但类似逻辑在 `user_memory_release`、`user_address_space_clone`、`exec` 里重复出现。
- **核心问题伪代码**：

```text
PreparedPage pages[N];
for each page: alloc physical page
for each page: load segment data
fence.i
for each page: map, set mapped=true
for each page: flush
commit: memory_out = prepared

clean_up:
    for index in rollback_order:
        if page.mapped: unmap
    for index in rollback_order:
        if page.mapping_attempted: flush
    for each page:
        if physical_page: free
    for index in rollback_order:
        reclaim_empty_tables
```

- **建议**：抽成通用 `ResourceTransaction`/`UserMemoryTransaction`，在 loader、release、clone、exec 中复用。成功时 `commit()`，失败/提前返回时析构自动回滚。
- **验收**：loader 的现有 rollback 测试全过；clone/exec 也能复用同一套事务。

### P2-10 `validate_no_overlap()` 是 O(n²)

- **状态**：待定
- **改动量**：小（当前不必改）
- **位置**：`include/mm/address.hpp:307-330`
- **原因**：两两比较，当前 16 页规模完全没问题。只有把 `USER_MEMORY_MAX_PAGE` 增大到几百/几千时，O(n²) 才会明显。
- **核心问题代码**：

```cpp
for (size_t i = 0; i < ranges.size(); ++i) {
    if (!ranges[i].is_valid()) {
        return false;
    }
    for (size_t j = i + 1; j < ranges.size(); ++j) {
        if (ranges[i].overlaps_with(ranges[j])) {
            return false;
        }
    }
}
```

- **建议**：当前保持；如果容量扩大，先按 start 排序再线性扫描，或使用区间树。
- **验收**：容量扩展后再评估；当前不作为优先项。

---

## P3：构建、测试与清理

### P3-1 `verify_syscall_write.py` 少逗号导致 `SyntaxError`

- **状态**：待定
- **改动量**：小
- **位置**：`test/verify_syscall_write.py` 的 `unmapped_start` 场景
- **原因**：Python 会把相邻字符串字面量自动拼接：

```python
"setup": "    li a0, 0x40002000\n    li a1, 1\n"
"expected_return": -1,
```

  实际被解析成 `"setup": ("...\n" "expected_return") : -1`，冒号出现位置非法。
- **核心问题代码**：

```python
{
    "name": "unmapped_start",
    "kind": "generated",
    "setup": "    li a0, 0x40002000\n    li a1, 1\n"
    "expected_return": -1,
    "expected_bytes": b"",
}
```

- **建议**：`"setup"` 行末加逗号：

```python
"setup": "    li a0, 0x40002000\n    li a1, 1\n",
"expected_return": -1,
```

  并在 CI 加入 `python3 -m py_compile test/*.py`。
- **验收**：脚本可执行，`length_zero`/`unmapped_start` 场景能跑。

### P3-2 WRITE 标记之间可能插入 timer/A 输出

- **状态**：待定
- **改动量**：小-中
- **位置**：`test/verify_syscall_write.py` 的 `USER_IMAGE_TEMPLATE`
- **原因**：脚本用两个 `PUTCHAR`（0x03/0x04）夹住 WRITE，然后精确比较两者之间的字节。`PUTCHAR` 和 WRITE 是多个独立 syscall；在它们之间，timer 中断可能打印 `[timer] tick in S-mode`，A/B 内核任务也可能打印字符，导致 `actual_bytes != expected_bytes`。
- **核心问题代码**：

```asm
    li a0, 3
    li a7, SYSCALL_PUTCHAR
    ecall

    mv a0, s1
    mv a1, s2
    li a7, SYSCALL_WRITE
    ecall
    mv s0, a0

    li a0, 4
    li a7, SYSCALL_PUTCHAR
    ecall
```

  Python 侧：

```python
_, _, after_begin = output.partition(WRITE_BEGIN)
actual_bytes, end_marker, after_end = after_begin.partition(WRITE_END)
if actual_bytes != expected_bytes:
    raise SystemExit(...)
```

- **建议**：三种方案任选：

  1. 用一次 `SYSCALL_WRITE` 输出整个开始/结束标记，使标记原子化；
  2. 从 `actual_bytes` 中过滤已知内核输出（`[timer] tick in S-mode`、`A`、`B`、`[syscall] ...`）；
  3. 在测试场景中先停掉/隔离 A/B 任务（临时 QEMU 副本），只保留用户任务和 timer。

- **验收**：连续跑 20 次同一场景无偶发失败。

### P3-3 `test-qemu` 未接入新 WRITE 脚本

- **状态**：待定
- **改动量**：小
- **位置**：`Makefile:202-204`
- **原因**：当前只跑旧的 Q1/Q2 脚本，新的 WRITE 验证没有被 `make test-qemu` 覆盖。
- **核心问题代码**：

```make
test-qemu:
python3 test/verify_prepare_rollback.py
python3 test/verify_task_creation_rollback.py
```

- **建议**：

```make
test-qemu:
python3 test/verify_prepare_rollback.py
python3 test/verify_task_creation_rollback.py
python3 test/verify_syscall_write.py
```

- **验收**：`make test-qemu` 能跑完三个脚本并以非零退出码报告失败。

### P3-4 C 用 GCC、C++ 用 Clang

- **状态**：待定
- **改动量**：中
- **位置**：`Makefile:1-4`
- **原因**：C 和汇编用 `riscv64-linux-gnu-gcc`，C++ 用 `clang++ --target=riscv64-unknown-elf`，链接用 GNU ld。ABI 理论上兼容，但工具链混用会增加异常、初始化、调试信息和优化行为差异，排查链接问题时更复杂。
- **核心问题代码**：

```make
CC = riscv64-linux-gnu-gcc
CXX = clang++
LD = riscv64-linux-gnu-ld
```

- **建议**：统一为 `riscv64-linux-gnu-g++`，或者 C/C++ 都用 Clang；在 README 里写清工具链版本和理由。
- **验收**：`make clean && make && make test && timeout 8s make qemu` 全过。

### P3-5 默认 `-O0`，缺少 sanitizer/优化档位

- **状态**：待定
- **改动量**：小
- **位置**：`Makefile:13`
- **原因**：`OPTIMIZATION ?= -O0` 适合调试，但 QEMU 回归和性能测试没有优化档位；宿主测试也没有 ASan/UBSan 选项，容易漏掉越界/未初始化/泄漏。
- **核心问题代码**：

```make
OPTIMIZATION ?= -O0
CFLAGS = -Wall -Wextra $(OPTIMIZATION) -g ...
CXXFLAGS = ... $(OPTIMIZATION) -g ...
```

- **建议**：保留 `-O0` 作为默认调试档；CI/回归使用 `OPTIMIZATION=-O2`；宿主测试增加可选 `-fsanitize=address,undefined` target。
- **验收**：`make OPTIMIZATION=-O2` 和宿主 sanitizer target 都能构建并通过测试。

### P3-6 调试残留与重复声明/包含

- **状态**：待定
- **改动量**：小
- **位置**：

  - `kernel/boot/main.cpp:6-19`
  - `kernel/boot/private/boot_stages.cpp:16-19,29-31`
  - `kernel/fs/file_syscall.cpp:7,10`
  - `kernel/task/task_table.cpp:106-107`

- **原因**：这些是清理项，不影响当前功能，但会污染启动输出、增加阅读负担，重复 include/return 也容易被静态分析标记。
- **核心问题代码**：

```cpp
class Test {
public:
    Test(char v) : value(v) { count += 1; }
    ...
};
Test test('D');

extern "C" void kernel_main(...) {
    uart_putchar(test.value);
    uart_put_hex(static_cast<unsigned long>(test.count));
    ...
}
```

```cpp
// boot_stages.cpp 重复 extern
extern const unsigned char user_elf_start[];
extern const unsigned char user_elf_end[];
...
extern const unsigned char user_elf_start[];
extern const unsigned char user_elf_end[];
```

```cpp
// file_syscall.cpp 重复 include
#include "task/syscall.h"
...
#include "task/syscall.h"
```

```cpp
// task_table.cpp 重复 return
return 0;
return 0;
```

- **建议**：删除 `Test` 调试对象和启动打印；合并重复 extern；删除重复 include；删除重复 return。启动日志只保留 `boot_console()` 的输出。
- **验收**：`make` 通过，QEMU 启动输出不再出现 `D0x...` 调试行。

### P3-7 拼写/命名清理

- **状态**：待定
- **改动量**：小
- **位置**：多处
- **原因**：拼写错误不影响运行，但会影响 API 可读性和后续维护。
- **核心问题代码/清单**：

```text
include/process/builtin_program.hpp: BUiltinProgramImage
include/mm/user_access.h:          destinatiom_address
kernel/mm/user_access.cpp:         writed
kernel/task/user_task_exit.cpp:    _is_have_child
test/test_user_access.cpp:         test_iliegal_second_page
test/user_memory_fixture.hpp:      alllocate borrowed root oage table
kernel/task/sched.cpp:             "user task has valid root"（语义反了）
```

- **建议**：统一改为 `BuiltinProgramImage`、`destination_address`、`written`、`has_child`、`test_illegal_second_page`、`allocate borrowed root page table`、`user task missing root`。
- **验收**：全仓 `grep` 不再出现旧拼写；编译/测试通过。

### P3-8 `test_main.cpp` 手工声明一长串测试入口

- **状态**：待定
- **改动量**：中
- **位置**：`test/test_main.cpp:11-20`
- **原因**：每新增一个测试文件，都要在这里加声明和调用，容易漏接；测试顺序也硬编码在 `main()` 里。
- **核心问题代码**：

```cpp
int test_user_page_collection();
int test_user_address_space();
int test_task_table();
int test_builtin_fs();
...
int main(void) {
    if (test_pmm() != 0) return EXIT_FAILURE;
    if (test_vm() != 0) return EXIT_FAILURE;
    ...
}
```

- **建议**：使用测试头文件或注册表宏，例如：

```cpp
REGISTER_TEST(test_pmm);
REGISTER_TEST(test_vm);
...
```

  或者在 `test/CMakeLists.txt` 里把测试拆成独立 CTest 用例（但 PMM 有全局状态，需保持串行）。
- **验收**：新增测试不再需要手改 `main()`；测试顺序可配置。

### P3-9 缺少 OOM/部分失败/fault 测试

- **状态**：待定
- **改动量**：中
- **位置**：`test/`
- **原因**：当前 P0 中的多个问题都是“失败路径”问题，但测试主要覆盖正常路径和少量 free/flush 注入，缺少 clone OOM、map 中间页表 OOM、U-mode 非页错误异常、FD 引用计数等测试。
- **建议**：优先补：

  - `user_address_space_clone` 第 N 页分配失败；
  - `sv39_map_page` 中间页表分配失败；
  - `user_memory_release` 在 unmap/flush/free/reclaim 各阶段失败；
  - U-mode `ebreak`、misaligned load/store；
  - `OpenFile` 引用计数/`dup2` 边界。

- **验收**：每个 P0/P1 修复都有对应的失败注入测试。

### P3-10 源码树内构建产物与 clean 通配符

- **状态**：待定
- **改动量**：小
- **位置**：`Makefile:216-225`、`.gitignore`
- **原因**：`.o/.d/kernel.elf` 直接生成在源码树里；`clean` 依赖手写通配符，新增目录层级时可能漏删。构建产物和源码混在一起，也不利于多配置构建。
- **核心问题代码**：

```make
clean:
rm -f kernel/*.o kernel/*.d drivers/*.o drivers/*.d kernel.elf \
kernel/*/*.o kernel/*/*.d kernel/*/*/*.o kernel/*/*/*.d \
...
```

- **建议**：短期把 `clean` 改成 `find kernel drivers lib -name '*.o' -o -name '*.d' -delete`；长期考虑 out-of-tree `build/` 目录。确认 `.gitignore` 覆盖所有产物。
- **验收**：`make clean` 后 `git status` 干净；重新 `make` 通过。

---

## 本轮已完成的改动（2026-10-06）

| ID | 内容 | 关键文件 | 验证 |
|---|---|---|---|
| DONE-1 | `user_address_space_clone()` 改为局部 `candidate{}` + 单一 `out_release`，成功后才发布到 `output`；修复 `new_page == nullptr` 未返回导致的空指针解引用 | `kernel/mm/user_address_space.cpp` | 单文件编译通过；`make test` 3/3 |
| DONE-2 | 新增 `sched_fatal()`，统一 `change_page()` 的 process/root/activate fatal 路径 | `kernel/task/sched.cpp` | `make -j4` 通过且无 warning |
| DONE-3 | 新增 `current_user_task_for_syscall()`，收敛 fork/exec/file syscall 的重复 prologue | `kernel/task/sched.cpp` | `make -j4` 通过 |
| DONE-4 | 引入 `TaskPeekResult` 命名结果；`TaskTable` 增加 recycle 结果常量；`Process::reset()` 失败按不变量破坏 fatal 处理 | `include/task/sched.h`、`kernel/task/sched.cpp`、`kernel/task/syscall.c`、`include/task/private/task_table.hpp`、`kernel/task/task_table.cpp` | `make -j4`、`make test` 均通过 |

### `sched.cpp` 改动前后统计

| 指标 | 改动前 | 改动后 | 说明 |
|---|---:|---:|---|
| 行数 | 761 | 750 | 略减，主要来自统一 fatal/prologue |
| `supervisor_interrupts_disabled()` 出现次数 | 14 | 5 | 定义 + 4 个仍需检查的边界 |
| 直接 `current_user_task(frame)` 调用 | 多处 | 1 处 | 只剩 `current_user_task_for_syscall()` 内部 |
| `wfi` 次数 | 4 | 3 | idle、task_exit、sched_fatal |
| `sched_fatal` 调用点 | 0 | 4 | change_page 三处 + task_reap 一处 |
| `goto` 次数 | 0 | 0 | 本轮 sched 没有引入 goto；clone 用了 out_release |

### 仍然保留的 sched 设计判断

- `sched_reschedule()` 返回 `trap_frame*`，上下文切换失败仍然不可恢复，因此 `change_page()` 继续 fail-stop 是合理的。
- `task_create_user()` 的所有权契约仍然是“成功转移，失败由调用方释放”，本轮没有引入 `UserAddressSpaceGuard`；这属于后续可选项。
- `TaskPeekResult` 只解决了返回值可读性；`waitpid` 的 `-2` would-block 语义仍在 syscall 层，后续可继续命名化。

## 当前未解决问题分类（待定）

| 类别 | 包含条目 | 状态 |
|---|---|---|
| 资源回滚与失败路径 | P0-2、P0-4、P1-7、P2-9、S-1、S-2、S-6 | 待定 |
| 用户故障与安全 | P0-3、P1-8 | 待定 |
| 调度器错误模型与结构 | S-1、S-2、S-3、S-4、S-5 | 待定 |
| 性能与数据结构 | P2-1、P2-2、P2-3、P2-4、P2-5、P2-6、P2-7、P2-8、P2-10 | 待定 |
| 构建/测试/工具 | P3-1、P3-2、P3-3、P3-4、P3-5、P3-8、P3-9、P3-10 | 待定 |
| 契约与文档 | P0-5、P1-6、P1-10 | 待定 |

### 新增的 sched 专项待定项

| ID | 位置 | 问题/影响 | 建议 | 改动量 | 状态 |
|---|---|---|---|---|---|
| S-1 | `kernel/task/sched.cpp` | 仍有大量 `return -1`，无法区分参数错、无内存、任务表满、状态错、无子进程等 | 定义内部 `SchedError`/命名错误码；syscall ABI 边界再映射到 -1 | 中 | 待定 |
| S-2 | `kernel/task/sched.cpp:402-435` | `task_reap`/`TaskTable` 除 `kRecycleProcessNotReset` 外仍返回通用 -1 | 继续命名剩余结果，区分“目标非法/状态错/不变量破坏” | 小 | 待定 |
| S-3 | `kernel/task/sched.cpp` | 750 行混合 init/调度/等待/fork/exec/文件 syscall/reaper | 拆为 `sched_core/sched_task/sched_wait/sched_user/sched_file/sched_reaper` | 中 | 待定 |
| S-4 | `kernel/task/sched.cpp:551-606` | `sched_configure_user_reaper` 失败只返回 -1，缺少诊断 | 返回具体错误码，boot 阶段打印可读原因 | 小 | 待定 |
| S-5 | `kernel/task/sched.cpp:106-141,723-761` | `sched_init`/`scheduler_start` 失败只返回 -1，boot_fail 只能打印通用信息 | 返回具体错误码或提供 `sched_last_error()` | 小 | 待定 |
| S-6 | `test/` | 缺少 sched/fork/exec/reap 的失败路径测试 | 增加 QEMU 临时探针与 TaskTable/Process 宿主测试 | 中 | 待定 |

## 建议处理顺序

> 已完成：P0-1、P1-5，以及本轮 sched 三步（DONE-1 ~ DONE-4）。

1. **P3-1**：修 `verify_syscall_write.py` 逗号错误，接上基本 QEMU 回归。
2. **P0-3**：U-mode 异常收敛，改动小、收益大。
3. **P0-2**：`sv39_map_page()` 中间页表分配失败回滚。
4. **P0-4**：`user_memory_release()` 释放阶段显式化，补故障注入。
5. **P1-1 / P1-3**：用户复制分块和 `uart_write`。
6. **P2-1 / P2-4**：PMM 位图/refcount，以及 root 所有权收敛。
7. **S-1 / S-3**：调度器错误码命名化与 `sched.cpp` 拆分。
8. **P3-4 / P3-5**：统一工具链、加 CI 优化档位和 sanitizer。
9. **P3-6 / P3-7**：清理调试残留和拼写。
10. 最后补 P3-8 / P3-9 / S-6 的测试基础设施和失败路径覆盖。

> P0-1 与 sched 三步已实施并验证；其余条目仍为 **待定**。
