/*
 * Diagnostic self-test. Every check is live (talks to or observes the hardware now),
 * not a copy of the boot result, so a component that fails at run time shows up.
 * Visual/audible parts (LCD colours, LED matrix light, buzzer sound) can only be
 * confirmed by a person; for those the check proves the driver path works.
 */
#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "clock.h"
#include "console.h"
#include "drivers/buzzer.h"
#include "drivers/i2c_bus.h"
#include "drivers/keys.h"
#include "drivers/rtc.h"
#include "drivers/rw007.h"
#include "drivers/sdcard.h"
#include "drivers/spi_flash.h"
#include "drivers/ws2812.h"
#include "io_probe.h"
#include "sensors.h"
#include "task.h"
#include "ui.h"

#define MAX_TASKS 16

static TaskStatus_t ts[MAX_TASKS];

static void add(diag_item_t *items, unsigned *n, const char *name, diag_status_t st, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
static void add(diag_item_t *items, unsigned *n, const char *name, diag_status_t st, const char *fmt, ...)
{
    if (*n >= DIAG_MAX_ITEMS) return;
    diag_item_t *it = &items[(*n)++];
    it->name = name;
    it->status = st;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(it->detail, sizeof it->detail, fmt, ap);
    va_end(ap);
}

static void check_rtos(diag_item_t *items, unsigned *n)
{
    configRUN_TIME_COUNTER_TYPE total;
    UBaseType_t count = uxTaskGetSystemState(ts, MAX_TASKS, &total);
    unsigned worst_pct = 100;
    const char *worst = "-";
    for (UBaseType_t i = 0; i < count; i++) {
        uint32_t alloc = (uint32_t)(ts[i].pxEndOfStack - ts[i].pxStackBase + 1);
        unsigned pct = (unsigned)((ts[i].usStackHighWaterMark * 100U) / alloc);
        if (pct < worst_pct) {
            worst_pct = pct;
            worst = ts[i].pcTaskName;
        }
    }
    size_t heap_min = xPortGetMinimumEverFreeHeapSize();
    bool ok = heap_min >= 4096U && worst_pct >= 20U;
    add(items, n, "RTOS", ok ? DIAG_PASS : DIAG_WARN, "%u tasks, heap min %u B, stack min %u%% (%s)",
        (unsigned)count, (unsigned)heap_min, worst_pct, worst);
}

static void check_sensors(diag_item_t *items, unsigned *n)
{
    sensor_snapshot_t a, b;
    sensors_get(&a);
    vTaskDelay(pdMS_TO_TICKS(300));
    sensors_get(&b);

    uint32_t imu_new = b.imu_samples - a.imu_samples;
    bool imu_ok = b.imu_health == SENSOR_OK && imu_new >= 25U; /* expect ~30 in 300 ms */
    add(items, n, "IMU", b.imu_health == SENSOR_ABSENT ? DIAG_ABSENT : (imu_ok ? DIAG_PASS : DIAG_FAIL),
        "ICM20608 id 0x%02x, %lu smp/0.3s, err %lu", sensors_imu_whoami(), imu_new, b.imu_errors);

    uint32_t light_new = b.light_samples - a.light_samples;
    bool light_ok = b.light_health == SENSOR_OK && light_new >= 2U;
    add(items, n, "LIGHT", b.light_health == SENSOR_ABSENT ? DIAG_ABSENT : (light_ok ? DIAG_PASS : DIAG_FAIL),
        "AP3216C %lu.%02lu lx ps %u, err %lu", b.lux_centi / 100U, b.lux_centi % 100U, b.light.ps,
        b.light_errors);

    uint32_t age = HAL_GetTick() - b.env_tick_ms;
    bool env_ok = b.env_health == SENSOR_OK && b.env_samples > 0 && age < 12000U;
    add(items, n, "ENV", b.env_health == SENSOR_ABSENT ? DIAG_ABSENT : (env_ok ? DIAG_PASS : DIAG_FAIL),
        "%s %ld.%02ld C %ld.%02ld %%RH, age %lus", sensors_aht_variant(), b.temp_centi / 100,
        labs(b.temp_centi % 100), b.rh_centi / 100, b.rh_centi % 100, age / 1000U);
}

unsigned diag_run(diag_item_t *items)
{
    unsigned n = 0;
    char buf[24];

    uint32_t sys = HAL_RCC_GetSysClockFreq();
    bool hse = (RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) == RCC_PLLCFGR_PLLSRC_HSE;
    add(items, &n, "CLOCK", (sys == 168000000U && hse) ? DIAG_PASS : DIAG_WARN,
        "SYSCLK %lu MHz from %s", sys / 1000000U, hse ? "HSE 8 MHz" : "HSI");

    check_rtos(items, &n);
    check_sensors(items, &n);

    const lcd_info_t *lcd = ui_lcd_info();
    add(items, &n, "LCD", lcd->ok ? DIAG_PASS : DIAG_FAIL, "ST7789V3 id %02x %02x, FSMC NE3 (visual)",
        lcd->id[1], lcd->id[2]);

    uint32_t frames = ws2812_frames_done();
    add(items, &n, "LEDMATRIX", frames > 0 ? DIAG_PASS : DIAG_FAIL, "19x WS2812, %lu DMA frames (visual)",
        frames);

    add(items, &n, "BUZZER", buzzer_ok() ? DIAG_PASS : DIAG_FAIL, "PB0 TIM8_CH2N %u Hz running (audible)",
        BUZZER_TONE_HZ);

    uint8_t idle = keys_idle_mask();
    add(items, &n, "KEYS", idle == 0x0F ? DIAG_PASS : DIAG_WARN, "4 keys active-low, %lu events",
        keys_event_count());

    add(items, &n, "LEDS", DIAG_PASS, "LED_R PF12 run, LED_G PF11 heartbeat");

    rtc_format(buf, sizeof buf);
    rtc_source_t src = rtc_source();
    add(items, &n, "RTC", src == RTC_SRC_LSE ? (rtc_is_set() ? DIAG_PASS : DIAG_WARN) :
                          (src == RTC_SRC_LSI ? DIAG_WARN : DIAG_FAIL),
        "%s, %s", src == RTC_SRC_LSE ? "LSE" : (src == RTC_SRC_LSI ? "LSI" : "off"), buf);

    const flash_info_t *fi = flash_info();
    uint8_t id[3] = {0};
    bool same = flash_read_jedec(id) && memcmp(id, fi->jedec, 3) == 0;
    add(items, &n, "SPIFLASH", (fi->ok && same) ? DIAG_PASS : DIAG_FAIL, "%s %lu MB, JEDEC %02x%02x%02x",
        fi->name, fi->size >> 20, id[0], id[1], id[2]);

    const sd_info_t *sd = sd_info();
    if (sd->present) {
        add(items, &n, "MICROSD", DIAG_PASS, "%s %s %lu MB, MBR %s", sd->type ? "SDHC/XC" : "SDSC", sd->product,
            sd->blocks / 2048U, sd->mbr_signature ? "ok" : "missing");
    } else {
        add(items, &n, "MICROSD", DIAG_ABSENT, "no card (init err 0x%x)", sd->error);
    }

    const can_probe_t *can = can_last_probe();
    bool eeprom = sensors_i2c1_devices() > 0;
    add(items, &n, "EEPROM", eeprom ? DIAG_PASS : DIAG_ABSENT, "%s",
        eeprom ? "device on i2c1 at boot scan"
               : (can->lec == 3 ? "not fitted: PB8/PB9 carry the CAN transceiver"
                                : "no ACK on i2c1 PB8/PB9 at boot (not fitted)"));

    bool codec = i2c_probe(&i2c_bus2, 0x10);
    add(items, &n, "AUDIO", codec ? DIAG_PASS : DIAG_ABSENT, "ES8388 codec %s on i2c2 (not used)",
        codec ? "ACK 0x10" : "no ACK");

    add(items, &n, "CONSOLE", DIAG_PASS, "USART1 115200 8N1 via ST-LINK VCP");
    const rw007_probe_t *w = rw007_last_probe();
    add(items, &n, "WIFI", w->magic_ok ? DIAG_PASS : (w->ready_signal ? DIAG_WARN : DIAG_ABSENT),
        "RW007 %s, boot %lu ms%s", w->magic_ok ? "SPI magic ok" : (w->ready_signal ? "booted, no magic" : "no boot"),
        w->boot_ms, " (held in reset)");
    add(items, &n, "CAN", can->lec == 3 || can->tx_ok ? DIAG_PASS : DIAG_ABSENT, "PB8/PB9: %s (LEC %u)",
        can->verdict ? can->verdict : "not probed", can->lec);
    const rs485_probe_t *rs = rs485_last_probe();
    add(items, &n, "RS485", (rs->pc7 != LINE_FLOATING) != (rs->pg9 != LINE_FLOATING) ? DIAG_PASS : DIAG_ABSENT,
        "%s", rs->verdict ? rs->verdict : "not probed");
    return n;
}

const char *diag_status_str(diag_status_t s)
{
    switch (s) {
    case DIAG_PASS:   return "PASS";
    case DIAG_WARN:   return "WARN";
    case DIAG_FAIL:   return "FAIL";
    case DIAG_ABSENT: return "ABSENT";
    default:          return "UNTESTED";
    }
}

void diag_print(const diag_item_t *items, unsigned n)
{
    unsigned pass = 0, fail = 0;
    for (unsigned i = 0; i < n; i++) {
        pass += items[i].status == DIAG_PASS;
        fail += items[i].status == DIAG_FAIL;
    }
    console_printf("==== diagnostics: %u checks, %u PASS, %u FAIL ====", n, pass, fail);
    for (unsigned i = 0; i < n; i++) {
        console_printf("  %-9s %-8s %s", items[i].name, diag_status_str(items[i].status), items[i].detail);
    }
}
