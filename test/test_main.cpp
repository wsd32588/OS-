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
int test_builtin_fs();
int test_file_syscall();
int test_open_file();
int test_process_files();
int test_kernel_console();
int test_input_queue();
int test_console_device();

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

    if (test_builtin_fs() != 0) {
        return EXIT_FAILURE;
    }

    if (test_file_syscall() != 0) {
        return EXIT_FAILURE;
    }

    if (test_open_file() != 0) {
        return EXIT_FAILURE;
    }

    if (test_process_files() != 0) {
        return EXIT_FAILURE;
    }

    if (test_kernel_console() != 0) {
        return EXIT_FAILURE;
    }

    if (test_input_queue() != 0) {
        return EXIT_FAILURE;
    }

    if (test_console_device() != 0) {
        return EXIT_FAILURE;
    }

    puts("[PASS] all host tests");
    return EXIT_SUCCESS;
}
