#ifndef TINYOS_TRIAL_REFERENCE_H
#define TINYOS_TRIAL_REFERENCE_H

#include <stdint.h>

// Test-only declarations for the existing C implementation. Keeping this bridge
// local avoids changing the kernel's C headers to accommodate this experiment.
#ifdef __cplusplus
extern "C" {
#endif

int pmm_init(const void* start, const void* end);
void* pmm_alloc_page(void);
int pmm_free_page(void* page);
unsigned long pmm_available_pages(void);
int sv39_virtual_address_is_canonical(uint64_t address);
unsigned int sv39_vpn_index(uint64_t address, unsigned int level);
uint64_t sv39_make_pte(uint64_t address, uint64_t flags);
uint64_t sv39_pte_physical_address(uint64_t pte);
uint64_t sv39_pte_flags(uint64_t pte);
int sv39_pte_is_valid(uint64_t pte);
int sv39_pte_is_leaf(uint64_t pte);

#ifdef __cplusplus
}
#endif

#endif
