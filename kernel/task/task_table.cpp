#include "task/private/task_table.hpp"

namespace tinyos {
int TaskTable::reset() {
    for (const Process& process : process_) {
        if (!process.resources_empty()) {
            return -1;
        }
    }

    for (int i = 0; i < kCapacity; ++i) {
        if (process_[i].reset() != 0) {
            return -1;
        }

        tasks_[i].clear_metadata();
        tasks_[i].state = TASK_UNUSED;
    }

    high_water_ = 0;
    next_task_id_ = 0;
    return 0;
}

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

int TaskTable::slot_of(const task* target) const noexcept {
    if (target == nullptr) {
        return -1;
    }

    for (int i = 0; i < kCapacity; ++i) {
        if (target == &tasks_[i]) {
            return i;
        }
    }

    return -1;
}

int TaskTable::high_water() const noexcept{
    return high_water_;
}

task* TaskTable::at(int slot_index) noexcept {
    if (slot_index < 0 || slot_index >= kCapacity) {
        return nullptr;
    }

    return &tasks_[slot_index];
}

const task* TaskTable::at(int slot_index) const noexcept {
    if (slot_index < 0 || slot_index >= kCapacity) {
        return nullptr;
    }

    return &tasks_[slot_index];
}

Process* TaskTable::process_at(int slot_index) noexcept {
    if (slot_index < 0 || slot_index >= kCapacity) {
        return nullptr;
    }

    return &process_[slot_index];
}
const Process* TaskTable::process_at(int slot_index) const noexcept {
    if (slot_index < 0 || slot_index >= kCapacity) {
        return nullptr;
    }

    return &process_[slot_index];
}

int TaskTable::recycle_dead_slot(int slot_index) {
    if (slot_index < 0 || slot_index >= high_water_) {
        return -1;
    }

    task& target = tasks_[slot_index];
    if (target.state != TASK_DEAD) {
        return -1;
    }

    if (process_[slot_index].reset() != 0) {
        return -1;
    }

    target.clear_metadata();
    target.state = TASK_UNUSED;

    return 0;
    return 0;
}

int TaskTable::allocate(task** task_out) {
    if (task_out == nullptr ||
        next_task_id_ > (uint64_t)__INT_MAX__) {
        return -1;
    }

    int slot_index = -1;
    for (int i = 0; i < high_water_; ++i) {
        if (tasks_[i].state == TASK_UNUSED) {
            slot_index = i;
            break;
        }
    }

    if (slot_index < 0) {
        if (high_water_ >= kCapacity) {
            return -1;
        }
        slot_index = high_water_;
    }

    if (process_[slot_index].reset() != 0) {
        return -1;
    }

    task& target = tasks_[slot_index];
    target.clear_metadata();
    riscv_task_context_init(&target.context);

    const int task_id = static_cast<int>(next_task_id_);
    target.task_id = task_id;
    target.state = TASK_READY;

    ++next_task_id_;
    if (slot_index == high_water_) {
        ++high_water_;
    }

    *task_out = &target;
    return task_id;
}

}