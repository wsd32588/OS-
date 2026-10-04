#include "test_pmm.h"
#include "test_vm.h"
#include "test_user_memory.h"

#include <stdio.h>
#include <stdlib.h>

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

    puts("[PASS] all host tests");
    return EXIT_SUCCESS;
}
