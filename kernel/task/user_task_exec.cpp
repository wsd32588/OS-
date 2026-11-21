#include "task/private/user_task.hpp"

#include <stddef.h>
#include <stdint.h>
#include <span>

#include "arch/riscv/trap.h"
#include "arch/riscv/vm_arch.h"
#include "drivers/uart.h"
#include "fs/file_service.hpp"
#include "mm/address.hpp"
#include "mm/user_access.h"
#include "mm/user_address_space.h"
#include "mm/user_memory.h"
#include "process/builtin_program.hpp"
#include "process/process.hpp"
#include "task/private/task.hpp"

namespace tinyos::user_task {
namespace {
/*
 * exec 参数传递（B31）的整体流程：
 *
 *   1. 在旧地址空间里，把用户给的 path/argv 拷到内核栈上的临时结构；
 *   2. 装载新镜像，得到新的用户地址空间 candidate 和新栈顶；
 *   3. 在新地址空间的用户栈上，从高地址向低地址放字符串和 argv 表；
 *   4. 重置 trap frame，让新程序入口按 ABI 看到 a0/a1/a2；
 *   5. 切内核根 -> 释放旧空间 -> adopt 新空间 -> 切新根。
 *
 * 关键约束：旧空间和新空间的 satp 不同，不能直接解引用用户 VA；
 * 所有读写都通过 user_copy_from/user_copy_to 按页完成。
 */
constexpr unsigned long kSstatusSpie = 1UL << 5;
constexpr size_t kMaxExecArguments = 4;       // argv 最多 4 项（含 argv[0]）
constexpr size_t kMaxExecArgumentBytes = 64;  // 每个字符串最多 63 字节 + NUL
constexpr uintptr_t kStackAlignment = 16;     // RISC-V 入口栈必须 16 字节对齐

static_assert(sizeof(uintptr_t) == 8);

struct ExecArgument {
    char bytes[kMaxExecArgumentBytes];
    size_t length;
};

struct PreparedArguments {
    ExecArgument entries[kMaxExecArguments];
    size_t count;
};

[[noreturn]] void exec_commit_fail(const char* reason) {
    uart_puts(reason);
    for (;;) {
        asm volatile("wfi");
    }
}

/*
 * 把用户路径解析成已登记的内置程序编号。
 * 路径只用于 lookup 身份，真正比较的是 BuiltinFile 记录地址；
 * 因此不能硬编码 "/bin/worker"，也不能比较 ELF 字节。
 */
bool resolve_program_id(
    const Sv39PageTable* old_root,
    uintptr_t path_address,
    uint64_t path_length,
    uint64_t& program_id
) {
    if (!fs::file_service_ready() || old_root == nullptr || path_length == 0 ||
        path_length > fs::kMaxBuiltinPathLength) {
        return false;
    }

    char path_buffer[fs::kMaxBuiltinPathLength]{};
    const size_t length = static_cast<size_t>(path_length);
    if (user_copy_from(old_root, path_buffer, path_address, length) != SV39_OK) {
        return false;
    }

    const std::span<const char> path{path_buffer, length};
    const fs::BuiltinFile* file = fs::lookup_file(path);
    return file != nullptr && find_builtin_program(*file, program_id) == 0;
}

/*
 * 从旧用户空间逐字节读一个参数字符串，直到 NUL。
 * length 包含结尾的 NUL（写回新栈时一起复制）。
 * 不能把 address 强转成内核指针——旧 root 的映射不一定在当前 satp 下。
 */
bool copy_exec_argument(const Sv39PageTable* old_root, uintptr_t address, ExecArgument& argument) {
    if (address == 0) {
        return false;
    }

    const VirtAddr start(address);
    for (size_t offset = 0; offset < sizeof(argument.bytes); ++offset) {
        VirtAddr byte_address;
        if (!start.try_add(offset, byte_address) ||
            user_copy_from(old_root, &argument.bytes[offset], byte_address.val(), 1) != SV39_OK) {
            return false;
        }

        if (argument.bytes[offset] == '\0') {
            argument.length = offset + 1;
            return true;
        }
    }
    return false;
}

/*
 * 先把用户地址空间里的 argv 指针表（char*[argc]）整体拷到内核数组，
 * 再逐个把每个指针指向的字符串拷到 ExecArgument。
 * 分两步是因为第一次拷贝后，指针表本身才在内核里可安全遍历。
 */
bool copy_exec_arguments(
    const Sv39PageTable* old_root,
    uintptr_t argv_address,
    uint64_t argc,
    PreparedArguments& arguments
) {
    if (argc > kMaxExecArguments) {
        return false;
    }

    arguments.count = static_cast<size_t>(argc);
    if (arguments.count == 0) {
        return true;
    }

    uintptr_t old_argv[kMaxExecArguments]{};
    const size_t table_bytes = arguments.count * sizeof(old_argv[0]);
    if (argv_address == 0 ||
        user_copy_from(old_root, old_argv, argv_address, table_bytes) != SV39_OK) {
        return false;
    }

    for (size_t i = 0; i < arguments.count; ++i) {
        if (!copy_exec_argument(old_root, old_argv[i], arguments.entries[i])) {
            return false;
        }
    }
    return true;
}

/*
 * 在新用户栈上布局 argv，从栈顶向低地址生长：
 *
 *   stack_top
 *     | 字符串 n-1 ... 字符串 0   |  （每个都含结尾 NUL）
 *     | argv[n-1] ... argv[0], NULL |  （指针表，最后一项是 NULL）
 *   table_address = 初始 SP（16 字节对齐）
 *
 * 字符串先放，指针表后放，保证表项指向的地址已经确定。
 * new_argv 里存的是新空间 VA，不是旧空间地址。
 */
bool write_exec_arguments(
    const Sv39PageTable* candidate_root,
    uintptr_t stack_top,
    const PreparedArguments& arguments,
    uintptr_t& argv_address
) {
    if (stack_top < SV39_PAGE_SIZE) {
        return false;
    }

    const uintptr_t stack_bottom = stack_top - SV39_PAGE_SIZE;
    uintptr_t cursor = stack_top;
    uintptr_t new_argv[kMaxExecArguments + 1]{};
    for (size_t i = 0; i < arguments.count; ++i) {
        const size_t length = arguments.entries[i].length;
        if (length > cursor - stack_bottom) {
            return false;
        }
        cursor -= length;
        new_argv[i] = cursor;
    }

    // argv 表多放一项 NULL，作为 C 字符串数组的结尾。
    const size_t table_bytes = (arguments.count + 1) * sizeof(new_argv[0]);
    if (table_bytes > cursor - stack_bottom) {
        return false;
    }

    // 指针表向下对齐到 16 字节；这个地址同时作为新程序的初始 SP。
    // 对齐掉的那几个字节是栈顶填充，不包含数据。
    const uintptr_t table_address = (cursor - table_bytes) & ~(kStackAlignment - 1);
    if (table_address < stack_bottom) {
        return false;
    }

    for (size_t i = 0; i < arguments.count; ++i) {
        const ExecArgument& argument = arguments.entries[i];
        if (user_copy_to(candidate_root, new_argv[i], argument.bytes, argument.length) != SV39_OK) {
            return false;
        }
    }

    if (user_copy_to(candidate_root, table_address, new_argv, table_bytes) != SV39_OK) {
        return false;
    }

    argv_address = table_address;
    return true;
}

/*
 * exec 的公共实现，带或不带 argv 都走这里。
 * 提交顺序不可交换：
 *   - 先切回 kernel_root，才能安全释放旧用户空间；
 *   - release_address_space 之后旧 root 不能再被访问；
 *   - adopt 新空间后再切到新 root；
 * 任何一步失败都只能停机，因为旧空间已经释放或 satp 已切走。
 */
int exec_impl(
    task& current,
    trap_frame& frame,
    const Sv39PageTable* kernel_root,
    uint64_t program_id,
    uintptr_t argument,
    const PreparedArguments* arguments
) {
    UserAddressSpace candidate{};
    uintptr_t entry{};
    uintptr_t stack_top{};

    int result = prepare_builtin_user_space(program_id, candidate, entry, stack_top);

    if (result != SV39_OK) {
        if (user_address_space_release(&candidate) != SV39_OK) {
            uart_puts("exec: candidate cleanup failed\n");
            for (;;) {
                asm volatile("wfi");
            }
        }

        return -1;
    }

    const auto reject_candidate = [&candidate]() -> int {
        if (user_address_space_release(&candidate) != SV39_OK) {
            uart_puts("exec candidate cleanup failed\n");
            for (;;) {
                asm volatile("wfi");
            }
        }
        return -1;
    };

    if (candidate.kernel_root != kernel_root || candidate.root == nullptr ||
        candidate.root == candidate.kernel_root || candidate.root != candidate.user_memory.root ||
        candidate.user_memory.page_count == 0 ||
        candidate.user_memory.page_count > USER_MEMORY_MAX_PAGE) {
        return reject_candidate();
    }

    const VirtAddr pc(entry);
    const VirtAddr sp(stack_top);
    if (pc.val() == 0 || !pc.is_valid() || (pc.val() & 1UL) != 0 || sp.val() == 0 ||
        !sp.is_valid() || (sp.val() & (kStackAlignment - 1)) != 0) {
        return reject_candidate();
    }

    uint64_t physical_address{};
    uint64_t flags{};
    const Sv39PageTable* root = candidate.root;
    const uint64_t pc_flags = SV39_PTE_U | SV39_PTE_X;
    result = root->query(pc.val(), &physical_address, &flags);
    if (result != SV39_OK || (flags & pc_flags) != pc_flags) {
        return reject_candidate();
    }

    const uint64_t stack_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_W;
    result = root->query(sp.val() - 1U, &physical_address, &flags);
    if (result != SV39_OK || (flags & stack_flags) != stack_flags) {
        return reject_candidate();
    }

    trap_frame fresh_frame{};
    fresh_frame.sepc = entry;
    fresh_frame.sp = stack_top;
    fresh_frame.a0 = argument;
    fresh_frame.sstatus = kSstatusSpie;

    if (arguments != nullptr && arguments->count != 0) {
        uintptr_t argv_address{};
        if (!write_exec_arguments(root, stack_top, *arguments, argv_address)) {
            return reject_candidate();
        }
        // 新程序的入口约定：a0 = argc，a1 = argv 指针，SP 指向 argv 表。
        fresh_frame.sp = argv_address;
        fresh_frame.a1 = arguments->count;
        fresh_frame.a2 = argv_address;
    }

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

    return 0;
}
} // namespace

/* 旧接口：只传一个不透明整数参数，没有 argv。 */
int exec(
    task& current,
    trap_frame& frame,
    const Sv39PageTable* kernel_root,
    uint64_t program_id,
    uintptr_t argument
) {
    return exec_impl(current, frame, kernel_root, program_id, argument, nullptr);
}

/* 按路径 exec，不带 argv。 */
int exec_path(
    task& current,
    trap_frame& frame,
    const Sv39PageTable* kernel_root,
    uintptr_t path_address,
    uint64_t path_length,
    uintptr_t argument
) {
    uint64_t program_id{};
    if (!resolve_program_id(current.proc->root(), path_address, path_length, program_id)) {
        return -1;
    }

    return exec(current, frame, kernel_root, program_id, argument);
}

/* 按路径 exec，并把用户 argv 复制到新栈。 */
int exec_path_args(
    task& current,
    trap_frame& frame,
    const Sv39PageTable* kernel_root,
    uintptr_t path_address,
    uint64_t path_length,
    uintptr_t argv_address,
    uint64_t argc
) {
    const Sv39PageTable* old_root = current.proc->root();
    uint64_t program_id{};
    if (!resolve_program_id(old_root, path_address, path_length, program_id)) {
        return -1;
    }

    PreparedArguments arguments{};
    if (!copy_exec_arguments(old_root, argv_address, argc, arguments)) {
        return -1;
    }

    return exec_impl(current, frame, kernel_root, program_id, 0, &arguments);
}
} // namespace tinyos::user_task
