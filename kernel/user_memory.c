#include "user_memory.h"

#include <stddef.h>
#include "uart.h"
#include "pmm.h"
#include "vm_arch.h"

int user_memory_release(
    UserMemory *memory
) {
    if (memory == NULL) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    if (memory->root == NULL &&
        memory->code_page == NULL &&
        memory->stack_page == NULL &&
        memory->code_address == 0 &&
        memory->stack_address == 0) {
        return SV39_OK;
    }

    if (memory->root == NULL) {
        return SV39_ERR_INVALID_ARGUMENT;
    }

    uint64_t physical_address_out1 = (uint64_t)(uintptr_t)memory->code_page;
    uint64_t physical_address_out2 = (uint64_t)(uintptr_t)memory->stack_page;

    uint64_t flags = SV39_PTE_U;

    int result =sv39_query_page(
        memory->root,
        memory->code_address,
        &physical_address_out1,
        &flags);

    if (result != SV39_OK &&
        result != SV39_ERR_NOT_MAPPED
    ) {
        return result;
    }
    if (result == SV39_OK){
        if (memory->code_page == NULL||
            physical_address_out1 != (uint64_t)(uintptr_t)memory->code_page ||
            (flags & SV39_PTE_U) == 0
        ) {
            if (physical_address_out1 == (uintptr_t)NULL) {
                uart_puts("Invalid address");
            }
            return SV39_ERR_INVALID_ARGUMENT;
        }
    }

    result = sv39_query_page(
        memory->root,
        memory->stack_address,
        &physical_address_out2,
        &flags);

    if (result != SV39_OK &&
        result != SV39_ERR_NOT_MAPPED
    ) {
        return result;
    }
    if (result == SV39_OK){
        if (memory->stack_page == NULL ||
            physical_address_out2 != (uint64_t)(uintptr_t)memory->stack_page ||
            (flags & SV39_PTE_U) == 0
        ) {
            return SV39_ERR_INVALID_ARGUMENT;
            }
    }

    result = sv39_unmap_page(
        memory->root, memory->code_address);
    if (result != SV39_OK &&
        result != SV39_ERR_NOT_MAPPED) {
        return result;
    }

    result = sv39_unmap_page(
        memory->root, memory->stack_address);

    if (result != SV39_OK &&
        result != SV39_ERR_NOT_MAPPED) {
        return result;
    }

    if (sv39_flush_page(memory->code_address) != SV39_OK ||
        sv39_flush_page(memory->stack_address) != SV39_OK
    ) {
        return SV39_ERR_INVALID_ARGUMENT;
    }
    if(memory->code_page != NULL){
        if (pmm_free_page(memory->code_page) != 0) {
            return SV39_ERR_PAGE_FREE_FAILED;
        }
        memory->code_page = NULL;
    }

    if (memory->stack_page != NULL){
        if(pmm_free_page(memory->stack_page) != 0) {
            return SV39_ERR_PAGE_FREE_FAILED;
        }
        memory->stack_page = NULL;
    }

    result = sv39_reclaim_empty_tables(
        memory->root, memory->code_address);
    if (result != SV39_OK &&
        result != SV39_ERR_NOT_MAPPED) {
        return result;
    }

    result = sv39_reclaim_empty_tables(
        memory->root, memory->stack_address);
    if (result != SV39_OK &&
        result != SV39_ERR_NOT_MAPPED) {
            return result;
        }
    *memory = (UserMemory){0};

    return SV39_OK;
}