#ifndef TINYOS_RUNTIME_PANIC_H
#define TINYOS_RUNTIME_PANIC_H

#ifdef __cplusplus
extern "C" [[noreturn]] void kernel_panic(
    const char* file, int line, const char* message);
#else
_Noreturn void kernel_panic(
    const char* file, int line, const char* message);
#endif

#define PANIC_KERNEL(message) \
    do { \
        kernel_panic(__FILE__, __LINE__, (message)); \
    } while (0)

#endif