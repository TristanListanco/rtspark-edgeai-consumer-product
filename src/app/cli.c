/*
 *  Task  Prio  Stack (words)  Activation               Communication
 *  cli   1     1024           bytes from console RX    console stream buffer; calls into
 *                                                      drivers that take their own mutexes
 */
#include "cli.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "capture.h"
#include "console.h"
#include "diag.h"
#include "drivers/buzzer.h"
#include "drivers/rtc.h"
#include "drivers/rw007.h"
#include "drivers/sdcard.h"
#include "drivers/spi_flash.h"
#include "io_probe.h"
#include "monitor.h"
#include "sensors.h"
#include "task.h"
#include "ui.h"

#define LINE_LEN 80
#define MAX_ARGS 6

/* ---- commands ---------------------------------------------------------------- */

static void cmd_help(int argc, char **argv);

static void cmd_diag(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ui_show_diag(); /* the UI task runs the checks, prints them and draws the LCD page */
}

static void cmd_live(int argc, char **argv)
{
    bool on = argc < 2 || strcmp(argv[1], "off") != 0;
    monitor_set_live(on);
    console_printf("live readings %s", on ? "on (1 Hz, RTOS stats every 30 s)" : "off");
}

static void cmd_stats(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    monitor_print_rtos_stats();
}

static void cmd_i2cscan(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sensors_scan_all();
}

static bool parse_uint(const char *s, unsigned *out, unsigned max)
{
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || v > max) return false;
    *out = (unsigned)v;
    return true;
}

static void cmd_rtc(int argc, char **argv)
{
    char buf[24];
    if (argc == 4 && strcmp(argv[1], "set") == 0) {
        /* rtc set YYYY-MM-DD HH:MM:SS */
        unsigned y, mo, d, h, mi, s;
        char *date = argv[2], *time = argv[3];
        if (strlen(date) == 10 && strlen(time) == 8 && date[4] == '-' && date[7] == '-' && time[2] == ':' &&
            time[5] == ':') {
            date[4] = date[7] = time[2] = time[5] = '\0';
            if (parse_uint(date, &y, 2099) && parse_uint(date + 5, &mo, 12) && parse_uint(date + 8, &d, 31) &&
                parse_uint(time, &h, 23) && parse_uint(time + 3, &mi, 59) && parse_uint(time + 6, &s, 59)) {
                rtc_time_t t = {(uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, (uint8_t)s};
                if (rtc_set(&t)) {
                    rtc_format(buf, sizeof buf);
                    console_printf("rtc: set to %s", buf);
                    return;
                }
            }
        }
        console_printf("rtc: usage: rtc set YYYY-MM-DD HH:MM:SS");
        return;
    }
    rtc_format(buf, sizeof buf);
    console_printf("rtc: %s, source %s", buf, rtc_source_name(rtc_source()));
}

static void cmd_flash(int argc, char **argv)
{
    const flash_info_t *fi = flash_info();
    console_printf("flash: %s, %lu bytes, JEDEC %02x %02x %02x", fi->name, fi->size, fi->jedec[0], fi->jedec[1],
                   fi->jedec[2]);
    if (argc >= 2 && strcmp(argv[1], "test") == 0) {
        char detail[96];
        bool force = argc >= 3 && strcmp(argv[2], "force") == 0;
        bool ok = flash_selftest(force, detail, sizeof detail);
        console_printf("flash test: %s - %s", ok ? "PASS" : "FAIL", detail);
    }
}

static void cmd_sd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sd_info_t sd = sd_init();
    if (!sd.present) {
        console_printf("sd: no card / init failed (HAL error 0x%x)", sd.error);
        return;
    }
    console_printf("sd: %s, manufacturer 0x%02x, product '%s', %lu blocks (%lu MB)",
                   sd.type ? "SDHC/SDXC" : "SDSC", sd.manufacturer, sd.product, sd.blocks, sd.blocks / 2048U);
    console_printf("sd: block 0 %s, first partition type 0x%02x%s", sd.mbr_signature ? "has 0x55AA" : "NO 0x55AA",
                   sd.partition_type, (sd.partition_type == 0x0B || sd.partition_type == 0x0C) ? " (FAT32)" : "");
}

/* At-rest IMU statistics over 2 s: offsets and noise for the calibration notes. */
static void cmd_calib(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_printf("calib: keep the board still for 2 s...");
    double sum[6] = {0}, sumsq[6] = {0};
    unsigned n = 0;
    sensor_snapshot_t s;
    sensors_get(&s);
    uint32_t last = s.imu_samples, start = HAL_GetTick();
    TickType_t wake = xTaskGetTickCount();
    while (n < 200 && HAL_GetTick() - start < 3000U) {
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
        sensors_get(&s);
        if (s.imu_samples == last) continue;
        last = s.imu_samples;
        const int16_t v[6] = {s.imu.ax, s.imu.ay, s.imu.az, s.imu.gx, s.imu.gy, s.imu.gz};
        for (int i = 0; i < 6; i++) {
            sum[i] += v[i];
            sumsq[i] += (double)v[i] * v[i];
        }
        n++;
    }
    if (n < 50) {
        console_printf("calib: only %u samples - IMU not delivering", n);
        return;
    }
    long mean[6], sd[6];
    for (int i = 0; i < 6; i++) {
        double m = sum[i] / n, var = sumsq[i] / n - m * m;
        double scale = (i < 3) ? 1000.0 / ICM20608_ACCEL_LSB_PER_G   /* -> mg */
                               : 1000.0 / ICM20608_GYRO_LSB_PER_DPS_X10; /* -> 0.01 dps */
        mean[i] = lround(m * scale);
        sd[i] = lround(sqrt(var > 0 ? var : 0) * scale);
    }
    long norm = lround(sqrt((double)mean[0] * mean[0] + (double)mean[1] * mean[1] + (double)mean[2] * mean[2]));
    console_printf("calib: %u samples", n);
    console_printf("calib: accel mean [%ld %ld %ld] mg, |a| = %ld mg (ideal 1000), noise sd [%ld %ld %ld] mg",
                   mean[0], mean[1], mean[2], norm, sd[0], sd[1], sd[2]);
    console_printf("calib: gyro bias [%ld.%02ld %ld.%02ld %ld.%02ld] dps, noise sd [%ld.%02ld %ld.%02ld %ld.%02ld] dps",
                   mean[3] / 100, labs(mean[3] % 100), mean[4] / 100, labs(mean[4] % 100), mean[5] / 100,
                   labs(mean[5] % 100), sd[3] / 100, sd[3] % 100, sd[4] / 100, sd[4] % 100, sd[5] / 100,
                   sd[5] % 100);
}

static void cmd_ioprobe(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rw007_probe_t w = rw007_probe();
    console_printf("wifi: ready %s after %lu ms, magic %08lx %08lx -> %s", w.ready_signal ? "yes" : "no", w.boot_ms,
                   w.magic1, w.magic2, w.magic_ok ? "RW007 present" : "no protocol reply");
    can_probe_t c = can_probe();
    console_printf("can: PB8 %s, PB9 %s, LEC %u -> %s", io_line_str(c.pb8), io_line_str(c.pb9), c.lec, c.verdict);
    rs485_probe_t r = rs485_probe();
    console_printf("rs485: PC7 %s, PG9 %s -> %s", io_line_str(r.pc7), io_line_str(r.pg9), r.verdict);
}

/* cap start <LABEL> <SESSION> | cap trial <n> <seconds> | cap stop | cap */
static void cmd_cap(int argc, char **argv)
{
    unsigned n, secs;
    if (argc == 4 && strcmp(argv[1], "start") == 0) {
        if (!capture_session_start(argv[2], argv[3])) console_printf("cap: cannot start (busy or name too long)");
    } else if (argc == 4 && strcmp(argv[1], "trial") == 0 && parse_uint(argv[2], &n, 999) &&
               parse_uint(argv[3], &secs, 60)) {
        if (!capture_trial(n, secs)) console_printf("cap: no open session, or bad duration (1-60 s)");
    } else if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        capture_session_stop();
    } else {
        char st[40];
        capture_status(st, sizeof st);
        console_printf("cap: %s | usage: cap start <LABEL> <SESSION> | cap trial <n> <s> | cap stop",
                       st[0] ? st : "idle");
    }
}

static void cmd_beep(int argc, char **argv)
{
    unsigned ms = 200;
    if (argc >= 2) parse_uint(argv[1], &ms, 2000);
    buzzer_beep((uint16_t)ms);
    console_printf("beep %u ms", ms);
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    console_printf("rebooting...");
    vTaskDelay(pdMS_TO_TICKS(50));
    NVIC_SystemReset();
}

static const struct {
    const char *name;
    void (*fn)(int, char **);
    const char *help;
} commands[] = {
    {"help", cmd_help, "this list"},
    {"diag", cmd_diag, "run the self-test (console + LCD page)"},
    {"live", cmd_live, "live on|off: 1 Hz timestamped readings"},
    {"stats", cmd_stats, "FreeRTOS heap / stack / CPU table"},
    {"i2cscan", cmd_i2cscan, "scan all three I2C buses"},
    {"rtc", cmd_rtc, "show time | rtc set YYYY-MM-DD HH:MM:SS"},
    {"flash", cmd_flash, "SPI flash info | flash test [force]"},
    {"sd", cmd_sd, "re-detect the microSD card (read-only)"},
    {"calib", cmd_calib, "IMU offsets and noise at rest (2 s)"},
    {"ioprobe", cmd_ioprobe, "re-probe Wi-Fi, CAN and RS485"},
    {"cap", cmd_cap, "dataset capture (driven by ml/capture.py)"},
    {"beep", cmd_beep, "beep [ms]"},
    {"reboot", cmd_reboot, "software reset"},
};

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (unsigned i = 0; i < sizeof commands / sizeof commands[0]; i++) {
        console_printf("  %-8s %s", commands[i].name, commands[i].help);
    }
}

/* ---- line editor -------------------------------------------------------------- */

static void run_line(char *line)
{
    char *argv[MAX_ARGS];
    int argc = 0;
    for (char *tok = strtok(line, " \t"); tok && argc < MAX_ARGS; tok = strtok(NULL, " \t")) {
        argv[argc++] = tok;
    }
    if (argc == 0) return;
    for (unsigned i = 0; i < sizeof commands / sizeof commands[0]; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].fn(argc, argv);
            return;
        }
    }
    console_printf("unknown command '%s' (try 'help')", argv[0]);
}

static void cli_task(void *arg)
{
    (void)arg;
    char line[LINE_LEN + 1];
    unsigned len = 0;

    console_rx_start();
    console_printf("cli: ready, type 'help'");
    for (;;) {
        int c = console_getc(UINT32_MAX);
        if (c < 0) continue;
        if (c == '\r' || c == '\n') {
            if (len == 0) continue;
            console_lock();
            console_write_n("\r\n", 2);
            console_unlock();
            line[len] = '\0';
            run_line(line);
            len = 0;
        } else if ((c == 0x08 || c == 0x7F) && len > 0) {
            len--;
            console_lock();
            console_write_n("\b \b", 3);
            console_unlock();
        } else if (c >= 0x20 && c < 0x7F && len < LINE_LEN) {
            line[len++] = (char)c;
            char ch = (char)c;
            console_lock();
            console_write_n(&ch, 1);
            console_unlock();
        }
    }
}

void cli_start(void)
{
    BaseType_t ok = xTaskCreate(cli_task, "cli", 1024, NULL, 1, NULL);
    configASSERT(ok == pdPASS);
}
