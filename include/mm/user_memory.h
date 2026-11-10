#ifndef TINYOS_USER_MEMORY_H
#define TINYOS_USER_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include "mm/vm.h"

#ifdef __cplusplus
constexpr unsigned int USER_MEMORY_MAX_PAGE = 16U;
#else
#define USER_MEMORY_MAX_PAGE 16U
#endif
typedef struct UserMemoryPage {
    uintptr_t virtual_address;
    void* physical_page;
} UserMemoryPage;

typedef struct UserMemory {
    Sv39PageTable* root; //根页表
    size_t page_count;
    UserMemoryPage pages[USER_MEMORY_MAX_PAGE]; //数组记录顺序为：代码页、栈页、数据页0、数据页1……。

#ifdef __cplusplus
    int validate_pages() const;
    int validate_mappings() const;
    int release();
#endif
} UserMemory;

#ifdef __cplusplus
extern "C"{
#endif
int user_memory_release(
    UserMemory* memory
);

const UserMemoryPage* user_memory_find_page(
    const UserMemory* memory,
    uintptr_t page_address
);
#ifdef __cplusplus
}
#endif
#endif
