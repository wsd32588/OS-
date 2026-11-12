#include "test_pmm.h"
#include "test_vm.h"
#include "test_user_memory.h"
#include "test_user_access.hpp"
#include "test_user_image.hpp"
#include "test_user_data.hpp"

#include <stdio.h>
#include <stdlib.h>

int test_user_page_collection();
int test_user_address_space();
int test_task_table();

int main(void) {
    if (test_pmm() != 0) {
        return EXIT_FAILURE;
    }

    if (test_vm() != 0) {
        return EXIT_FAILURE;
    }

    if (test_user_memory() != 0) {
        return EXIT_FAILURE;
    }

    if (test_user_page_collection() != 0) {
        return EXIT_FAILURE;
    }

    if (test_user_address_space() != 0) {
        return EXIT_FAILURE;
    }

    if (test_user_access() != 0) {
        return EXIT_FAILURE;
    }

    if (test_user_image() != 0) {
        return EXIT_FAILURE;
    }

    if (test_user_data() != 0) {
        return EXIT_FAILURE;
    }

    if (test_task_table() != 0) {
        return EXIT_FAILURE;
    }

    puts("[PASS] all host tests");
    return EXIT_SUCCESS;
}
