/* Heartbeat LED and the console monitor (live sensor readings + RTOS statistics). */
#ifndef MONITOR_H
#define MONITOR_H

#include <stdbool.h>

void monitor_start(void);
/* 1 Hz timestamped readings (+ RTOS stats every 30 s) on the console; off by default. */
void monitor_set_live(bool on);
void monitor_print_rtos_stats(void);

#endif /* MONITOR_H */
