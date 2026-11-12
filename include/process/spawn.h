#ifndef TINYOS_SPAWN_H
#define TINYOS_SPAWN_H
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif
int process_spawn_builtin(uint64_t program_id, uintptr_t user_argument);

#ifdef __cplusplus
}
#endif

#endif