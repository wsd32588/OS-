#include "pmm.h"

#include <stdint.h>
#include <stddef.h>

static unsigned long next_free_page;
static unsigned long allocatable_start;
static unsigned long allocatable_end;

static unsigned long align_up(unsigned long value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static unsigned long align_down(unsigned long value) {
    return value & ~(PAGE_SIZE - 1);
}

struct free_page {
    struct free_page* next;
};

static struct free_page* free_list;
static unsigned long recycled_page_count;

int pmm_init(const void* memory_start, const void* memory_end) {
    uintptr_t raw_start = (uintptr_t)memory_start;
    uintptr_t raw_end = (uintptr_t)memory_end;

    uintptr_t page_start = align_up(raw_start);
    uintptr_t page_end = align_down(raw_end);

    if (page_start >= page_end) {
        return -1;
    }

    next_free_page = (unsigned long)page_start;
    allocatable_start = (unsigned long)page_start;
    allocatable_end = (unsigned long)page_end;

    free_list = NULL;
    recycled_page_count = 0;

    return 0;
}

void* pmm_alloc_page(void) {
    if (free_list != NULL) {
        struct free_page* page = free_list;
        free_list = page->next;
        --recycled_page_count;

        uint64_t* p = (uint64_t*)page;
        for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i) {
            p[i] = 0;
        }

        return (void*)page;
    }
    else {
        if (next_free_page >= allocatable_end) {
            return NULL;
        }

        unsigned long allocated = next_free_page;
        next_free_page += PAGE_SIZE;

        uint64_t* p = (uint64_t*)allocated;
        for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i) {
            p[i] = 0;
        }

    return (void*)allocated;
    }
}

unsigned long pmm_available_pages(void) {
    return recycled_page_count +
           (allocatable_end - next_free_page) / PAGE_SIZE;
}

int pmm_free_page(void* page) {
    uintptr_t addr = (uintptr_t)page;
    if (addr == 0 ||
        (addr & (PAGE_SIZE - 1)) != 0 ||
        addr < allocatable_start ||
        addr >= next_free_page) {
        return -1;
    }

    for (struct free_page *current = free_list;
        current != NULL;
        current = current->next) {
        if (current == page) {
            return -1;
        }
    }

    struct free_page* free_page = (struct free_page*)page;

    free_page->next = free_list;
    free_list = free_page;
    ++recycled_page_count;


    return 0;
}
