#ifndef TINYOS_TEST_C_API_HPP
#define TINYOS_TEST_C_API_HPP

// Host tests call the same C objects that the kernel build uses.
extern "C" {
#include "pmm.h"
#include "uart.h"
#include "user_memory.h"
#include "vm_arch.h"
#include "user_access.h"
}

#endif
