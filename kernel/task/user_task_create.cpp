#include "task/private/user_task.hpp"

#include "arch/riscv/trap.h"
#include "drivers/uart.h"
#include "mm/address.hpp"
#include "mm/user_address_space.h"
#include "mm/user_memory.h"
#include "process/process.hpp"
#include "task/private/task.hpp"
#include "task/private/task_table.hpp"

namespace tinyos::user_task {
namespace {
constexpr unsigned long kSstatusSpie = 1UL << 5;

int task_create_user_from_frame(
    TaskTable& table,
    const trap_frame& snapshot,
    UserAddressSpace& candidate,
    int parent_id
) {
    task* child = nullptr;
    const int task_id = table.allocate(&child);
    if (task_id < 0) {
        return -1;
    }

    *child->context.frame = snapshot;
    child->context.frame->a0 = 0;

    const int slot_index = table.slot_of(child);
    child->proc = table.process_at(slot_index);
    child->proc->adopt(candidate);
    child->kind = TASK_KIND_USER;
    child->proc->set_parent_id(parent_id);

    return task_id;
}

int discard_fork_candidate(UserAddressSpace& candidate) {
    const int result = user_address_space_release(&candidate);
    if (result != SV39_OK) {
        uart_puts("fork candidate cleanup failed\n");
        for (;;) {
            asm volatile("wfi");
        }
    }
    return -1;
}
} // namespace

int create(
    TaskTable& table,
    const Sv39PageTable* kernel_root,
    uintptr_t entry,
    uintptr_t stack_top,
    UserAddressSpace& source,
    uintptr_t argument,
    int parent_id
) {
    if (kernel_root == nullptr || entry == 0 || source.root == nullptr || (entry & 1U) != 0 ||
        stack_top == 0 || (stack_top & 0xFUL) != 0 || source.root == source.kernel_root ||
        //有过error, 私有 root 本来就应该不同于内核 root，
        //所以用!=的话用户任务一定在这里返回 -1
        source.kernel_root != kernel_root || source.user_memory.root != source.root) {
        return -1;
    }

    const uintptr_t page_mask = ~((uintptr_t)SV39_PAGE_SIZE - 1U);
    const UserMemoryPage* code = user_memory_find_page(&source.user_memory, entry & page_mask);
    const UserMemoryPage* stack =
        user_memory_find_page(&source.user_memory, (stack_top - 1U) & page_mask);
    if (code == nullptr || stack == nullptr || code->physical_page == nullptr ||
        stack->physical_page == nullptr) {
        return -1;
    }

    struct task* task = nullptr;
    int task_id = table.allocate(&task);

    if (task_id < 0) {
        return -1;
    }

    task->context.frame->sepc = entry;
    task->context.frame->sp = stack_top;
    task->context.frame->a0 = argument;
    task->context.frame->sstatus = kSstatusSpie;
    task->context.frame->gp = 0;
    /*
     *SPP = 0: sret后回到U-mode
     *SPIE = 1: sret后SIE恢复到1
     *当前用户汇编不用gp,所以设置为0
     *不把内核 gp 暴露给用户初始上下文
     */

    const int slot_index = table.slot_of(task);
    task->proc = table.process_at(slot_index);
    task->proc->adopt(source);
    source = {};
    task->kind = TASK_KIND_USER;
    task->proc->set_parent_id(parent_id);
    return task_id;
}

int fork(TaskTable& table, task& parent, const trap_frame& frame) {
    const VirtAddr pc(frame.sepc);
    const VirtAddr sp(frame.sp);

    if (!pc.is_valid() || (pc.val() & 1UL) != 0 || !sp.is_valid() || sp.val() == 0 ||
        (sp.val() & 0xFUL) != 0) {
        return -1;
    }

    uint64_t physical_address{};
    uint64_t flags{};
    const Sv39PageTable* root = parent.proc->root();

    const uint64_t pc_flags = SV39_PTE_U | SV39_PTE_X;
    int res = root->query(pc.val(), &physical_address, &flags);

    if (res != SV39_OK || (flags & pc_flags) != pc_flags) {
        return -1;
    }

    const uint64_t stack_flags = SV39_PTE_U | SV39_PTE_R | SV39_PTE_W;

    res = root->query(sp.val() - 1U, &physical_address, &flags);

    if (res != SV39_OK || (flags & stack_flags) != stack_flags) {
        return -1;
    }

    const struct trap_frame snapshot = frame;
    UserAddressSpace candidate{};

    const int result = parent.proc->clone_address_space(candidate);
    if (result != SV39_OK) {
        return discard_fork_candidate(candidate);
    }

    asm volatile("fence.i" ::: "memory");

    const int new_id = task_create_user_from_frame(table, snapshot, candidate, parent.task_id);

    if (new_id < 0) {
        return discard_fork_candidate(candidate);
    }
    return new_id;
}

} // namespace tinyos::user_task
