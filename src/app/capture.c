/*
 *  Task     Prio  Stack (words)  Activation         Communication
 *  capture  2     512            cap_q records      cap_q (from sensor task, depth 128 = 1.28 s);
 *                                                   console stream buffer (DMA)
 *
 * The sensor task copies raw samples into cap_q without blocking; this task turns
 * them into text. A full queue drops the sample and counts it, so capture can never
 * delay the 100 Hz sampling loop.
 */
#include "capture.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "console.h"
#include "drivers/buzzer.h"
#include "drivers/rtc.h"
#include "monitor.h"
#include "queue.h"
#include "sensors.h"
#include "task.h"

#define CAP_QUEUE_DEPTH 128
#define DRAIN_TIMEOUT_MS 2000U

typedef struct {
    char type; /* 'D', 'L', 'E' */
    uint32_t t_ms;
    uint32_t seq;
    int16_t v[6];
} cap_rec_t;

static QueueHandle_t cap_q;
static volatile bool active;
static volatile uint32_t dropped;
static volatile uint32_t lines;
static bool session_open;
static char session_id[41];
static char session_label[13];
static unsigned current_trial;

bool capture_active(void)
{
    return active;
}

static void push(const cap_rec_t *r)
{
    if (xQueueSend(cap_q, r, 0) != pdPASS) {
        dropped++;
    }
}

void capture_push_imu(uint32_t seq, uint32_t t_ms, const icm20608_raw_t *raw)
{
    cap_rec_t r = {.type = 'D', .t_ms = t_ms, .seq = seq,
                   .v = {raw->ax, raw->ay, raw->az, raw->gx, raw->gy, raw->gz}};
    push(&r);
}

void capture_push_light(uint32_t t_ms, const ap3216c_reading_t *l)
{
    cap_rec_t r = {.type = 'L', .t_ms = t_ms,
                   .v = {(int16_t)l->als_raw, (int16_t)l->ps, (int16_t)l->ir,
                         (int16_t)((l->ps_object ? 1 : 0) | (l->ir_overflow ? 2 : 0))}};
    push(&r);
}

void capture_push_error(uint32_t t_ms, bool imu)
{
    cap_rec_t r = {.type = 'E', .t_ms = t_ms, .seq = imu ? 1U : 0U};
    push(&r);
}

static void capture_task(void *arg)
{
    (void)arg;
    cap_rec_t r;
    char buf[96];
    for (;;) {
        xQueueReceive(cap_q, &r, portMAX_DELAY);
        int n;
        if (r.type == 'D') {
            n = snprintf(buf, sizeof buf, "D,%lu,%lu,%d,%d,%d,%d,%d,%d\r\n", r.seq, r.t_ms, r.v[0], r.v[1], r.v[2],
                         r.v[3], r.v[4], r.v[5]);
        } else if (r.type == 'L') {
            n = snprintf(buf, sizeof buf, "L,%lu,%u,%u,%u,%u\r\n", r.t_ms, (uint16_t)r.v[0], (uint16_t)r.v[1],
                         (uint16_t)r.v[2], (uint16_t)r.v[3]);
        } else {
            n = snprintf(buf, sizeof buf, "E,%lu,%s\r\n", r.t_ms, r.seq ? "imu" : "light");
        }
        console_lock();
        console_write_n(buf, (unsigned)n);
        console_unlock();
        if (r.type == 'D') lines++;
    }
}

void capture_start(void)
{
    cap_q = xQueueCreate(CAP_QUEUE_DEPTH, sizeof(cap_rec_t));
    configASSERT(cap_q != NULL);
    BaseType_t ok = xTaskCreate(capture_task, "capture", 512, NULL, 2, NULL);
    configASSERT(ok == pdPASS);
}

static void raw_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void raw_line(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 3) n = (int)sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    console_lock();
    console_write_n(buf, (unsigned)n);
    console_unlock();
}

bool capture_session_start(const char *label, const char *session)
{
    if (active || strlen(label) >= sizeof session_label || strlen(session) >= sizeof session_id) {
        return false;
    }
    strcpy(session_label, label);
    strcpy(session_id, session);
    session_open = true;
    current_trial = 0;
    monitor_set_live(false); /* keep the stream clean */

    sensor_snapshot_t s;
    sensors_get(&s);
    char now[24];
    rtc_format(now, sizeof now);
    raw_line("H,1,%s,%s,fw=%s,rate_hz=100,acc_lsb_per_g=%d,gyr_lsb_per_dps_x10=%d,als_lux_per_count_x10000=788,"
             "rtc=%s,env_temp_centi=%ld,env_rh_centi=%ld,lux_centi=%lu",
             session_id, session_label, FW_VERSION, ICM20608_ACCEL_LSB_PER_G, ICM20608_GYRO_LSB_PER_DPS_X10, now,
             s.temp_centi, s.rh_centi, s.lux_centi);
    return true;
}

bool capture_trial(unsigned trial, unsigned seconds)
{
    if (!session_open || active || seconds == 0 || seconds > 60) {
        return false;
    }
    current_trial = trial;

    /* Countdown the participant can hear with the lid closed: beep, beep, beep, GO */
    for (int i = 0; i < 3; i++) {
        buzzer_beep(80);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    buzzer_beep(400);

    dropped = 0;
    lines = 0;
    raw_line("T,%u,start,%lu,%u", trial, HAL_GetTick(), seconds);
    /* Seed the light value so the first IMU rows are not blank (AP3216C runs at 10 Hz) */
    sensor_snapshot_t s;
    sensors_get(&s);
    if (s.light_health == SENSOR_OK) capture_push_light(HAL_GetTick(), &s.light);
    active = true;
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000U));
    active = false;

    /* Let the capture task empty the queue so the end marker follows the last sample */
    uint32_t t0 = HAL_GetTick();
    while (uxQueueMessagesWaiting(cap_q) > 0 && HAL_GetTick() - t0 < DRAIN_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    raw_line("T,%u,end,%lu,%lu,%lu", trial, HAL_GetTick(), lines, dropped);

    static const uint16_t stop[] = {120, 100, 120};
    buzzer_play(stop, 3);
    return true;
}

void capture_session_stop(void)
{
    if (session_open) {
        raw_line("C,stop,%s", session_id);
    }
    session_open = false;
    current_trial = 0;
}

void capture_status(char *buf, size_t size)
{
    if (!session_open) {
        buf[0] = '\0';
    } else if (active) {
        snprintf(buf, size, "REC %s trial %u", session_label, current_trial);
    } else {
        snprintf(buf, size, "CAPTURE %s (waiting)", session_label);
    }
}
