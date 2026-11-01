/*
 *  Task       Prio  Stack (words)  Activation                 Communication
 *  monitor    1     512            every 1 s (DelayUntil)     sensors_get() snapshot, console mutex;
 *                                                             prints only while "live" is on
 *  heartbeat  1     128            every 500 ms (DelayUntil)  LED_G
 */
#include "monitor.h"

#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "board.h"
#include "console.h"
#include "drivers/rtc.h"
#include "sensors.h"
#include "task.h"

#define STATS_EVERY_S 30U
#define MAX_TASKS     12

static TaskStatus_t task_status[MAX_TASKS];
static volatile bool live;
static uint32_t last_imu_samples, last_light_samples; /* per-second rate baseline */

/* Fixed-point to text without float printf: fx(buf, 1234, 3) -> "+1.234" */
static const char *fx(char *buf, size_t size, int32_t value, int decimals)
{
    int32_t scale = 1;
    for (int i = 0; i < decimals; i++) scale *= 10;
    uint32_t mag = (uint32_t)labs(value);
    snprintf(buf, size, "%c%lu.%0*lu", value < 0 ? '-' : '+', mag / (uint32_t)scale, decimals,
             mag % (uint32_t)scale);
    return buf;
}

static int32_t mg(int16_t raw) { return ((int32_t)raw * 1000) / ICM20608_ACCEL_LSB_PER_G; }
static int32_t decidps(int16_t raw) { return ((int32_t)raw * 100) / ICM20608_GYRO_LSB_PER_DPS_X10; }

static void print_readings(void)
{
    sensor_snapshot_t s;
    uint32_t pmin, pmax;
    char a[3][24], g[3][24], t[24];

    sensors_get(&s);
    sensors_take_period(&pmin, &pmax);

    char now[24];
    rtc_format(now, sizeof now);
    console_printf("time  %s (RTC)", now);

    console_printf("imu   %-6s a=[%s %s %s] g  w=[%s %s %s] dps  die %s C | %lu/s, period %lu..%lu us, "
                   "err %lu, sat %lu, overrun %lu",
                   sensor_health_str(s.imu_health), fx(a[0], 24, mg(s.imu.ax), 3), fx(a[1], 24, mg(s.imu.ay), 3),
                   fx(a[2], 24, mg(s.imu.az), 3), fx(g[0], 24, decidps(s.imu.gx), 1),
                   fx(g[1], 24, decidps(s.imu.gy), 1), fx(g[2], 24, decidps(s.imu.gz), 1),
                   fx(t, 24, icm20608_temp_centi(s.imu.temp), 2), s.imu_samples - last_imu_samples,
                   pmin == UINT32_MAX ? 0U : pmin, pmax, s.imu_errors, s.imu_saturated, s.imu_overruns);
    console_printf("light %-6s als %lu.%02lu lx  ps %u  ir %u  obj %u%s | %lu/s, err %lu",
                   sensor_health_str(s.light_health), s.lux_centi / 100U, s.lux_centi % 100U, s.light.ps,
                   s.light.ir, s.light.ps_object, s.light.ir_overflow ? "  IR-OVERFLOW(ps invalid)" : "",
                   s.light_samples - last_light_samples, s.light_errors);
    console_printf("env   %-6s %s C  %s %%RH (%s) | age %lu ms, n %lu, err %lu", sensor_health_str(s.env_health),
                   fx(t, 24, s.temp_centi, 2), fx(a[0], 24, s.rh_centi, 2), sensors_aht_variant(),
                   s.env_samples ? HAL_GetTick() - s.env_tick_ms : 0U, s.env_samples, s.env_errors);

    last_imu_samples = s.imu_samples;
    last_light_samples = s.light_samples;
}

static char state_char(eTaskState s)
{
    switch (s) {
    case eRunning:   return 'X';
    case eReady:     return 'R';
    case eBlocked:   return 'B';
    case eSuspended: return 'S';
    case eDeleted:   return 'D';
    default:         return '?';
    }
}

/* Task table from uxTaskGetSystemState(): no heap allocation, CPU % to 0.01 %,
 * stack use against the task's real allocation. Sorted by creation order. */
void monitor_print_rtos_stats(void)
{
    configRUN_TIME_COUNTER_TYPE total_us = 0;
    UBaseType_t n = uxTaskGetSystemState(task_status, MAX_TASKS, &total_us);

    for (UBaseType_t i = 1; i < n; i++) {
        TaskStatus_t ts = task_status[i];
        UBaseType_t j = i;
        while (j > 0 && task_status[j - 1].xTaskNumber > ts.xTaskNumber) {
            task_status[j] = task_status[j - 1];
            j--;
        }
        task_status[j] = ts;
    }

    console_printf("heap: free %u B, min-ever free %u B, total %u B; uptime %lu s",
                   (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize(),
                   (unsigned)configTOTAL_HEAP_SIZE, (uint32_t)(total_us / 1000000U));
    console_printf("  task       st prio  stack peak/alloc (B)  min-free   cpu");
    for (UBaseType_t i = 0; i < n; i++) {
        const TaskStatus_t *ts = &task_status[i];
        uint32_t alloc_b = (uint32_t)(ts->pxEndOfStack - ts->pxStackBase + 1) * sizeof(StackType_t);
        uint32_t free_b = (uint32_t)ts->usStackHighWaterMark * sizeof(StackType_t);
        uint32_t cpu_x100 = total_us ? (uint32_t)((ts->ulRunTimeCounter * 10000U) / total_us) : 0U;
        console_printf("  %-10s %c  %4lu  %6lu / %-6lu       %3lu %%  %3lu.%02lu %%", ts->pcTaskName,
                       state_char(ts->eCurrentState), (uint32_t)ts->uxCurrentPriority, alloc_b - free_b, alloc_b,
                       (free_b * 100U) / alloc_b, cpu_x100 / 100U, cpu_x100 % 100U);
    }
}

static void monitor_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t seconds = 0;

    for (;;) {
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
        if (!live) {
            /* keep the per-second baselines fresh so the first live line is correct */
            sensor_snapshot_t snap;
            sensors_get(&snap);
            last_imu_samples = snap.imu_samples;
            last_light_samples = snap.light_samples;
            sensors_take_period(&(uint32_t){0}, &(uint32_t){0});
            continue;
        }
        print_readings();
        if (++seconds % STATS_EVERY_S == 0U) {
            monitor_print_rtos_stats();
        }
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    board_led_set(LED_R_PORT, LED_R_PIN, 1); /* steady red = scheduler running */
    for (;;) {
        board_led_toggle(LED_G_PORT, LED_G_PIN);
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
    }
}

void monitor_set_live(bool on)
{
    live = on;
}

void monitor_start(void)
{
    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(monitor_task, "monitor", 512, NULL, 1, NULL);
    ok &= xTaskCreate(heartbeat_task, "heartbeat", 128, NULL, 1, NULL);
    configASSERT(ok == pdPASS);
}
