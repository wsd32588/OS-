#include "test_pmm.h"
#include "test_vm.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    if (test_pmm() != 0) {
        return EXIT_FAILURE;
    }

    if (test_vm() != 0) {
        return EXIT_FAILURE;
    }

    puts("[PASS] all host tests");
    return EXIT_SUCCESS;
}
