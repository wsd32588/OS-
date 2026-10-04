cmake_minimum_required(VERSION 3.20)
project(TinyOSHostTests LANGUAGES C CXX)

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
    "${TINYOS_SOURCE_DIR}/kernel/pmm.c"
    "${TINYOS_SOURCE_DIR}/kernel/vm.c"
    "${TINYOS_SOURCE_DIR}/kernel/user_memory.c"
)
set_target_properties(tinyos_test_kernel PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

# This small test directory intentionally discovers new .cpp files automatically.
# CONFIGURE_DEPENDS also checks additions during a direct cmake --build invocation.
file(GLOB TINYOS_TEST_SOURCES CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
add_executable(tinyos_tests ${TINYOS_TEST_SOURCES})
set_target_properties(tinyos_tests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

foreach(target IN ITEMS tinyos_test_kernel tinyos_tests)
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
)

# PMM has global state: keep all cases in one sequential test process.
add_test(NAME tinyos_host_tests COMMAND tinyos_tests)
set_tests_properties(tinyos_host_tests PROPERTIES TIMEOUT 60)
