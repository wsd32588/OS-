#include "mm/user_memory.h"

#include <span>

#include "arch/riscv/vm_arch.h"
#include "mm/address.hpp"
#include "mm/pmm.h"

int UserMemory::release() {
    if (this->root == nullptr &&
        this->code_page == nullptr &&
        this->stack_page == nullptr &&
        this->data_page == nullptr &&
        this->code_address == 0 &&
        this->stack_address == 0 &&
        this->data_address == 0) {
        return SV39_OK;
    }

    if (this->root == nullptr) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
    /* 指针已清空但 VA 保留时，仍需要完成清理。 */
    const bool data_enabled =
        this->data_page != nullptr ||
        this->data_address != 0;

    if (data_enabled &&
        (this->data_address == 0 ||
        !sv39_virtual_address_is_canonical(this->data_address) ||
        (this->data_address & (SV39_PAGE_SIZE - 1U)) != 0 ||
        this->data_address == this->code_address ||
        this->data_address == this->stack_address)) {
            return SV39_ERR_INVALID_ARGUMENT;
    }

    /* 同一个物理页不能被描述成两份拥有的资源。 */
    if ((this->code_page != nullptr &&
        (this->code_page == this->stack_page ||
        this->code_page == this->data_page)) ||
        (this->stack_page != nullptr &&
        this->stack_page == this->data_page)) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
    
    struct PageReference {
        void** physical_page;
        uintptr_t virtual_address;
    };
    PageReference page_references[] = {
        {&code_page, code_address},
        {&stack_page, stack_address},
        {&data_page, data_address},
    };
    const std::span<PageReference> pages(
        page_references, data_enabled ? 3 : 2);

    for (const PageReference& page : pages) {
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

        const PhysAddr expected(*page.physical_page);
        const PhysAddr actual(mapped_address);

        if (*page.physical_page == nullptr ||
            actual.val() != expected.val() ||
            (flags & SV39_PTE_U) == 0) {
            return SV39_ERR_INVALID_ARGUMENT;
        }
    }

    for (const PageReference& page : pages) {
        const int result =
            sv39_unmap_page(root, page.virtual_address);

        if (result != SV39_OK &&
            result != SV39_ERR_NOT_MAPPED) {
            return result;
        }
    }


    for (const PageReference& page : pages) {
        if (sv39_flush_page(page.virtual_address) != SV39_OK) {
            return SV39_ERR_INVALID_ARGUMENT;
        }
    }


    for (const PageReference& page : pages) {
        if (*page.physical_page != nullptr) {
            if (pmm_free_page(*page.physical_page) != 0) {
                return SV39_ERR_PAGE_FREE_FAILED;
            }
            // Clear the owning member immediately so a failed release can retry.
            *page.physical_page = nullptr;
        }
    }

    for (const PageReference& page : pages) {
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
}
