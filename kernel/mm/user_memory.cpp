#include "mm/user_memory.h"

#include <span>

#include "arch/riscv/vm_arch.h"
#include "mm/address.hpp"
#include "mm/pmm.h"


int UserMemory::validate_pages() const {
    if (page_count > USER_MEMORY_MAX_PAGE) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    const std::span<const UserMemoryPage> active_pages(pages, page_count);

    PageRange ranges[USER_MEMORY_MAX_PAGE]{};
    PhysPageRange physical_ranges[USER_MEMORY_MAX_PAGE]{};
    size_t physical_count = 0;

    for (size_t i = 0; i < active_pages.size(); ++i) {
        const UserMemoryPage& page = active_pages[i];
        const VirtAddr address(page.virtual_address);
        if (address.val() == 0 || !address.is_page_aligned() ||
            !PageRange::try_from_bytes(address, SV39_PAGE_SIZE, ranges[i])) {
            return SV39_ERR_INVALID_ARGUMENT;
        }

        // Cleared physical pointers belong to a release that can still retry.
        if (page.physical_page != nullptr) {
            const PhysAddr physical(page.physical_page);
            if (!physical.is_page_aligned() ||
                !PhysPageRange::try_from_bytes(
                    physical, SV39_PAGE_SIZE, physical_ranges[physical_count])) {
                return SV39_ERR_INVALID_ARGUMENT;
            }
            ++physical_count;
        }
    }

    const std::span<const PageRange> active_ranges(ranges, active_pages.size());
    const std::span<const PhysPageRange> owned_ranges(physical_ranges, physical_count);
    // Distinct VAs and unique physical ownership are independent requirements.
    if (!validate_no_overlap(active_ranges) || !validate_no_overlap(owned_ranges)) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    return SV39_OK;
}


int UserMemory::validate_mappings() const {
    if (page_count > USER_MEMORY_MAX_PAGE) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    if (root == nullptr && page_count == 0) {
        return SV39_OK;
    }

    if (root == nullptr || page_count == 0) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    const std::span<const UserMemoryPage> active_pages(pages, page_count);
    for (const UserMemoryPage& page : active_pages) {
        uint64_t mapped_address = 0;
        uint64_t flags = 0;

        const int result = root->query(
            page.virtual_address,
            &mapped_address,
            &flags
        );

        if (result == SV39_ERR_NOT_MAPPED) {
            continue;
        }
        if (result != SV39_OK) {
            return result;
        }

        const PhysAddr expected(page.physical_page);
        const PhysAddr actual(mapped_address);

        if (page.physical_page == nullptr ||
            actual.val() != expected.val() ||
            (flags & SV39_PTE_U) == 0) {
            return SV39_ERR_INVALID_ARGUMENT;
        }
    }

    return SV39_OK;
}

int UserMemory::release() {
    int result = validate_pages();
    if (result != SV39_OK) {
        return result;
    }

    result = validate_mappings();
    if (result != SV39_OK) {
        return result;
    }

    if (page_count == 0) {
        return SV39_OK;
    }

    const std::span<UserMemoryPage> active_pages(pages, page_count);

    for (const UserMemoryPage& page : active_pages) {
        const int result =
            sv39_unmap_page(root, page.virtual_address);

        if (result != SV39_OK &&
            result != SV39_ERR_NOT_MAPPED) {
            return result;
        }
    }


    for (const UserMemoryPage& page : active_pages) {
        if (sv39_flush_page(page.virtual_address) != SV39_OK) {
            return SV39_ERR_INVALID_ARGUMENT;
        }
    }


    for (UserMemoryPage& page : active_pages) {
        if (page.physical_page != nullptr) {
            if (pmm_free_page(page.physical_page) != 0) {
                return SV39_ERR_PAGE_FREE_FAILED;
            }
            // Clear the owning member immediately so a failed release can retry.
            page.physical_page = nullptr;
        }
    }

    for (const UserMemoryPage& page : active_pages) {
        const int result =
            root->reclaim_empty_tables(page.virtual_address);
        if (result != SV39_OK &&
            result != SV39_ERR_NOT_MAPPED) {
            return result;
        }
    }

    *this = {};

    return SV39_OK;
}

extern "C" {
    int user_memory_release(UserMemory *memory) {
        if (memory == nullptr) {
            return SV39_ERR_INVALID_ARGUMENT;
        }

        return memory->release();
    }

const UserMemoryPage* user_memory_find_page(
    const UserMemory* memory,
    uintptr_t page_address
    ) {
    if (memory == nullptr || memory->root == nullptr) {
        return nullptr;
    }

    if (memory->page_count == 0 || memory->page_count > USER_MEMORY_MAX_PAGE) {
        return nullptr;
    }
    const VirtAddr address(page_address);
    if (page_address == 0 || !address.is_valid() || !address.is_page_aligned()) {
        return nullptr;
    }

    for (size_t i = 0; i < memory->page_count; ++i) {
        const UserMemoryPage& page = memory->pages[i];

        if (page.virtual_address == page_address) {
            return &page;
        }
    }

    return nullptr;
    }
}
