#include "test_pmm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pmm.h"

#define TEST_PAGE_COUNT 8192UL
#define STRESS_OPERATION_COUNT 20000UL

static int fail(const char *message) {
    fprintf(stderr, "[FAIL] %s\n", message);
    return -1;
}

static int page_is_zero(const void *page) {
    const unsigned char *bytes = page;

    for (size_t i = 0; i < PAGE_SIZE; ++i) {
        if (bytes[i] != 0) {
            return 0;
        }
    }

    return 1;
}

static uint32_t next_random(uint32_t *state) {
    uint32_t value = *state;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;

    return value;
}

static int test_basic_operations(void *memory, size_t memory_size) {
    if (pmm_init(memory, (unsigned char *)memory + memory_size) != 0) {
        return fail("pmm_init rejected a valid region");
    }

    if (pmm_available_pages() != TEST_PAGE_COUNT) {
        return fail("initial available-page count is wrong");
    }

    void *page1 = pmm_alloc_page();
    void *page2 = pmm_alloc_page();
    void *page3 = pmm_alloc_page();

    if (page1 == NULL || page2 == NULL || page3 == NULL) {
        return fail("basic allocation returned NULL");
    }

    if ((uintptr_t)page2 - (uintptr_t)page1 != PAGE_SIZE ||
        (uintptr_t)page3 - (uintptr_t)page2 != PAGE_SIZE) {
        return fail("sequential pages are not one page apart");
    }

    memset(page2, 0xa5, PAGE_SIZE);
    unsigned long before_free = pmm_available_pages();

    if (pmm_free_page(page2) != 0 ||
        pmm_available_pages() != before_free + 1) {
        return fail("freeing a valid page did not restore its count");
    }

    if (pmm_free_page(page2) == 0) {
        return fail("double free was accepted");
    }

    void *reused_page = pmm_alloc_page();

    if (reused_page != page2) {
        return fail("the free-list page was not reused first");
    }

    if (!page_is_zero(reused_page)) {
        return fail("a reused page was not cleared");
    }

    void *unaligned_page = (unsigned char *)page1 + 1;
    void *never_allocated_page = (unsigned char *)page3 + PAGE_SIZE;
    unsigned long before_invalid_free = pmm_available_pages();

    if (pmm_free_page(NULL) == 0 ||
        pmm_free_page(unaligned_page) == 0 ||
        pmm_free_page(never_allocated_page) == 0 ||
        pmm_available_pages() != before_invalid_free) {
        return fail("an invalid free changed PMM state");
    }

    puts("[PASS] PMM basic allocation and release");
    return 0;
}

static int test_stress(void *memory, size_t memory_size) {
    void **live_pages = calloc(TEST_PAGE_COUNT, sizeof(*live_pages));
    unsigned char *in_use = calloc(TEST_PAGE_COUNT, sizeof(*in_use));
    int result = -1;

    if (live_pages == NULL || in_use == NULL) {
        fail("could not allocate stress-test metadata");
        goto done;
    }

    if (pmm_init(memory, (unsigned char *)memory + memory_size) != 0) {
        fail("pmm_init failed before the stress test");
        goto done;
    }

    size_t live_count = 0;
    uint32_t random_state = UINT32_C(0x32588);

    for (unsigned long operation = 0;
         operation < STRESS_OPERATION_COUNT;
         ++operation) {
        uint32_t random_value = next_random(&random_state);
        int should_allocate =
            live_count == 0 ||
            (live_count < TEST_PAGE_COUNT && (random_value & 1U) != 0);

        if (should_allocate) {
            void *page = pmm_alloc_page();

            if (page == NULL) {
                fail("stress allocation returned NULL too early");
                goto done;
            }

            uintptr_t offset = (uintptr_t)page - (uintptr_t)memory;

            if (offset >= memory_size || (offset & (PAGE_SIZE - 1)) != 0) {
                fail("stress allocation returned an out-of-range page");
                goto done;
            }

            size_t page_index = offset / PAGE_SIZE;

            if (in_use[page_index] != 0) {
                fail("stress allocation returned a live page twice");
                goto done;
            }

            if (!page_is_zero(page)) {
                fail("stress allocation returned a dirty page");
                goto done;
            }

            memset(page, 0xa5, PAGE_SIZE);
            in_use[page_index] = 1;
            live_pages[live_count++] = page;
        } else {
            size_t live_index = next_random(&random_state) % live_count;
            void *page = live_pages[live_index];
            size_t page_index =
                ((uintptr_t)page - (uintptr_t)memory) / PAGE_SIZE;

            if (pmm_free_page(page) != 0) {
                fail("stress release rejected a live page");
                goto done;
            }

            in_use[page_index] = 0;
            live_pages[live_index] = live_pages[--live_count];
        }

        if (pmm_available_pages() != TEST_PAGE_COUNT - live_count) {
            fail("available-page count drifted during the stress test");
            goto done;
        }
    }

    while (live_count > 0) {
        if (pmm_free_page(live_pages[--live_count]) != 0) {
            fail("final stress-test cleanup failed");
            goto done;
        }
    }

    if (pmm_available_pages() != TEST_PAGE_COUNT) {
        fail("not all stress-test pages were recovered");
        goto done;
    }

    printf(
        "[PASS] PMM stress: %lu deterministic operations over %lu pages\n",
        STRESS_OPERATION_COUNT,
        TEST_PAGE_COUNT
    );
    result = 0;

done:
    free(in_use);
    free(live_pages);
    return result;
}

int test_pmm(void) {
    size_t memory_size = TEST_PAGE_COUNT * PAGE_SIZE;
    void *memory = aligned_alloc(PAGE_SIZE, memory_size);

    if (memory == NULL) {
        return fail("could not allocate page-aligned host memory");
    }

    int result = test_basic_operations(memory, memory_size);

    if (result == 0) {
        result = test_stress(memory, memory_size);
    }

    free(memory);
    return result;
}
