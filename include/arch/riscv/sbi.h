#ifndef TINYOS_SBI_H
#define TINYOS_SBI_H

typedef unsigned long u64;

#ifdef __cplusplus
extern "C" {
#endif

void sbi_set_timer(u64 stime_value);

#ifdef __cplusplus
}
#endif

#endif
