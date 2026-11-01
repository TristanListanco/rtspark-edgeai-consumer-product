/*
 *  Task  Prio  Stack (words)  Activation                       Communication
 *  ui    2     768            key_q events, or every 250 ms    key_q (from keys task), sensors_get()
 *
 * M3: hardware self-test at start-up, then a live sensor screen. Each key press
 * gives an immediate beep, LED-matrix colour and console line (input -> feedback path).
 * Holding DOWN (or the CLI "diag" command) shows the self-test table; any key returns.
 */
#include "ui.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "console.h"
#include "diag.h"
#include "drivers/buzzer.h"
#include "drivers/keys.h"
#include "drivers/lcd.h"
#include "drivers/ws2812.h"
#include "queue.h"
#include "sensors.h"
#include "task.h"

#define REFRESH_MS   250U
#define COLS         30   /* 240 px / 8 px */
#define MATRIX_LEVEL 24U  /* keep the WS2812s comfortable to look at */

#define KEY_SHOW_DIAG KEY_COUNT /* synthetic event posted by ui_show_diag() */

static QueueHandle_t key_q;
static char last_key[COLS + 1] = "none";
static lcd_info_t lcd_id;
static bool diag_mode;
static diag_item_t diag_items[DIAG_MAX_ITEMS];

static void line(uint16_t row, uint16_t fg, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void line(uint16_t row, uint16_t fg, const char *fmt, ...)
{
    char buf[COLS + 1];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    for (int i = (n < 0 ? 0 : n); i < COLS; i++) buf[i] = ' '; /* pad: erase old text */
    buf[COLS] = '\0';
    lcd_text(0, (uint16_t)(40 + row * 16), buf, fg, COLOR_BLACK, 1);
}

static uint16_t health_color(sensor_health_t h)
{
    return (h == SENSOR_OK) ? COLOR_GREEN : COLOR_RED;
}

/* value in 1/scale units -> "+1.23" style text with `dec` decimals */
static const char *fx(char *buf, size_t size, int32_t v, int32_t scale, int dec)
{
    uint32_t mag = (uint32_t)labs(v);
    snprintf(buf, size, "%c%lu.%0*lu", v < 0 ? '-' : '+', mag / (uint32_t)scale, dec,
             (mag % (uint32_t)scale) / (uint32_t)(dec == 1 ? scale / 10 : scale / 100));
    return buf;
}

static void draw_static(void)
{
    lcd_fill(COLOR_BLACK);
    lcd_fill_rect(0, 0, LCD_W, 34, COLOR_BLUE);
    lcd_text(8, 1, "KeepSafe Box", COLOR_WHITE, COLOR_BLUE, 2);
    lcd_text(0, 240 - 18, "keys: beep | hold DOWN: diag", COLOR_GRAY, COLOR_BLACK, 1);
}

static void draw_values(void)
{
    sensor_snapshot_t s;
    char a[3][16], w[3][16], t[16], h[16];
    sensors_get(&s);

    line(0, health_color(s.imu_health), "IMU   %-6s n=%lu err=%lu", sensor_health_str(s.imu_health),
         s.imu_samples, s.imu_errors);
    line(1, COLOR_WHITE, " a %s %s %s g", fx(a[0], 16, s.imu.ax * 1000 / ICM20608_ACCEL_LSB_PER_G, 1000, 2),
         fx(a[1], 16, s.imu.ay * 1000 / ICM20608_ACCEL_LSB_PER_G, 1000, 2),
         fx(a[2], 16, s.imu.az * 1000 / ICM20608_ACCEL_LSB_PER_G, 1000, 2));
    line(2, COLOR_WHITE, " w %s %s %s dps", fx(w[0], 16, s.imu.gx * 100 / ICM20608_GYRO_LSB_PER_DPS_X10, 10, 1),
         fx(w[1], 16, s.imu.gy * 100 / ICM20608_GYRO_LSB_PER_DPS_X10, 10, 1),
         fx(w[2], 16, s.imu.gz * 100 / ICM20608_GYRO_LSB_PER_DPS_X10, 10, 1));
    line(4, health_color(s.light_health), "LIGHT %-6s n=%lu err=%lu", sensor_health_str(s.light_health),
         s.light_samples, s.light_errors);
    line(5, COLOR_WHITE, " %lu.%02lu lx  ps %u  ir %u", s.lux_centi / 100U, s.lux_centi % 100U, s.light.ps,
         s.light.ir);
    line(7, health_color(s.env_health), "ENV   %-6s %s", sensor_health_str(s.env_health), sensors_aht_variant());
    line(8, COLOR_WHITE, " %s C  %s %%RH", fx(t, 16, s.temp_centi, 100, 2), fx(h, 16, s.rh_centi, 100, 2));
    line(10, COLOR_YELLOW, "KEY   %s", last_key);
    line(11, COLOR_GRAY, "up %lu s  heap %u B", HAL_GetTick() / 1000U, (unsigned)xPortGetFreeHeapSize());
}

static void self_test(void)
{
    static const struct { uint16_t color; const char *name; } bars[] = {
        {COLOR_RED, "red"}, {COLOR_GREEN, "green"}, {COLOR_BLUE, "blue"}, {COLOR_WHITE, "white"},
    };
    for (unsigned i = 0; i < sizeof bars / sizeof bars[0]; i++) {
        console_printf("selftest: LCD full %s", bars[i].name);
        lcd_fill(bars[i].color);
        vTaskDelay(pdMS_TO_TICKS(400));
    }

    console_printf("selftest: buzzer beep-beep (%u Hz)", BUZZER_TONE_HZ);
    static const uint16_t beep2[] = {100, 100, 100};
    buzzer_play(beep2, 3);

    static const struct { uint8_t r, g, b; const char *name; } m[] = {
        {MATRIX_LEVEL, 0, 0, "red"}, {0, MATRIX_LEVEL, 0, "green"}, {0, 0, MATRIX_LEVEL, "blue"},
    };
    for (unsigned i = 0; i < 3; i++) {
        console_printf("selftest: LED matrix all %s", m[i].name);
        ws2812_fill(m[i].r, m[i].g, m[i].b);
        ws2812_show();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ws2812_fill(0, 0, 0);
    ws2812_show();
    console_printf("selftest: done");
}

static void on_key(const key_event_t *e)
{
    static const uint8_t color[KEY_COUNT][3] = {
        [KEY_UP] = {MATRIX_LEVEL, 0, 0}, [KEY_DOWN] = {0, MATRIX_LEVEL, 0},
        [KEY_LEFT] = {0, 0, MATRIX_LEVEL}, [KEY_RIGHT] = {MATRIX_LEVEL, MATRIX_LEVEL, MATRIX_LEVEL},
    };
    console_printf("key %s %s (event t=%lu.%03lu s, held %lu ms)", key_name(e->key), key_evt_name(e->type),
                   e->tick_ms / 1000U, e->tick_ms % 1000U, e->held_ms);
    snprintf(last_key, sizeof last_key, "%s %s @%lu.%03lus", key_name(e->key), key_evt_name(e->type),
             e->tick_ms / 1000U, e->tick_ms % 1000U);

    if (e->type == KEY_EVT_PRESS) {
        buzzer_beep(40);
        ws2812_fill(color[e->key][0], color[e->key][1], color[e->key][2]);
        ws2812_show();
    } else if (e->type == KEY_EVT_LONG) {
        static const uint16_t beep2[] = {60, 60, 60};
        buzzer_play(beep2, 3);
        for (unsigned i = 0; i < WS2812_COUNT; i++) { /* rainbow-ish ramp */
            uint8_t k = (uint8_t)((i * 3U * MATRIX_LEVEL) / WS2812_COUNT);
            ws2812_set(i, k < MATRIX_LEVEL ? MATRIX_LEVEL - k : 0, k < MATRIX_LEVEL ? k : 2 * MATRIX_LEVEL - k,
                       k >= MATRIX_LEVEL ? k - MATRIX_LEVEL : 0);
        }
        ws2812_show();
    } else {
        ws2812_fill(0, 0, 0);
        ws2812_show();
    }
}

static void show_diag(void)
{
    static const uint16_t colors[] = {[DIAG_PASS] = COLOR_GREEN, [DIAG_WARN] = COLOR_YELLOW,
                                      [DIAG_FAIL] = COLOR_RED, [DIAG_ABSENT] = COLOR_GRAY,
                                      [DIAG_UNTESTED] = COLOR_CYAN};
    lcd_fill(COLOR_BLACK);
    lcd_fill_rect(0, 0, LCD_W, 18, COLOR_BLUE);
    lcd_text(0, 1, " SELF-TEST  running...", COLOR_WHITE, COLOR_BLUE, 1);
    unsigned n = diag_run(diag_items);
    diag_print(diag_items, n);

    unsigned pass = 0;
    for (unsigned i = 0; i < n; i++) pass += diag_items[i].status == DIAG_PASS;
    char text[COLS + 1], title[COLS + 1];
    snprintf(text, sizeof text, " SELF-TEST  %u/%u PASS", pass, n);
    snprintf(title, sizeof title, "%-30s", text); /* pad over "running..." */
    lcd_text(0, 1, title, COLOR_WHITE, COLOR_BLUE, 1);
    for (unsigned i = 0; i < n && i < 13; i++) {
        char row[COLS + 1];
        snprintf(row, sizeof row, "%-9s %-8s", diag_items[i].name, diag_status_str(diag_items[i].status));
        lcd_text(0, (uint16_t)(20 + i * 16), row, colors[diag_items[i].status], COLOR_BLACK, 1);
    }
    if (n > 13) {
        char row[COLS + 1];
        snprintf(row, sizeof row, "+%u more on serial console", n - 13);
        lcd_text(0, 20 + 13 * 16, row, COLOR_GRAY, COLOR_BLACK, 1);
    }
    diag_mode = true;
}

static void ui_task(void *arg)
{
    (void)arg;
    lcd_id = lcd_init();
    lcd_info_t lcd = lcd_id;
    console_printf("LCD: RDDID %02x %02x %02x -> %s", lcd.id[0], lcd.id[1], lcd.id[2],
                   lcd.ok ? "ST7789V3 ok" : "unexpected ID (check FSMC wiring/timing)");
    ws2812_init();
    console_printf("LED matrix: %u x WS2812 on PA7 (TIM3_CH2 + DMA1 Stream5)", WS2812_COUNT);
    buzzer_init();
    console_printf("buzzer: PB0 TIM8_CH2N %u Hz", BUZZER_TONE_HZ);
    keys_init(key_q);
    uint8_t idle = keys_idle_mask();
    console_printf("keys: idle level UP=%s DOWN=%s LEFT=%s RIGHT=%s (pressed = opposite)",
                   (idle & 1U) ? "high" : "low", (idle & 2U) ? "high" : "low", (idle & 4U) ? "high" : "low",
                   (idle & 8U) ? "high" : "low");

    self_test();
    draw_static();

    for (;;) {
        key_event_t e;
        if (xQueueReceive(key_q, &e, pdMS_TO_TICKS(REFRESH_MS)) == pdPASS) {
            if (e.key == KEY_SHOW_DIAG || (e.key == KEY_DOWN && e.type == KEY_EVT_LONG && !diag_mode)) {
                show_diag();
                continue;
            }
            if (diag_mode) {
                if (e.type == KEY_EVT_PRESS) { /* any key leaves the self-test page */
                    diag_mode = false;
                    buzzer_beep(40);
                    draw_static();
                }
                continue;
            }
            on_key(&e);
        }
        if (!diag_mode) {
            draw_values();
        }
    }
}

const lcd_info_t *ui_lcd_info(void)
{
    return &lcd_id;
}

void ui_show_diag(void)
{
    key_event_t e = {.key = (key_id_t)KEY_SHOW_DIAG, .type = KEY_EVT_PRESS, .tick_ms = HAL_GetTick()};
    (void)xQueueSend(key_q, &e, 0);
}

void ui_start(void)
{
    key_q = xQueueCreate(8, sizeof(key_event_t));
    configASSERT(key_q != NULL);
    BaseType_t ok = xTaskCreate(ui_task, "ui", 1024, NULL, 2, NULL);
    configASSERT(ok == pdPASS);
}
