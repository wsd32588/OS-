#ifndef TINYOS_VM_ARCH_H
#define TINYOS_VM_ARCH_H

#include <stdint.h>

#include "vm.h"

int sv39_activate(const Sv39PageTable* root);
uint64_t sv39_read_satp(void);

#endif