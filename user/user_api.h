#ifndef TINYOS_USER_API_H
#define TINYOS_USER_API_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

long user_write(const void* buffer, size_t length);
long user_putchar(unsigned char character);
long user_yield(void);
long user_sleep(unsigned long ticks);

#ifdef __cplusplus
[[noreturn]] void user_exit(void);
#else
_Noreturn void user_exit(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
