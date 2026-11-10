#include "mm/user_address_space.h"

#include "arch/riscv/vm_arch.h"
#include "mm/address.hpp"
#include "mm/pmm.h"

static bool valid_root(const Sv39PageTable* root) {
    if (root == nullptr) {
        return false;
    }

    const PhysAddr address(root);
    return address.is_valid() && address.is_page_aligned();
}

static bool user_address_space_memory_is_empty(
    const UserAddressSpace& space
) {
    return space.user_memory.root == nullptr &&
        space.user_memory.page_count == 0;
}

static bool shared_entry_is_valid(Sv39Pte entry) {
    // Borrowed branches contain only a physical page number and the V bit.
    return entry == sv39_make_pte(
        sv39_pte_physical_address(entry), SV39_PTE_V);
}

static bool kernel_template_is_valid(const Sv39PageTable* kernel_root) {
    if (!valid_root(kernel_root)) {
        return false;
    }

    for (size_t i = 0; i < SV39_PTE_COUNT; ++i) {
        const Sv39Pte entry = kernel_root->entries[i];
        if (i == 0 || i == 2) {
            if (!shared_entry_is_valid(entry)) {
                return false;
            }
        } else if (entry != 0) {
            return false;
        }
    }

    return true;
}

static bool user_address_space_layout_is_valid(
    const UserAddressSpace& space
) {
    if (!kernel_template_is_valid(space.kernel_root)) {
        return false;
    }

    for (size_t i = 0; i < SV39_PTE_COUNT; ++i) {
        const Sv39Pte entry = space.root->entries[i];
        if (i == 0 || i == 2) {
            if (entry != space.kernel_root->entries[i]) {
                return false;
            }
        } else if (i != USER_ADDRESS_SPACE_ROOT_INDEX && entry != 0) {
            return false;
        }
    }

    return true;
}

static bool user_address_space_memory_is_valid(
    const UserAddressSpace& space
) {
    const UserMemory& memory = space.user_memory;
    if (memory.page_count > USER_MEMORY_MAX_PAGE) {
        return false;
    }

    if (user_address_space_memory_is_empty(space)) {
        return true;
    }

    if (memory.root != space.root || memory.page_count == 0) {
        return false;
    }

    const uintptr_t user_start =
        uintptr_t{USER_ADDRESS_SPACE_ROOT_INDEX} << 30;
    const uintptr_t user_end = user_start + (uintptr_t{1} << 30);
    for (size_t i = 0; i < memory.page_count; ++i) {
        const UserMemoryPage& page = memory.pages[i];
        if (page.virtual_address < user_start ||
            page.virtual_address >= user_end ||
            page.physical_page == space.root ||
            page.physical_page == space.kernel_root) {
            return false;
        }
    }

    return true;
}

static bool user_address_space_root_is_active(const Sv39PageTable* root) {
    const uint64_t satp = sv39_read_satp();
    const uint64_t mode = satp >> 60;
    const uint64_t ppn_mask = (UINT64_C(1) << 44) - 1;
    const uint64_t current_root_ppn = satp & ppn_mask;
    const uint64_t expected_root_ppn =
        reinterpret_cast<uintptr_t>(root) >> SV39_PAGE_SHIFT;

    return mode == 8 && current_root_ppn == expected_root_ppn;
}

int user_address_space_create(
    const Sv39PageTable* kernel_root,
    UserAddressSpace* output
) {
    if (output == nullptr ||
        output->root != nullptr || output->kernel_root != nullptr ||
        !user_address_space_memory_is_empty(*output) ||
        !kernel_template_is_valid(kernel_root)) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    Sv39PageTable* root = sv39_create_page_table();
    if (root == nullptr) {
        return SV39_ERR_NO_MEMORY;
    }

    root->entries[0] = kernel_root->entries[0];
    root->entries[2] = kernel_root->entries[2];

    output->root = root;
    output->kernel_root = kernel_root;
    output->user_memory = UserMemory{};

    return SV39_OK;
}

int user_address_space_clone(
    const UserAddressSpace* source,
    UserAddressSpace* output
) {

    if (source == nullptr ||
        output == nullptr ||
        source == output ||
        output->kernel_root != nullptr ||
        output->root != nullptr ||
        output->user_memory.root != nullptr ||
        output->user_memory.page_count != 0) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    for (const UserMemoryPage& page : output->user_memory.pages) {
        if (page.physical_page != nullptr ||
            page.virtual_address != 0) {
                return  SV39_ERR_INVALID_ARGUMENT;
        }
    }

    if (!valid_root(source->kernel_root) ||
        !valid_root(source->root) ||
        source->root == source->kernel_root ||
        source->user_memory.root != source->root ||
        source->user_memory.page_count == 0) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    if (!user_address_space_layout_is_valid(*source) ||
        !user_address_space_memory_is_valid(*source) ||
        source->user_memory.validate_pages() != SV39_OK) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    uint64_t page_flags[USER_MEMORY_MAX_PAGE]{};
    
    for (size_t i = 0; i < source->user_memory.page_count; ++i) {
        const UserMemoryPage& page = source->user_memory.pages[i];
        uint64_t mapped_address = 0;

        if (page.physical_page == nullptr ||
            source->root->query(
                page.virtual_address,
                &mapped_address,
                &page_flags[i]) != SV39_OK ||
                mapped_address != reinterpret_cast<uintptr_t>(page.physical_page) ||
                (page_flags[i] & SV39_PTE_U) == 0) {
                return SV39_ERR_INVALID_ARGUMENT;
        }
    }

    const int result =
        user_address_space_create(source->kernel_root,output);

    if (result != SV39_OK) {
        return result;
    }

    for (size_t i = 0; i < source->user_memory.page_count; ++i) {
        const UserMemoryPage& source_page = source->user_memory.pages[i];

        void* new_page = pmm_alloc_page();

        if (new_page == nullptr) {
            return SV39_ERR_NO_MEMORY;
        }

        UserMemory& memory = output->user_memory;
        
        memory.root = output->root;
        memory.pages[memory.page_count] = UserMemoryPage {
            source_page.virtual_address,
            new_page
        };

        ++memory.page_count;
        const auto* source_bytes =
            static_cast<const unsigned char*>(source_page.physical_page);
        auto* destination_bytes =
            static_cast<unsigned char*>(new_page);

        for (size_t offset = 0; offset < SV39_PAGE_SIZE; ++offset) {
            destination_bytes[offset] = source_bytes[offset];
        }

        const int map_result = memory.root->map(
            source_page.virtual_address,
            reinterpret_cast<uintptr_t>(new_page),
            page_flags[i] & SV39_PTE_LEAF_FLAG_MASK);

        if (map_result != SV39_OK) {
            return map_result;
        }
    }

    return SV39_OK;
}

int user_address_space_release(UserAddressSpace* space) {
    if (space == nullptr) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    if (space->root == nullptr && space->kernel_root == nullptr &&
        user_address_space_memory_is_empty(*space)) {
        return SV39_OK;
    }

    if (!valid_root(space->root) || !valid_root(space->kernel_root) ||
        space->root == space->kernel_root) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    if (user_address_space_root_is_active(space->root)) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    if (!user_address_space_layout_is_valid(*space) ||
        !user_address_space_memory_is_valid(*space)) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    const int result = user_memory_release(&space->user_memory);
    if (result != SV39_OK) {
        return result;
    }

    if (space->root->entries[USER_ADDRESS_SPACE_ROOT_INDEX] != 0) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    // Keep the outer owner intact if returning the final root page fails.
    if (pmm_free_page(space->root) != 0) {
        return SV39_ERR_PAGE_FREE_FAILED;
    }

    *space = {};
    return SV39_OK;
}
