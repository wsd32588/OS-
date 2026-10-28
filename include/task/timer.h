#ifndef TINYOS_TIMER_H
#define TINYOS_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

void timer_init(void);
void timer_set_next(void);
void timer_stop(void);

#ifdef __cplusplus
}
#endif

#endif
