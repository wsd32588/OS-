#ifndef TINYOS_VM_ARCH_H
#define TINYOS_VM_ARCH_H

#include <stdint.h>

#include "vm.h"

int sv39_activate(const Sv39PageTable* root);
uint64_t sv39_read_satp(void);

/*
 * 刷新当前 hart 上 virtual_address 对应的地址翻译缓存。
 * 地址必须是 canonical 且按页对齐。
 */
int sv39_flush_page(uint64_t virtual_address);

#endif
