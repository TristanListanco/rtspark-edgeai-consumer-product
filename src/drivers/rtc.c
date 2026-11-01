#include "drivers/rtc.h"

#include <stdio.h>

#include "stm32f4xx_hal.h"

#define BKP_MAGIC 0x4B534231U /* "KSB1": calendar was set by the user */

static RTC_HandleTypeDef hrtc;
static rtc_source_t source = RTC_SRC_NONE;

static rtc_source_t current_source(void)
{
    uint32_t sel = RCC->BDCR & RCC_BDCR_RTCSEL;
    if (!(RCC->BDCR & RCC_BDCR_RTCEN)) return RTC_SRC_NONE;
    if (sel == RCC_BDCR_RTCSEL_0 && (RCC->BDCR & RCC_BDCR_LSERDY)) return RTC_SRC_LSE;
    if (sel == RCC_BDCR_RTCSEL_1) return RTC_SRC_LSI;
    return RTC_SRC_NONE;
}

rtc_source_t rtc_init(bool *kept_time)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127;
    hrtc.Init.SynchPrediv = 255; /* 32768 / 128 / 256 = 1 Hz */
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

    /* Calendar already running from an earlier boot: keep it */
    *kept_time = false;
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == BKP_MAGIC && current_source() != RTC_SRC_NONE) {
        source = current_source();
        hrtc.State = HAL_RTC_STATE_READY;
        *kept_time = true;
        return source;
    }

    /* LSE crystal; HAL waits up to LSE_STARTUP_TIMEOUT for it to start */
    RCC_OscInitTypeDef osc = {.OscillatorType = RCC_OSCILLATORTYPE_LSE, .LSEState = RCC_LSE_ON,
                              .PLL.PLLState = RCC_PLL_NONE};
    RCC_PeriphCLKInitTypeDef pclk = {.PeriphClockSelection = RCC_PERIPHCLK_RTC};
    if (HAL_RCC_OscConfig(&osc) == HAL_OK) {
        pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
        source = RTC_SRC_LSE;
    } else {
        osc = (RCC_OscInitTypeDef){.OscillatorType = RCC_OSCILLATORTYPE_LSI, .LSIState = RCC_LSI_ON,
                                   .PLL.PLLState = RCC_PLL_NONE};
        if (HAL_RCC_OscConfig(&osc) != HAL_OK) return source = RTC_SRC_NONE;
        pclk.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
        hrtc.Init.SynchPrediv = 249; /* LSI ~32 kHz: 32000 / 128 / 250 = 1 Hz (inaccurate) */
        source = RTC_SRC_LSI;
    }
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) return source = RTC_SRC_NONE;
    __HAL_RCC_RTC_ENABLE();
    if (HAL_RTC_Init(&hrtc) != HAL_OK) return source = RTC_SRC_NONE;
    return source;
}

rtc_source_t rtc_source(void)
{
    return source;
}

bool rtc_is_set(void)
{
    return source != RTC_SRC_NONE && HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == BKP_MAGIC;
}

bool rtc_get(rtc_time_t *t)
{
    if (source == RTC_SRC_NONE) return false;
    RTC_TimeTypeDef tm;
    RTC_DateTypeDef dt;
    /* Time must be read before date: reading the date unlocks the shadow registers */
    if (HAL_RTC_GetTime(&hrtc, &tm, RTC_FORMAT_BIN) != HAL_OK) return false;
    if (HAL_RTC_GetDate(&hrtc, &dt, RTC_FORMAT_BIN) != HAL_OK) return false;
    t->year = (uint16_t)(2000U + dt.Year);
    t->month = dt.Month;
    t->day = dt.Date;
    t->hour = tm.Hours;
    t->min = tm.Minutes;
    t->sec = tm.Seconds;
    return true;
}

bool rtc_set(const rtc_time_t *t)
{
    if (source == RTC_SRC_NONE || t->year < 2000 || t->year > 2099 || t->month < 1 || t->month > 12 ||
        t->day < 1 || t->day > 31 || t->hour > 23 || t->min > 59 || t->sec > 59) {
        return false;
    }
    RTC_TimeTypeDef tm = {.Hours = t->hour, .Minutes = t->min, .Seconds = t->sec,
                          .DayLightSaving = RTC_DAYLIGHTSAVING_NONE, .StoreOperation = RTC_STOREOPERATION_RESET};
    RTC_DateTypeDef dt = {.Year = (uint8_t)(t->year - 2000U), .Month = t->month, .Date = t->day,
                          .WeekDay = RTC_WEEKDAY_MONDAY /* not used */};
    if (HAL_RTC_SetTime(&hrtc, &tm, RTC_FORMAT_BIN) != HAL_OK) return false;
    if (HAL_RTC_SetDate(&hrtc, &dt, RTC_FORMAT_BIN) != HAL_OK) return false;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, BKP_MAGIC);
    return true;
}

void rtc_format(char *buf, size_t size)
{
    rtc_time_t t;
    if (!rtc_is_set() || !rtc_get(&t)) {
        snprintf(buf, size, "time-not-set");
        return;
    }
    snprintf(buf, size, "%04u-%02u-%02u %02u:%02u:%02u", t.year, t.month, t.day, t.hour, t.min, t.sec);
}

const char *rtc_source_name(rtc_source_t s)
{
    switch (s) {
    case RTC_SRC_LSE: return "LSE 32.768 kHz crystal";
    case RTC_SRC_LSI: return "LSI ~32 kHz (crystal failed)";
    default:          return "not running";
    }
}
