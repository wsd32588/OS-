cmake_minimum_required(VERSION 3.20)
project(TinyOSHostTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "TinyOS host tests require a native Linux/WSL build")
endif()
if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$" OR
   NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR "TinyOS host tests require GCC or Clang and a linker supporting --wrap")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
enable_testing()
get_filename_component(TINYOS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# Select only kernel algorithms that can run without RISC-V instructions.
add_library(tinyos_test_kernel STATIC
    "${TINYOS_SOURCE_DIR}/kernel/console/console_device.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/console/console_service.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/console/input_queue.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/debug/console.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/debug/file_view.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/fs/builtin_fs.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/fs/file_syscall.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/fs/file_service.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/fs/open_file.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/fs/file_descriptor_table.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/mm/pmm.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/mm/vm.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/mm/user_memory.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/mm/user_address_space.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/mm/user_access.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/storage/block_cache.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/user/user_image.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/process/process.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/task/task.cpp"
    "${TINYOS_SOURCE_DIR}/kernel/task/task_table.cpp"
)
set_target_properties(tinyos_test_kernel PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# This small test directory intentionally discovers new .cpp files automatically.
# CONFIGURE_DEPENDS also checks additions during a direct cmake --build invocation.
file(GLOB TINYOS_TEST_SOURCES CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
add_executable(tinyos_tests ${TINYOS_TEST_SOURCES})
add_executable(tinyos_console_fd_tests console_fd/main.cpp host_support.cpp)
set_target_properties(tinyos_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
set_target_properties(tinyos_console_fd_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

foreach(target IN ITEMS tinyos_test_kernel tinyos_tests tinyos_console_fd_tests)
    # Ordinary -I would shadow the system <sched.h> required by the C++ library.
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
        "SHELL:-iquote \"${TINYOS_SOURCE_DIR}/include\""
        "SHELL:-iquote \"${CMAKE_CURRENT_SOURCE_DIR}\""
    )
endforeach()

target_link_libraries(tinyos_tests PRIVATE tinyos_test_kernel)
target_link_options(tinyos_tests PRIVATE
    "LINKER:--wrap=sv39_unmap_page"
    "LINKER:--wrap=pmm_free_page"
    "LINKER:--wrap=pmm_alloc_page"
)
target_link_libraries(tinyos_console_fd_tests PRIVATE tinyos_test_kernel)
target_link_options(tinyos_console_fd_tests PRIVATE
    "LINKER:--wrap=sv39_unmap_page"
    "LINKER:--wrap=pmm_free_page"
    "LINKER:--wrap=pmm_alloc_page"
)

# Keep PMM cases sequential within each executable. Console profiles use
# separate processes so that service bindings remain one-time operations.
add_test(NAME tinyos_host_tests COMMAND tinyos_tests)
set_tests_properties(tinyos_host_tests PROPERTIES TIMEOUT 60)
add_test(NAME tinyos_console_fd_kdbg COMMAND tinyos_console_fd_tests kdbg)
add_test(NAME tinyos_console_fd_user COMMAND tinyos_console_fd_tests user)
set_tests_properties(tinyos_console_fd_kdbg tinyos_console_fd_user PROPERTIES TIMEOUT 60)
