#include "mm/user_loader.hpp"
#include "mm/pmm.h"
#include "user/user_image.h"
#include "arch/riscv/vm_arch.h"
#include "mm/address.hpp"

#include <span>

namespace tinyos {
int load_user_elf(
    Sv39PageTable& root,
    std::span<const unsigned char> image,
    const UserElfLayout& elf_layout,
    const UserMemoryLayout& memory_layout,
    UserMemory& memory_out
) {
    if (image.empty() ||
        elf_layout.load_segment_count != USER_ELF_MAX_LOAD_SEGMENTS) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    const VirtAddr page_starts[] = {
        VirtAddr(memory_layout.code_address),
        VirtAddr(memory_layout.stack_address),
        VirtAddr(memory_layout.data_address),
    };

    for (const VirtAddr& start : page_starts) {
        VirtAddr last_byte;

        if (start.val() == 0 ||
            !start.is_page_aligned() ||
            !start.try_add(SV39_PAGE_SIZE - 1, last_byte)) {
            return SV39_ERR_INVALID_ARGUMENT;
        }
    }

    VirtAddr stack_top;
    if (!VirtAddr(memory_layout.stack_address)
            .try_add(SV39_PAGE_SIZE, stack_top)) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    if (memory_layout.code_address == memory_layout.stack_address ||
        memory_layout.code_address == memory_layout.data_address ||
        memory_layout.stack_address == memory_layout.data_address) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    const UserElfSegment* code_segment = nullptr;
    const UserElfSegment* data_segment = nullptr;

    for (size_t i = 0; i < elf_layout.load_segment_count; ++i) {
        const UserElfSegment* segment = &elf_layout.segments[i];

        if (segment->memory_size <= 0 ||
            segment->memory_size > SV39_PAGE_SIZE) {
            return SV39_ERR_INVALID_ARGUMENT;
        }
        if (segment->file_size > SV39_PAGE_SIZE) {
            return SV39_ERR_INVALID_ARGUMENT;
        }

        /* 按 VA 找段，不依赖数组顺序；重复段/未知段/权限不符都拒绝。 */

        if (segment->virtual_address == memory_layout.code_address) {
            if (code_segment != nullptr || segment->flags != 5U) {   /* R|X */
                return SV39_ERR_INVALID_ARGUMENT;
            }
            code_segment = segment;
        } else if (segment->virtual_address == memory_layout.data_address) {
            if (data_segment != nullptr || segment->flags != 6U) {   /* R|W */
                return SV39_ERR_INVALID_ARGUMENT;
            }
            data_segment = segment;
        } else {
            return SV39_ERR_INVALID_ARGUMENT;
        }
    }

    if (code_segment == nullptr || data_segment == nullptr) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
    
    /* 段字段校验：大小、文件范围、入口。 */
    if (code_segment->memory_size == 0 ||
        code_segment->memory_size > SV39_PAGE_SIZE ||
        data_segment->memory_size == 0 ||
        data_segment->memory_size > SV39_PAGE_SIZE ||
        code_segment->file_size > code_segment->memory_size ||
        data_segment->file_size > data_segment->memory_size ||
        code_segment->file_offset > image.size() ||
        code_segment->file_size > image.size() - code_segment->file_offset ||
        data_segment->file_offset > image.size() ||
        data_segment->file_size > image.size() - data_segment->file_offset) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    uintptr_t entry = (uintptr_t)elf_layout.entry_address;
    if (entry < memory_layout.code_address ||
        (entry & 1U) != 0 ||
        entry - memory_layout.code_address >= code_segment->file_size) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    constexpr uint64_t user_code_flags =
        SV39_PTE_R | SV39_PTE_X | SV39_PTE_U | SV39_PTE_A;
    constexpr uint64_t user_writable_flags =
        SV39_PTE_R | SV39_PTE_W | SV39_PTE_U |
        SV39_PTE_A | SV39_PTE_D;

    struct PreparedPage {
        uintptr_t virtual_address;
        uint64_t flags;
        const UserElfSegment* segment;
        void* physical_page = nullptr;
        bool mapping_attempted = false;
        bool mapped = false;
    };
    PreparedPage pages[] = {
        {memory_layout.code_address, user_code_flags, code_segment},
        {memory_layout.stack_address, user_writable_flags, nullptr},
        {memory_layout.data_address, user_writable_flags, data_segment},
    };
    // Preserve the existing rollback order separately from allocation order.
    constexpr size_t rollback_order[] = {1, 0, 2};
    int result = SV39_OK;

    for (PreparedPage& page : pages) {
        page.physical_page = pmm_alloc_page();
        if (page.physical_page == nullptr) {
            result = SV39_ERR_NO_MEMORY;
            goto clean_up;
        }
    }

    for (const PreparedPage& page : pages) {
        // The user stack has no ELF segment; PMM already cleared its page.
        if (page.segment == nullptr) {
            continue;
        }
        const int load_result = user_image_load_segment(
            page.physical_page, SV39_PAGE_SIZE, image.data(), image.size(),
            static_cast<size_t>(page.segment->file_offset),
            static_cast<size_t>(page.segment->file_size),
            static_cast<size_t>(page.segment->memory_size));
        if (load_result != SV39_OK) {
            result = SV39_ERR_INVALID_ARGUMENT;
            goto clean_up;
        }
    }

    asm volatile("fence.i" ::: "memory");

    for (PreparedPage& page : pages) {
        page.mapping_attempted = true;
        result = sv39_map_page(
            &root,
            page.virtual_address,
            reinterpret_cast<uintptr_t>(page.physical_page), page.flags);
        if (result != SV39_OK) {
            goto clean_up;
        }
        page.mapped = true;
    }

    for (const PreparedPage& page : pages) {
        result = sv39_flush_page(page.virtual_address);
        if (result != SV39_OK) {
            goto clean_up;
        }
    }

    memory_out = UserMemory{
        .root = &root,
        .code_page = pages[0].physical_page,
        .stack_page = pages[1].physical_page,
        .code_address = memory_layout.code_address,
        .stack_address = memory_layout.stack_address,
        .data_page = pages[2].physical_page,
        .data_address = memory_layout.data_address,
    };

    return SV39_OK;
clean_up:
    for (size_t index : rollback_order) {
        const PreparedPage& page = pages[index];
        if (page.mapped) {
            const int error = sv39_unmap_page(&root, page.virtual_address);
            if (error != SV39_OK && error != SV39_ERR_NOT_MAPPED) {
                return error;
            }
        }
    }

    for (size_t index : rollback_order) {
        const PreparedPage& page = pages[index];
        if (page.mapping_attempted) {
            const int error = sv39_flush_page(page.virtual_address);
            if (error != SV39_OK) {
                return error;
            }
        }
    }

    for (PreparedPage& page : pages) {
        if (page.physical_page != nullptr) {
            if (pmm_free_page(page.physical_page) != 0) {
                return SV39_ERR_PAGE_FREE_FAILED;
            }
            page.physical_page = nullptr;
        }
    }

    for (size_t index : rollback_order) {
        const PreparedPage& page = pages[index];
        if (page.mapping_attempted) {
            const int error =
                root.reclaim_empty_tables(page.virtual_address);
            if (error != SV39_OK && error != SV39_ERR_NOT_MAPPED) {
                return error;
            }
        }
    }

    return result;
}
}

extern "C" {
int user_memory_load_elf(
    Sv39PageTable* root,
    const void* image,
    size_t image_size,
    const UserElfLayout* elf_layout,
    const UserMemoryLayout* memory_layout,
    UserMemory* memory_out
) {
    if(root == nullptr ||
        image == nullptr ||
        elf_layout == nullptr ||
        memory_out == nullptr ||
        elf_layout->load_segment_count != USER_ELF_MAX_LOAD_SEGMENTS ||
        image_size == 0 ||
        memory_layout == nullptr
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    return tinyos::load_user_elf(
        *root,
        std::span<const unsigned char>(
            static_cast<const unsigned char*>(image),
            image_size
        ),
        *elf_layout,
        *memory_layout,
        *memory_out
    );
}
}