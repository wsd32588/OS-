#include "timer.h"
#include "sbi.h"

typedef unsigned long u64;

#define TIMER_PREQ 10000000UL
#define TIMER_INTERVAL TIMER_PREQ

#define SIE_STIE (1UL << 5)
#define SSTATUS_SIE (1UL << 1)

static inline u64 read_time(void) {
    u64 value;

    asm volatile(
        "csrr %0, time"
        : "=r"(value)
    );

    return value;
}

void timer_set_next(void) {
    u64 now = read_time();

    sbi_set_timer(now + TIMER_INTERVAL);
}

void timer_init(void) {
    timer_set_next();

    asm volatile(
        "csrs sie, %0"
        :
        :"r"(SIE_STIE)
        : "memory"
    );
}

void timer_stop(void) {
    asm volatile(
        "csrc sie, %0"
        :
        : "r"(SIE_STIE)
        : "memory"
    );
}
