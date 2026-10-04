#ifndef TINYOS_USER_MEMORY_H
#define TINYOS_USER_MEMORY_H

#include <stdint.h>
#include "vm.h"

typedef struct {
    Sv39PageTable* root; //根页表
    void* code_page; //PMM返回的指针
    void* stack_page;
    uintptr_t code_address; // 用户虚地址
    uintptr_t stack_address;
} UserMemory;

int user_memory_release(
    UserMemory* memory
);

#endif