/*
 * M2 bring-up tasks. They prove scheduler start-up, deterministic periodic timing,
 * and inter-task communication before any sensor driver exists.
 *
 *  Task       Prio  Stack (words)  Activation                 Communication
 *  probe      4     256            every 10 ms (DelayUntil)   -> probe_q, 1 report/s
 *  monitor    2     512            blocks on probe_q          console mutex
 *  heartbeat  1     128            every 500 ms (DelayUntil)  LED_B
 *
 * "probe" stands in for the future 100 Hz SensorTask: it timestamps every wake-up
 * with the DWT cycle counter so sampling jitter can be measured, not assumed.
 */
#include "bringup_tasks.h"

#include "FreeRTOS.h"
#include "board.h"
#include "console.h"
#include "cycles.h"
#include "queue.h"
#include "task.h"

#define PROBE_PERIOD_MS     10U
#define PROBE_REPORT_EVERY  100U  /* wake-ups per report = 1 s */
#define STATS_EVERY_REPORTS 10U   /* full RTOS stats every 10 s */

typedef struct {
    uint32_t seq;
    uint32_t wakeups;
    uint32_t period_min_us;
    uint32_t period_max_us;
    uint32_t period_mean_us;
    uint32_t overruns; /* xTaskDelayUntil found the deadline already passed */
} probe_report_t;

#define MAX_TASKS 12

static QueueHandle_t probe_q;
static TaskStatus_t task_status[MAX_TASKS];

static void probe_task(void *arg)
{
    (void)arg;
    probe_report_t rep = {0};
    uint64_t period_sum = 0;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t prev = cycles_now();

    rep.period_min_us = UINT32_MAX;
    for (;;) {
        if (xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PROBE_PERIOD_MS)) == pdFALSE) {
            rep.overruns++;
        }
        uint32_t now = cycles_now();
        uint32_t period_us = cycles_to_us(now - prev); /* unsigned math survives CYCCNT wrap */
        prev = now;

        if (period_us < rep.period_min_us) rep.period_min_us = period_us;
        if (period_us > rep.period_max_us) rep.period_max_us = period_us;
        period_sum += period_us;

        if (++rep.wakeups == PROBE_REPORT_EVERY) {
            rep.period_mean_us = (uint32_t)(period_sum / rep.wakeups);
            /* Never block the periodic task: drop the report if monitor is behind */
            (void)xQueueSend(probe_q, &rep, 0);
            rep.seq++;
            rep.wakeups = 0;
            rep.overruns = 0;
            rep.period_min_us = UINT32_MAX;
            rep.period_max_us = 0;
            period_sum = 0;
        }
    }
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
static void print_rtos_stats(void)
{
    configRUN_TIME_COUNTER_TYPE total_us = 0;
    UBaseType_t n = uxTaskGetSystemState(task_status, MAX_TASKS, &total_us);

    for (UBaseType_t i = 1; i < n; i++) {
        TaskStatus_t t = task_status[i];
        UBaseType_t j = i;
        while (j > 0 && task_status[j - 1].xTaskNumber > t.xTaskNumber) {
            task_status[j] = task_status[j - 1];
            j--;
        }
        task_status[j] = t;
    }

    console_printf("heap: free %u B, min-ever free %u B, total %u B; uptime %lu s",
                   (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize(),
                   (unsigned)configTOTAL_HEAP_SIZE, (uint32_t)(total_us / 1000000U));
    console_printf("  task       st prio  stack peak/alloc (B)  min-free   cpu");
    for (UBaseType_t i = 0; i < n; i++) {
        const TaskStatus_t *t = &task_status[i];
        uint32_t alloc_b = (uint32_t)(t->pxEndOfStack - t->pxStackBase + 1) * sizeof(StackType_t);
        uint32_t free_b = (uint32_t)t->usStackHighWaterMark * sizeof(StackType_t);
        uint32_t cpu_x100 = total_us ? (uint32_t)((t->ulRunTimeCounter * 10000U) / total_us) : 0U;
        console_printf("  %-10s %c  %4lu  %6lu / %-6lu       %3lu %%  %3lu.%02lu %%",
                       t->pcTaskName, state_char(t->eCurrentState), (uint32_t)t->uxCurrentPriority,
                       alloc_b - free_b, alloc_b, (free_b * 100U) / alloc_b, cpu_x100 / 100U,
                       cpu_x100 % 100U);
    }
}

static void monitor_task(void *arg)
{
    (void)arg;
    probe_report_t rep;
    uint32_t worst_min = UINT32_MAX, worst_max = 0, total_overruns = 0, reports = 0;

    for (;;) {
        if (xQueueReceive(probe_q, &rep, pdMS_TO_TICKS(2000)) != pdPASS) {
            console_printf("monitor: no probe report for 2 s (probe task stalled?)");
            continue;
        }
        reports++;
        if (rep.period_min_us < worst_min) worst_min = rep.period_min_us;
        if (rep.period_max_us > worst_max) worst_max = rep.period_max_us;
        total_overruns += rep.overruns;

        console_printf("probe #%lu: %lu wake-ups, period min/mean/max = %lu/%lu/%lu us, overruns %lu",
                       rep.seq, rep.wakeups, rep.period_min_us, rep.period_mean_us,
                       rep.period_max_us, rep.overruns);

        if (reports % STATS_EVERY_REPORTS == 0U) {
            console_printf("probe since boot: %lu reports, period min %lu us, max %lu us, overruns %lu",
                           reports, worst_min, worst_max, total_overruns);
            print_rtos_stats();
        }
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    board_led_set(LED_R_PORT, LED_R_PIN, 1); /* steady red = scheduler running */
    for (;;) {
        board_led_toggle(LED_B_PORT, LED_B_PIN);
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
    }
}

void bringup_tasks_start(void)
{
    probe_q = xQueueCreate(4, sizeof(probe_report_t));
    configASSERT(probe_q != NULL);
    vQueueAddToRegistry(probe_q, "probe_q");

    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(probe_task, "probe", 256, NULL, 4, NULL);
    ok &= xTaskCreate(monitor_task, "monitor", 512, NULL, 2, NULL);
    ok &= xTaskCreate(heartbeat_task, "heartbeat", 128, NULL, 1, NULL);
    configASSERT(ok == pdPASS);
}
