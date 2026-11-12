#ifndef TINYOS_TEST_TASK_FACTORY_HPP
#define TINYOS_TEST_TASK_FACTORY_HPP

#include "task/private/task_table.hpp"

#include "arch/riscv/trap.h"
#include "test_support.hpp"

namespace tinyos_test {

struct TestTask {
    int id;
    int slot;
    task* execution;
    tinyos::Process* process;
};

// This factory creates table records with empty user spaces. It never bypasses
// allocation/recycling, and does not claim to execute real user processes.
class TaskFactory {
public:
    TaskFactory() {
        require_equal(table_.reset(), 0, "initialize test task table");
    }

    TestTask create(int parent_id, unsigned long argument) {
        task* execution = nullptr;
        const int id = table_.allocate(&execution);
        require(id >= 0 && execution != nullptr, "allocate a test task record");
        const int slot = table_.slot_of(execution);
        tinyos::Process* process = table_.process_at(slot);
        require(process != nullptr, "bind the matching Process storage");
        process->set_parent_id(parent_id);
        execution->proc = process;
        execution->kind = TASK_KIND_USER;
        execution->context.frame->a0 = argument;
        return {id, slot, execution, process};
    }

    void finish(const TestTask& target, long status) {
        require_equal(table_.find_slot(target.id), target.slot,
                      "finish targets the current public identity");
        require(target.process->resources_empty(),
                "metadata factory owns no physical user resources");
        target.process->record_exit({TASK_EXIT_REQUESTED, status, 0});
        target.execution->state = TASK_DEAD;
    }

    void reap(const TestTask& target) {
        require_equal(table_.find_slot(target.id), target.slot,
                      "reap targets the current public identity");
        require_equal(table_.recycle_dead_slot(target.slot), 0,
                      "recycle a completed test record");
    }

    tinyos::TaskTable& table() noexcept { return table_; }

private:
    tinyos::TaskTable table_{};
};

} // namespace tinyos_test

#endif
