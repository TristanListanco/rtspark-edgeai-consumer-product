/*
 * Hardware bring-up runs in a task, not in main(): from the first FreeRTOS API
 * call until vTaskStartScheduler(), the port keeps BASEPRI raised, masking every
 * interrupt at priority >= configMAX_SYSCALL_INTERRUPT_PRIORITY. That includes
 * the TIM6 HAL tick, so HAL_Delay() and every HAL timeout would hang forever.
 */
#include "app.h"

#include "FreeRTOS.h"
#include "cli.h"
#include "console.h"
#include "drivers/rtc.h"
#include "drivers/sdcard.h"
#include "drivers/rw007.h"
#include "drivers/spi_flash.h"
#include "io_probe.h"
#include "monitor.h"
#include "sensors.h"
#include "task.h"
#include "ui.h"

#define INIT_TASK_PRIO (configMAX_PRIORITIES - 2) /* above all app tasks, below timer daemon */

static void storage_init(void)
{
    bool kept = false;
    rtc_source_t src = rtc_init(&kept);
    char now[24];
    rtc_format(now, sizeof now);
    console_printf("RTC: %s, %s%s", rtc_source_name(src), now, kept ? " (kept across reset)" : "");

    flash_info_t fi = flash_init();
    console_printf("SPI flash: %s, %lu KB, JEDEC %02x %02x %02x", fi.ok ? fi.name : "NOT FOUND", fi.size / 1024U,
                   fi.jedec[0], fi.jedec[1], fi.jedec[2]);

    sd_info_t sd = sd_init();
    if (sd.present) {
        console_printf("microSD: %s '%s' %lu MB, block 0 signature %s", sd.type ? "SDHC/SDXC" : "SDSC", sd.product,
                       sd.blocks / 2048U, sd.mbr_signature ? "0x55AA ok" : "missing");
    } else {
        console_printf("microSD: no card or init failed (HAL error 0x%x) - continuing without it", sd.error);
    }
}

static void io_probes(void)
{
    rw007_probe_t w = rw007_probe();
    console_printf("Wi-Fi RW007: INT/BUSY %s after %lu ms, %u header tries, last reply magic %08lx %08lx -> %s; "
                   "held in reset (not used)",
                   w.ready_signal ? "went high" : "stayed low", w.boot_ms, w.attempts, w.magic1, w.magic2,
                   w.magic_ok ? "present" : (w.ready_signal ? "booted but no protocol reply" : "not responding"));
    can_probe_t c = can_probe();
    console_printf("CAN1 probe: PB8 %s, PB9 %s, frame LEC %u%s -> %s", io_line_str(c.pb8), io_line_str(c.pb9), c.lec,
                   c.tx_ok ? " TXOK" : "", c.verdict);
    rs485_probe_t r = rs485_probe();
    console_printf("RS485 probe (PG8=receive): PC7 %s, PG9 %s -> %s", io_line_str(r.pc7), io_line_str(r.pg9),
                   r.verdict);
}

static void init_task(void *arg)
{
    (void)arg;
    sensors_init();
    storage_init();
    io_probes();
    sensors_start();
    monitor_start();
    ui_start();
    cli_start();
    console_printf("init done (%u B heap free)", (unsigned)xPortGetFreeHeapSize());
    vTaskDelete(NULL);
}

void app_start(void)
{
    BaseType_t ok = xTaskCreate(init_task, "init", 1024, NULL, INIT_TASK_PRIO, NULL);
    configASSERT(ok == pdPASS);
}
