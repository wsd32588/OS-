#include "task/private/user_task.hpp"

#include "arch/riscv/trap.h"
#include "arch/riscv/vm_arch.h"
#include "drivers/uart.h"
#include "mm/address.hpp"
#include "mm/user_address_space.h"
#include "mm/user_memory.h"
#include "process/process.hpp"
#include "process/builtin_program.hpp"
#include "task/private/task.hpp"

namespace tinyos::user_task {
namespace {
constexpr unsigned long kSstatusSpie = 1UL << 5;

[[noreturn]] void exec_commit_fail(const char* reason) {
    uart_puts(reason);
    for (;;) {
        asm volatile("wfi");
    }
}
} // namespace

int exec(task& current, trap_frame& frame, const Sv39PageTable* kernel_root, uint64_t program_id, uintptr_t argument) {

    UserAddressSpace candidate{};
    uintptr_t entry{};
    uintptr_t stack_top{};

    int result = tinyos::prepare_builtin_user_space(program_id, candidate, entry, stack_top);

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
        !sp.is_valid() || (sp.val() & 0xFUL) != 0) {
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

    result = sv39_activate(kernel_root);
    if (result != SV39_OK) {
        exec_commit_fail("exec: activate kernel root failed\n");
    }

    result = current.proc->release_resources();
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

} // namespace tinyos::user_task
