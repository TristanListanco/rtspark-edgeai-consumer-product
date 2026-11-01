/* On-chip RTC clocked by the 32.768 kHz LSE crystal (LSI fallback).
 * A backup-register marker records that the time was set, so a reset keeps the clock. */
#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { RTC_SRC_NONE, RTC_SRC_LSE, RTC_SRC_LSI } rtc_source_t;

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, min, sec;
} rtc_time_t;

rtc_source_t rtc_init(bool *kept_time);
rtc_source_t rtc_source(void);
bool rtc_is_set(void);
bool rtc_get(rtc_time_t *t);
bool rtc_set(const rtc_time_t *t);
/* "2026-11-01 15:30:00", or "time-not-set" */
void rtc_format(char *buf, size_t size);
const char *rtc_source_name(rtc_source_t s);

#endif /* RTC_DRIVER_H */
