#ifndef TINYOS_TEST_C_API_HPP
#define TINYOS_TEST_C_API_HPP

// Host tests call the same C objects that the kernel build uses.
extern "C" {
#include "mm/pmm.h"
#include "drivers/uart.h"
#include "mm/user_memory.h"
#include "arch/riscv/vm_arch.h"
#include "mm/user_access.h"
#include "user/user_image.h"
}

#endif
