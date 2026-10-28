#ifndef TINYOS_USER_ACCESS_H
#define TINYOS_USER_ACCESS_H

#include <stddef.h>
#include <stdint.h>

#include "mm/vm.h"

#ifdef __cplusplus
extern "C" {
#endif
int user_copy_from(
    const Sv39PageTable* root,
    void* destination, //读写缓存区
    uintptr_t source_address, //用户虚地址
    size_t length
);
#ifdef __cplusplus
}
#endif
#endif
