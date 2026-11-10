#ifndef TINYOS_PROCESS_HPP
#define TINYOS_PROCESS_HPP

#include "mm/user_address_space.h"
#include "task/task_exit_info.h"

namespace tinyos {

inline constexpr int kNoParentId = -1;

class Process {
public:
    constexpr Process() = default;

    // 显式禁用拷贝(语义：禁止复制进程实体)
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    // 显式禁用移动(语义：进程栈与内部现场自指向，禁止搬移所有权)
    Process(Process&&) = delete;
    Process& operator=(Process&&) = delete;

    void adopt(UserAddressSpace& source) noexcept;
    const Sv39PageTable* root() const noexcept;

    void record_exit(TaskExitInfo res) noexcept;
    int release_resources();
    const TaskExitInfo& exit_info() const noexcept;
    int clone_address_space(UserAddressSpace& output) const;
    int parent_id() const noexcept;
    void set_parent_id(int parent_id) noexcept;

    bool resources_empty() const noexcept;
    int reset();

private:
    UserAddressSpace user_space_{};
    TaskExitInfo exit_info_{};
    int parent_id_{kNoParentId};
};

}

#endif