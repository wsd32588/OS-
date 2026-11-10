#include "mm/user_loader.hpp"
#include "mm/pmm.h"
#include "user/user_image.hpp"
#include "arch/riscv/vm_arch.h"
#include "mm/address.hpp"
#include "mm/user_address_space.h"

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

    const UserElfSegment* code_segment = nullptr;
    const UserElfSegment* data_segment = nullptr;

    for (size_t i = 0; i < elf_layout.load_segment_count; ++i) {
        const UserElfSegment* segment = &elf_layout.segments[i];

        if (segment->memory_size == 0) {
            return SV39_ERR_INVALID_ARGUMENT;
        }

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
    if (code_segment->memory_size > SV39_PAGE_SIZE ||
        code_segment->file_size > code_segment->memory_size ||
        data_segment->file_size > data_segment->memory_size ||
        code_segment->file_offset > image.size() ||
        code_segment->file_size > image.size() - code_segment->file_offset ||
        data_segment->file_offset > image.size() ||
        data_segment->file_size > image.size() - data_segment->file_offset) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    const uint64_t bytes = data_segment->memory_size;
    const uint64_t count = bytes / SV39_PAGE_SIZE +
        (bytes % SV39_PAGE_SIZE != 0);
    if (count > USER_MEMORY_MAX_PAGE - 2) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
    const size_t data_page_count = static_cast<size_t>(count);
    const size_t total_pages = 2 + data_page_count;

    PageRange ranges[3]{};

    if (!PageRange::try_from_bytes(VirtAddr(memory_layout.code_address),
        SV39_PAGE_SIZE, ranges[0]) ||
        !PageRange::try_from_bytes(VirtAddr(memory_layout.stack_address),
        SV39_PAGE_SIZE, ranges[1]) ||
        !PageRange::try_from_bytes(VirtAddr(memory_layout.data_address),
        static_cast<size_t>(data_segment->memory_size),
        ranges[2]) ||
        !validate_no_overlap(ranges)
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    for (const PageRange& range : ranges) {
        const uintptr_t first = range.start.to_addr().val();
        const uintptr_t last = range.end.to_addr().val() - 1;

        if (sv39_vpn_index(first, 2) != USER_ADDRESS_SPACE_ROOT_INDEX ||
            sv39_vpn_index(last, 2) != USER_ADDRESS_SPACE_ROOT_INDEX
        ) {
            return SV39_ERR_INVALID_ARGUMENT;
        }
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
    PreparedPage prepare_pages[USER_MEMORY_MAX_PAGE] {};

    prepare_pages[0] = {memory_layout.code_address, user_code_flags, code_segment};
    prepare_pages[1] = {memory_layout.stack_address, user_writable_flags, nullptr};

    UserMemory prepared_memory{};

    size_t next = 2;
    for (VirtPageNum vpn : ranges[2]) {
        prepare_pages[next++] = {
            vpn.to_addr().val(),
            user_writable_flags,
            data_segment
        };
    }
    std::span<PreparedPage> active_pages(prepare_pages, total_pages);

    // Preserve the existing rollback order separately from allocation order.
    size_t rollback_order[USER_MEMORY_MAX_PAGE] {};

    rollback_order[0] = 1;
    rollback_order[1] = 0;

    for (size_t i = 2; i < total_pages; ++i) {
        rollback_order[i] = i;
    }

    std::span<const size_t> rollback_pages(rollback_order, total_pages);
    int result = SV39_OK;

    for (PreparedPage& page : active_pages) {
        page.physical_page = pmm_alloc_page();
        if (page.physical_page == nullptr) {
            result = SV39_ERR_NO_MEMORY;
            goto clean_up;
        }
    }


    for (PreparedPage& page : active_pages) {
        // The user stack has no ELF segment; PMM already cleared its page.
        if (page.segment == nullptr) {
            continue;
        }

        const UserElfSegment& segment = *page.segment;
        const size_t offset = page.virtual_address -
            static_cast<uintptr_t>(segment.virtual_address);

        const size_t remaining = static_cast<size_t>(segment.memory_size) - offset;
        const size_t memory_bytes = remaining < SV39_PAGE_SIZE ? remaining : SV39_PAGE_SIZE;

        size_t file_bytes = 0;
        if (offset < segment.file_size) {
            const size_t available =
                static_cast<size_t>(segment.file_size) - offset;
            file_bytes = available < memory_bytes ? available : memory_bytes;
        }

        const size_t source_offset = static_cast<size_t>(segment.file_offset) +
            (offset < segment.file_size ?
                offset
                : static_cast<size_t>(segment.file_size)
            );

        const int load_result = user_image_load_segment(
            page.physical_page, SV39_PAGE_SIZE, image.data(), image.size(),
            source_offset,
            file_bytes,
            memory_bytes);
        if (load_result != SV39_OK) {
            result = SV39_ERR_INVALID_ARGUMENT;
            goto clean_up;
        }
    }

    asm volatile("fence.i" ::: "memory");

    for (PreparedPage& page : active_pages) {
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

    for (PreparedPage& page : active_pages) {
        result = sv39_flush_page(page.virtual_address);
        if (result != SV39_OK) {
            goto clean_up;
        }
    }

    prepared_memory.root = &root;
    prepared_memory.page_count = total_pages;

    for (size_t i = 0; i < total_pages; ++i) {
        prepared_memory.pages[i] = {
            prepare_pages[i].virtual_address,
            prepare_pages[i].physical_page
        };
    }

    memory_out = prepared_memory;

    return SV39_OK;
clean_up:
    for (size_t index : rollback_pages) {
        const PreparedPage& page = prepare_pages[index];
        if (page.mapped) {
            const int error = sv39_unmap_page(&root, page.virtual_address);
            if (error != SV39_OK && error != SV39_ERR_NOT_MAPPED) {
                return error;
            }
        }
    }

    for (size_t index : rollback_pages) {
        const PreparedPage& page = prepare_pages[index];
        if (page.mapping_attempted) {
            const int error = sv39_flush_page(page.virtual_address);
            if (error != SV39_OK) {
                return error;
            }
        }
    }

    for (PreparedPage& page : active_pages) {
        if (page.physical_page != nullptr) {
            if (pmm_free_page(page.physical_page) != 0) {
                return SV39_ERR_PAGE_FREE_FAILED;
            }
            page.physical_page = nullptr;
        }
    }

    for (size_t index : rollback_pages) {
        const PreparedPage& page = prepare_pages[index];
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
