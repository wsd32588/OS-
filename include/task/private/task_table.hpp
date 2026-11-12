#pragma once

#include "task/private/task.hpp"

namespace tinyos {
class TaskTable {
    public:
    static constexpr int kCapacity = 5;
    constexpr TaskTable() = default;

    TaskTable(const TaskTable&) = delete;
    TaskTable& operator=(const TaskTable&) = delete;
    TaskTable(TaskTable&&) = delete;
    TaskTable& operator=(TaskTable&&) = delete;

    int reset();
    task* at(int slot_index) noexcept;
    const task* at(int slot_index) const noexcept;
    Process* process_at(int slot_index) noexcept;
    const Process* process_at(int slot_index) const noexcept;

    int high_water() const noexcept;
    int find_slot(int task_id) const noexcept;
    int slot_of(const task* target) const noexcept;
    int allocate(task** task_out);
    int recycle_dead_slot(int slot_index);

    private:
    task tasks_[kCapacity]{};
    Process process_[kCapacity]{};
    int high_water_{};
    uint64_t next_task_id_{};
};
}