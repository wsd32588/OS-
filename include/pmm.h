#ifndef TINYOS_PMM_H
#define TINYOS_PMM_H

#define PAGE_SIZE 4096UL

int pmm_init(const void* memory_start, const void* memory_end);
void* pmm_alloc_page(void);
unsigned long pmm_available_pages(void);
int pmm_free_page(void* page);

#endif