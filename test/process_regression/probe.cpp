#include "process_regression_probe.hpp"

#include "arch/riscv/vm_arch.h"
#include "drivers/uart.h"
#include "mm/pmm.h"
#include "task/builtin_program.h"
#include "task/syscall.h"

namespace {
unsigned long allocations, frees, boot_allocations, boot_frees, boot_available;
unsigned releases, reaps, spawns, forks, execs, ticks;
unsigned file_calls[4]{};
unsigned free_mask;
void* owned_pages[7]{};
uint64_t kernel_satp;
bool releasing;

void check(bool condition, const char* reason) {
    if (!condition) {
        uart_puts("[process regression] FAIL ");
        uart_puts(reason);
        uart_putchar('\n');
        for (;;) {
            asm volatile("wfi");
        }
    }
}
void pass(const char* name) {
    uart_puts("[process regression] PASS ");
    uart_puts(name);
    uart_putchar('\n');
}
void gather(const Sv39PageTable* root) {
    check(root != nullptr, "release has an owned root");
    const auto* l1 = reinterpret_cast<const Sv39PageTable*>(
        sv39_pte_physical_address(root->entries[USER_ADDRESS_SPACE_ROOT_INDEX]));
    unsigned contents = 0, tables = 0;
    for (size_t i = 0; i < SV39_PTE_COUNT; ++i) {
        if (l1->entries[i] == 0) {
            continue;
        }
        check(++tables == 1, "one private L0 table");
        const auto* l0 = reinterpret_cast<const Sv39PageTable*>(sv39_pte_physical_address(l1->entries[i]));
        for (size_t j = 0; j < SV39_PTE_COUNT; ++j) {
            if (l0->entries[j] != 0) {
                check(contents < 4 && sv39_pte_is_leaf(l0->entries[j]), "four owned content pages");
                owned_pages[contents++] = reinterpret_cast<void*>(sv39_pte_physical_address(l0->entries[j]));
            }
        }
        owned_pages[4] = const_cast<Sv39PageTable*>(l0);
    }
    owned_pages[5] = const_cast<Sv39PageTable*>(l1);
    owned_pages[6] = const_cast<Sv39PageTable*>(root);
    check(contents == 4 && tables == 1, "seven pages per image");
    for (unsigned i = 0; i < 7; ++i) {
        for (unsigned j = i + 1; j < 7; ++j) {
            check(owned_pages[i] != owned_pages[j], "seven distinct owned pages");
        }
    }
}
}

extern "C" void* __real_pmm_alloc_page();
extern "C" int __real_pmm_free_page(void*);
extern "C" int __real_user_address_space_release(UserAddressSpace*);
extern "C" int __real_task_reap(int);
extern "C" int __real_process_spawn_builtin(uint64_t, uintptr_t);
extern "C" int __real_sched_fork_current(trap_frame*);
extern "C" int __real_sched_exec_current(trap_frame*, uint64_t, uintptr_t);
extern "C" trap_frame* __real_syscall_handle(trap_frame*);

extern "C" void* __wrap_pmm_alloc_page() {
    ++allocations;
    return __real_pmm_alloc_page();
}
extern "C" int __wrap_pmm_free_page(void* page) {
    ++frees;
    if (releasing) {
        check(sv39_read_satp() == kernel_satp, "free under the kernel root");
        unsigned index = 0;
        while (index < 7 && owned_pages[index] != page) {
            ++index;
        }
        check(index < 7 && !(free_mask & (1U << index)), "free each owned page exactly once");
        if (index == 6) {
            check(free_mask == 0x3f, "private root freed last");
        }
        free_mask |= 1U << index;
    }
    const int result = __real_pmm_free_page(page);
    check(result == 0, "legal physical-page free");
    return result;
}
extern "C" int __wrap_user_address_space_release(UserAddressSpace* space) {
    if (space == nullptr || space->root == nullptr) {
        return __real_user_address_space_release(space);
    }
    check(releases < 5, "five image lifetimes");
    const unsigned stage = releases;
    const task* owner = regression_task(stage == 4 ? 3 : 2);
    const int id = stage == 0 ? 2 : stage == 1 ? 4 : stage < 4 ? 5 : 3;
    check(owner != nullptr && owner->task_id == id && owner->proc->root() == space->root,
          "release matches its actual task and root");
    check(owner->proc->parent_id() == (stage == 4 ? -1 : 3), "release preserves the process parent");
    check(regression_service_in_use() == 0, "file references closed before final image release");
    const auto a = allocations, f = frees, available = pmm_available_pages();
    gather(space->root);
    free_mask = 0;
    releasing = true;
    const int result = __real_user_address_space_release(space);
    releasing = false;
    check(result == SV39_OK && free_mask == 0x7f && allocations == a && frees == f + 7 &&
          pmm_available_pages() == available + 7 && owner->proc->resources_empty(),
          "image release restores exactly seven pages");
    ++releases;
    static const char* names[] = {"release U0 seven pages", "release SPAWN4 seven pages",
        "release EXEC-old5 seven pages", "release EXEC-new5 seven pages", "release root3 seven pages"};
    pass(names[stage]);
    return result;
}
extern "C" int __wrap_task_reap(int id) {
    check(reaps < 3 && id == (reaps == 0 ? 2 : reaps == 1 ? 4 : 5), "three exact child IDs reaped");
    const auto a = allocations, f = frees, available = pmm_available_pages();
    const int result = __real_task_reap(id);
    const task* recycled = regression_task(2);
    check(result == 0 && allocations == a && frees == f && pmm_available_pages() == available &&
          recycled->state == TASK_UNUSED && recycled->task_id == -1 && recycled->proc == nullptr,
          "reap resets slot without physical-page changes");
    ++reaps;
    pass("reap zero PMM");
    return result;
}
extern "C" int __wrap_process_spawn_builtin(uint64_t program, uintptr_t argument) {
    const auto a = allocations, f = frees;
    const int result = __real_process_spawn_builtin(program, argument);
    if (program == USER_PROGRAM_WORKER) {
        check(++spawns == 1 && result == 4 && argument == 3 && allocations == a + 7 && frees == f,
              "SPAWN4 allocates seven pages");
        pass("SPAWN4 seven allocations");
    } else {
        check(program == UINT64_MAX && result == -1 && allocations == a && frees == f,
              "unknown SPAWN is rejected without PMM");
    }
    return result;
}
extern "C" int __wrap_sched_fork_current(trap_frame* frame) {
    const auto a = allocations, f = frees;
    const int result = __real_sched_fork_current(frame);
    check(++forks == 1 && result == 5 && allocations == a + 7 && frees == f,
          "FORK5 allocates seven pages");
    pass("FORK5 seven allocations");
    return result;
}
extern "C" int __wrap_sched_exec_current(trap_frame* frame, uint64_t program, uintptr_t argument) {
    const auto a = allocations, f = frees, available = pmm_available_pages();
    const int result = __real_sched_exec_current(frame, program, argument);
    if (program == USER_PROGRAM_WORKER) {
        check(++execs == 1 && result == 0 && argument == 17 && allocations == a + 7 &&
              frees == f + 7 && pmm_available_pages() == available, "EXEC5 replaces seven pages");
        pass("EXEC5 seven allocations and frees");
    } else {
        check(program == UINT64_MAX && result == -1 && allocations == a && frees == f,
              "unknown EXEC is rejected without PMM");
    }
    return result;
}
extern "C" trap_frame* __wrap_syscall_handle(trap_frame* frame) {
    const bool file_operation = frame->a7 >= SYSCALL_READ_FILE && frame->a7 <= SYSCALL_CLOSE;
    const unsigned index = static_cast<unsigned>(frame->a7 - SYSCALL_READ_FILE);
    const auto a = allocations, f = frees, available = pmm_available_pages();
    trap_frame* result = __real_syscall_handle(frame);
    if (file_operation) {
        check(result == frame && allocations == a && frees == f && pmm_available_pages() == available,
              "file syscalls preserve PMM ownership");
        ++file_calls[index];
    }
    return result;
}
extern "C" void regression_after_boot() {
    kernel_satp = sv39_read_satp();
    boot_allocations = allocations;
    boot_frees = frees;
    boot_available = pmm_available_pages();
    check(regression_task(2)->task_id == 2 && regression_task(2)->proc->parent_id() == 3 &&
          regression_task(3)->task_id == 3 && regression_task(3)->proc->parent_id() == -1,
          "two bootstrap users have the original parent relation");
    pass("boot two S and two U");
}
extern "C" void regression_on_timer(const trap_frame* frame) {
    if (releases != 5 || !(frame->sstatus & (1UL << 8))) {
        return;
    }
    check(reaps == 3 && spawns == 1 && forks == 1 && execs == 1 &&
          frees == boot_frees + 35 && allocations == boot_allocations + 21 &&
          pmm_available_pages() == boot_available + 14 && regression_service_in_use() == 0,
          "final resource totals and empty file pool");
    check(file_calls[0] == 4 && file_calls[1] == 4 && file_calls[2] == 6 && file_calls[3] == 2,
          "original READ_FILE and FD syscall counts");
    if (++ticks == 2) {
        pass("final free35 alloc21 net14 reap3 pool0 two S ticks");
    }
}
