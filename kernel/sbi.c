#include "sbi.h"

#define SBI_EXT_TIME 0x54494D45UL
#define SBI_FID_SET_TIMER 0

void sbi_set_timer(u64 stime_value) {
    register u64 a0 asm("a0") = stime_value;
    register u64 a1 asm("a1");
    register u64 a6 asm("a6") = SBI_FID_SET_TIMER;
    register u64 a7 asm("a7") = SBI_EXT_TIME;

    asm volatile (
        "ecall"
        : "+r"(a0), "=r"(a1)
        : "r"(a6), "r"(a7)
        : "memory"
    );
}