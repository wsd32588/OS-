// Included only in the isolated stress copy of sched.cpp.
#include "mm/pmm.h"
#include "task/syscall.h"

namespace {
unsigned long stress_allocations, stress_frees, stress_base_allocations, stress_base_frees;
unsigned long stress_available, stress_rounds, stress_full_batches;
bool stress_started, stress_finished, stress_root_exiting;
task* stress_root;
int stress_root_id;
task* stress_tasks[tinyos::TaskTable::kCapacity];
tinyos::Process* stress_processes[tinyos::TaskTable::kCapacity];
void* stress_live_pages[64];

[[noreturn]] void stress_observer_fail(const char* reason) {
    uart_puts("[stress-observer] FAIL ");
    uart_puts(reason);
    uart_putchar('\n');
    for (;;) { asm volatile("wfi"); }
}

void stress_check(bool condition, const char* reason) {
    if (!condition) { stress_observer_fail(reason); }
}

void stress_print_count(const char* text, unsigned long value) {
    uart_puts(text);
    uart_put_hex(value);
    uart_putchar('\n');
}

void stress_check_storage(void) {
    for (int i = 0; i < tinyos::TaskTable::kCapacity; ++i) {
        stress_check(g_task_table.at(i) == stress_tasks[i] &&
                     g_task_table.process_at(i) == stress_processes[i], "stable table storage");
    }
}

void stress_checkpoint(unsigned long phase, const trap_frame* frame) {
    task* current = current_user_task_metadata();
    stress_check(current != nullptr && supervisor_interrupts_disabled(), "USER checkpoint SIE0");
    if (phase == 0x1d) {
        stress_check(!stress_started && current->proc->parent_id() == tinyos::kNoParentId &&
                     current->task_id == g_user_reaper_id, "initial reaper");
        stress_root = current;
        stress_root_id = current->task_id;
        stress_base_allocations = stress_allocations;
        stress_base_frees = stress_frees;
        stress_available = pmm_available_pages();
        for (int i = 0; i < tinyos::TaskTable::kCapacity; ++i) {
            stress_tasks[i] = g_task_table.at(i);
            stress_processes[i] = g_task_table.process_at(i);
            stress_check(stress_tasks[i] == current || stress_tasks[i]->state == TASK_UNUSED,
                         "baseline has only reaper");
        }
        stress_started = true;
        uart_puts("[stress-observer] baseline ready\n");
        return;
    }
    stress_check(stress_started && current == stress_root && current->task_id == stress_root_id,
                 "same root identity");
    const uintptr_t frame_address = reinterpret_cast<uintptr_t>(frame);
    const uintptr_t stack_begin = reinterpret_cast<uintptr_t>(current->context.stack);
    stress_check(frame_address >= stack_begin &&
                 frame_address + sizeof(*frame) <= stack_begin + TINYOS_KERNEL_STACK_SIZE,
                 "current frame remains on root kernel stack");
    stress_check_storage();
    if (phase == 0x1c) {
        stress_check(stress_full_batches == stress_rounds &&
                     g_task_table.high_water() == tinyos::TaskTable::kCapacity, "full cohort order");
        for (int i = 0; i < tinyos::TaskTable::kCapacity; ++i) {
            const task* item = g_task_table.at(i);
            stress_check(item->state != TASK_UNUSED && item->kind == TASK_KIND_USER &&
                         item->proc == g_task_table.process_at(i), "five real USER records");
            stress_check(item == stress_root || item->proc->parent_id() == stress_root_id,
                         "worker belongs to actual root");
        }
        ++stress_full_batches;
        return;
    }
    if (phase == 0x1e) {
        ++stress_rounds;
        stress_check(stress_full_batches == stress_rounds &&
                     g_task_table.high_water() == tinyos::TaskTable::kCapacity,
                     "one full cohort per round and high water retained");
        for (int i = 0; i < tinyos::TaskTable::kCapacity; ++i) {
            const task* item = g_task_table.at(i);
            if (item == stress_root) { continue; }
            const tinyos::Process* process = g_task_table.process_at(i);
            stress_check(item->state == TASK_UNUSED && item->task_id == -1 && item->proc == nullptr &&
                         item->context.frame == nullptr && process->resources_empty() &&
                         process->parent_id() == tinyos::kNoParentId &&
                         process->exit_info().reason == TASK_EXIT_NONE &&
                         process->exit_info().status == 0 && process->exit_info().fault_scause == 0,
                         "all worker records fully recycled");
        }
        for (void* page : stress_live_pages) { stress_check(page == nullptr, "no leaked runtime page"); }
        const unsigned long expected = stress_rounds * 35 + ((stress_rounds + 3) / 4) * 7;
        stress_check(stress_allocations - stress_base_allocations == expected &&
                     stress_frees - stress_base_frees == expected &&
                     pmm_available_pages() == stress_available, "per-round PMM balance");
        if (stress_rounds % 25 == 0) { stress_print_count("[stress-observer] rounds=", stress_rounds); }
        return;
    }
    stress_check(phase == 0x1f && stress_rounds == TEST_STRESS_ROUNDS &&
                 stress_full_batches == stress_rounds, "all configured rounds completed");
    stress_finished = true;
}
}

extern "C" void* __real_pmm_alloc_page(void);
extern "C" int __real_pmm_free_page(void*);
extern "C" trap_frame* __real_syscall_handle(trap_frame*);

extern "C" void* __wrap_pmm_alloc_page(void) {
    void* page = __real_pmm_alloc_page();
    if (page == nullptr) { stress_observer_fail("unexpected physical allocation failure"); }
    ++stress_allocations;
    if (stress_started) {
        unsigned int i = 0;
        while (i < 64 && stress_live_pages[i] != nullptr) {
            stress_check(stress_live_pages[i] != page, "allocated PA already live");
            ++i;
        }
        stress_check(i < 64, "observer page capacity");
        // Check the tail too: a previously freed bookkeeping slot can be reused.
        for (unsigned int j = i + 1; j < 64; ++j) {
            stress_check(stress_live_pages[j] != page, "allocated PA duplicated in tail");
        }
        stress_live_pages[i] = page;
    }
    return page;
}

extern "C" int __wrap_pmm_free_page(void* page) {
    unsigned int position = 64;
    if (stress_started && !stress_root_exiting) {
        for (unsigned int i = 0; i < 64; ++i) {
            if (stress_live_pages[i] == page) { position = i; break; }
        }
        stress_check(position < 64, "free targets a live runtime PA exactly once");
    }
    const int result = __real_pmm_free_page(page);
    stress_check(result == SV39_OK, "physical free succeeded");
    ++stress_frees;
    if (position < 64) { stress_live_pages[position] = nullptr; }
    return result;
}

extern "C" trap_frame* __wrap_syscall_handle(trap_frame* frame) {
    const unsigned long number = frame->a7;
    const unsigned long argument = frame->a0;
    const bool root_exit = stress_started && number == SYSCALL_EXIT &&
                           current_user_task_metadata() == stress_root;
    if (number == SYSCALL_PUTCHAR && argument >= 0x1c && argument <= 0x1f) {
        stress_checkpoint(argument, frame);
    }
    if (root_exit) {
        stress_check(stress_finished && argument == 0, "root exits after all cohorts");
        stress_root_exiting = true;
    }
    trap_frame* result = __real_syscall_handle(frame);
    if (root_exit) {
        stress_check(stress_root->state == TASK_DEAD && stress_root->proc->resources_empty() &&
                     pmm_available_pages() == stress_available + 7, "final root resources released");
        const unsigned long expected = TEST_STRESS_ROUNDS * 35 + ((TEST_STRESS_ROUNDS + 3) / 4) * 7;
        stress_check(stress_allocations - stress_base_allocations == expected &&
                     stress_frees - stress_base_frees == expected + 7, "final allocation/free totals");
        stress_print_count("[stress-observer] PASS rounds=", stress_rounds);
        stress_print_count("[stress-observer] runtime_allocations=", expected);
        stress_print_count("[stress-observer] runtime_frees=", expected + 7);
    }
    return result;
}
