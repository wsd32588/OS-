#include "boot_private.h"

#include "arch/riscv/vm_arch.h"
#include "mm/pmm.h"
#include "user/user_image.h"

int prepare_demo_user_memory(
    Sv39PageTable* root,
    const void* image,
    size_t image_size,
    const UserElfLayout* layout,
    UserMemory* memory_out
) {

    if(root == NULL ||
        image == NULL ||
        layout == NULL ||
        memory_out == NULL ||
        layout->load_segment_count != USER_ELF_MAX_LOAD_SEGMENTS
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    const UserElfSegment* code_segment = NULL;
    const UserElfSegment* data_segment = NULL;

    for (size_t i = 0; i < layout->load_segment_count; ++i) {
        const UserElfSegment* segment = &layout->segments[i];

        if (segment->memory_size <= 0 ||
            segment->memory_size > SV39_PAGE_SIZE) {
            return SV39_ERR_INVALID_ARGUMENT;
        }
        if (segment->file_size > SV39_PAGE_SIZE) {
            return SV39_ERR_INVALID_ARGUMENT;
        }

        /* 按 VA 找段，不依赖数组顺序；重复段/未知段/权限不符都拒绝。 */

        if (segment->virtual_address == DEMO_USER_CODE_ADDRESS) {
            if (code_segment != NULL || segment->flags != 5U) {   /* R|X */
                return SV39_ERR_INVALID_ARGUMENT;
            }
            code_segment = segment;
        } else if (segment->virtual_address == DEMO_USER_DATA_ADDRESS) {
            if (data_segment != NULL || segment->flags != 6U) {   /* R|W */
                return SV39_ERR_INVALID_ARGUMENT;
            }
            data_segment = segment;
        } else {
            return SV39_ERR_INVALID_ARGUMENT;
        }
    }

    if (code_segment == NULL || data_segment == NULL) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
    
    /* 段字段校验：大小、文件范围、入口。 */
    if (code_segment->memory_size == 0 ||
        code_segment->memory_size > SV39_PAGE_SIZE ||
        data_segment->memory_size == 0 ||
        data_segment->memory_size > SV39_PAGE_SIZE ||
        code_segment->file_size > code_segment->memory_size ||
        data_segment->file_size > data_segment->memory_size ||
        code_segment->file_offset > image_size ||
        code_segment->file_size > image_size - code_segment->file_offset ||
        data_segment->file_offset > image_size ||
        data_segment->file_size > image_size - data_segment->file_offset) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    uintptr_t entry = (uintptr_t)layout->entry_address;
    if (entry < DEMO_USER_CODE_ADDRESS ||
        (entry & 1U) != 0 ||
        entry - DEMO_USER_CODE_ADDRESS >= code_segment->file_size) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    void* code_page = NULL;
    void* stack_page = NULL;
    void* data_page = NULL;
    int code_mapping_attempted = 0;
    int stack_mapping_attempted = 0;
    int data_mapping_attempted = 0;
    int code_mapped = 0;
    int stack_mapped = 0;
    int data_mapped = 0;
    int result = SV39_OK;

    code_page = pmm_alloc_page();
    if (code_page == NULL) {
        result = SV39_ERR_NO_MEMORY;
        goto clean_up;
    }

    stack_page = pmm_alloc_page();
    if (stack_page == NULL) {
        result = SV39_ERR_NO_MEMORY;
        goto clean_up;
    }

    data_page = pmm_alloc_page();
    if (data_page == NULL) {
        result = SV39_ERR_NO_MEMORY;
        goto clean_up;
    }

    int load_reslut = user_image_load_segment(
        code_page, SV39_PAGE_SIZE, image, image_size,
        (size_t)code_segment->file_offset,
        (size_t)code_segment->file_size,
        (size_t)code_segment->memory_size);

    if (load_reslut != SV39_OK) {
        result = SV39_ERR_INVALID_ARGUMENT;
        goto clean_up;
    }

    load_reslut = user_image_load_segment(
        data_page, SV39_PAGE_SIZE, image, image_size,
        (size_t)data_segment->file_offset,
        (size_t)data_segment->file_size,
        (size_t)data_segment->memory_size);
    if (load_reslut != SV39_OK) {
        result = SV39_ERR_INVALID_ARGUMENT;
        goto clean_up;
    }

    asm volatile("fence.i" ::: "memory");

    const uint64_t user_code_flags =
        SV39_PTE_R |
        SV39_PTE_X |
        SV39_PTE_U |
        SV39_PTE_A;
    const uint64_t user_stack_flags =
        SV39_PTE_R |
        SV39_PTE_W |
        SV39_PTE_U |
        SV39_PTE_A |
        SV39_PTE_D;

    const uint64_t user_data_flags =
        SV39_PTE_R |
        SV39_PTE_W |
        SV39_PTE_U |
        SV39_PTE_A |
        SV39_PTE_D;

    code_mapping_attempted = 1;
    result = sv39_map_page(
        root,
        DEMO_USER_CODE_ADDRESS,
        (uintptr_t)code_page,
        user_code_flags
    );
    if (result != SV39_OK) {
        goto clean_up;
    }
    code_mapped = 1;

    stack_mapping_attempted = 1;
    result = sv39_map_page(
        root,
        DEMO_USER_STACK_ADDRESS,
        (uint64_t)(uintptr_t)stack_page,
        user_stack_flags
    );
    if (result != SV39_OK) {
        goto clean_up;
    }
    stack_mapped = 1;

    data_mapping_attempted  = 1;
    result = sv39_map_page(
        root,
        DEMO_USER_DATA_ADDRESS,
        (uint64_t)(uintptr_t)data_page,
        user_data_flags
    );
    if (result != SV39_OK) {
        goto clean_up;
    }
    data_mapped =1;

    result = sv39_flush_page(DEMO_USER_CODE_ADDRESS);
    if (result != SV39_OK) {
        goto clean_up;
    }

    result = sv39_flush_page(DEMO_USER_STACK_ADDRESS);
    if (result != SV39_OK) {
        goto clean_up;
    }

    result = sv39_flush_page(DEMO_USER_DATA_ADDRESS);
    if (result != SV39_OK) {
        goto clean_up;
    }

    *memory_out = (UserMemory){
        .root = root,
        .code_page = code_page,
        .stack_page = stack_page,
        .data_page = data_page,
        .code_address = DEMO_USER_CODE_ADDRESS,
        .stack_address = DEMO_USER_STACK_ADDRESS,
        .data_address = DEMO_USER_DATA_ADDRESS
    };

    return SV39_OK;
    clean_up:
        if (stack_mapped == 1) {
            int err = sv39_unmap_page(root, DEMO_USER_STACK_ADDRESS);
            if (err != SV39_OK && err != SV39_ERR_NOT_MAPPED) {
                return err;
            }
        }

        if (code_mapped == 1) {
            int err = sv39_unmap_page(root, DEMO_USER_CODE_ADDRESS);
            if (err != SV39_OK && err != SV39_ERR_NOT_MAPPED) {
                return err;
            }
        }

        if (data_mapped == 1) {
            int err = sv39_unmap_page(root, DEMO_USER_DATA_ADDRESS);
            if (err != SV39_OK && err != SV39_ERR_NOT_MAPPED) {
                return err;
            }
        }

        if (stack_mapping_attempted == 1) {
            int err = sv39_flush_page(DEMO_USER_STACK_ADDRESS);
            if (err != SV39_OK) {
                return err;
            }
        }

        if (code_mapping_attempted == 1) {
            int err = sv39_flush_page(DEMO_USER_CODE_ADDRESS);
            if (err != SV39_OK) {
                return err;
            }
        }

        if (data_mapping_attempted == 1) {
            int err = sv39_flush_page(DEMO_USER_DATA_ADDRESS);
            if (err != SV39_OK) {
                return err;
            }
        }

        if (code_page != NULL) {
            int err = pmm_free_page(code_page);
            if (err != 0) {
                return SV39_ERR_PAGE_FREE_FAILED;
            }
        }

        if (stack_page != NULL) {
            int err = pmm_free_page(stack_page);
            if (err != 0) {
                return SV39_ERR_PAGE_FREE_FAILED;
            }
        }

        if (data_page != NULL) {
            int err = pmm_free_page(data_page);
            if (err != 0) {
                return SV39_ERR_PAGE_FREE_FAILED;
            }
        }

        if (stack_mapping_attempted) {
            int err = sv39_reclaim_empty_tables(root, DEMO_USER_STACK_ADDRESS);
            if (err != SV39_OK &&
                err != SV39_ERR_NOT_MAPPED) {
                return err;
            }
        }

        if (code_mapping_attempted) {
            int err = sv39_reclaim_empty_tables(root, DEMO_USER_CODE_ADDRESS);
            if (err != SV39_OK &&
                err != SV39_ERR_NOT_MAPPED) {
                return err;
            }
        }

        if (data_mapping_attempted) {
            int err = sv39_reclaim_empty_tables(root, DEMO_USER_DATA_ADDRESS);
            if (err != SV39_OK &&
                err != SV39_ERR_NOT_MAPPED
            ) {
                return err;
            }
        }

    return result;
}
