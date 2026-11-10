#include "process/process.hpp"
#include "mm/user_address_space.h"

namespace tinyos {

    const Sv39PageTable* Process::root() const noexcept {
        return user_space_.root;
    }

    const TaskExitInfo& Process::exit_info() const noexcept {
        return exit_info_;   
    }

    bool Process::resources_empty() const noexcept {
        if (user_space_.root != nullptr ||
            user_space_.kernel_root != nullptr ||
            user_space_.user_memory.root != nullptr ||
            user_space_.user_memory.page_count != 0
        ) {
            return false;
        }

        for (const UserMemoryPage& page : user_space_.user_memory.pages) {
            if (page.physical_page != nullptr ||
                page.virtual_address != 0
            ) {
                return false;
            }
        }
        
        return true;
    }


    int Process::clone_address_space(UserAddressSpace& output) const {
        return user_address_space_clone(&user_space_, &output);
    }

    int Process::parent_id() const noexcept {
        return parent_id_;
    }

    void Process::set_parent_id(int parent_id) noexcept {
        parent_id_ = parent_id;
    }

    int Process::reset() {
        if (!resources_empty()) {
            return -1;
        }
        user_space_ = {};
        exit_info_ = {};
        parent_id_ = {kNoParentId};
        return 0;
    }
    void Process::adopt(UserAddressSpace& source) noexcept {
        user_space_ = source;
        source = {};
    }

    void Process::record_exit(TaskExitInfo res) noexcept {
        exit_info_ = res;
    }

    int Process::release_resources() {
        int res = user_address_space_release(&user_space_);
     
        return res;
    }
}