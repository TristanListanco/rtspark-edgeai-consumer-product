#include "sensors.h"

#include <string.h>

#include "FreeRTOS.h"
#include "capture.h"
#include "console.h"
#include "cycles.h"
#include "semphr.h"
#include "task.h"

#define IMU_PERIOD_MS        10U
#define LIGHT_EVERY_N_IMU    10U   /* 100 ms */
#define ENV_PERIOD_MS        5000U
#define AHT_CONVERSION_MS    80U
#define STUCK_FRAMES         50U   /* 0.5 s of bit-identical IMU frames */
#define ALS_RANGE            1U    /* 0..5162 lux, 0.0788 lux/count */

static icm20608_t imu;
static ap3216c_t light;
static aht_t env;

static sensor_snapshot_t snap;
static unsigned i2c1_found = 0;
static SemaphoreHandle_t snap_lock;

/* ---- boot-time bus scan ------------------------------------------------------ */

static const char *known_device(const i2c_bus_t *bus, uint8_t addr)
{
    if (bus == &i2c_bus2) {
        if (addr == 0x68 || addr == 0x69) return "ICM20608 IMU";
        if (addr == 0x1E) return "AP3216C light/proximity";
        if (addr == 0x10 || addr == 0x11) return "ES8388 audio codec";
    }
    if (bus == &i2c_bus3 && addr == 0x38) return "AHT temp/humidity";
    if (bus == &i2c_bus1 && addr >= 0x50 && addr <= 0x57) return "EEPROM (AT24Cxx)";
    return "unexpected";
}

static void scan_bus(i2c_bus_t *bus, i2c_status_t init_st)
{
    if (init_st != I2C_OK) {
        console_printf("i2c scan %s: bus init failed (%s) - lines stuck low?", bus->name, i2c_status_str(init_st));
        return;
    }
    unsigned found = 0;
    uint32_t start = HAL_GetTick();
    uint32_t recoveries = bus->recoveries;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_probe(bus, addr)) {
            console_printf("i2c scan %s: 0x%02x  %s", bus->name, addr, known_device(bus, addr));
            found++;
        }
    }
    /* A healthy empty bus NACKs each address in ~0.1 ms; timeouts/recoveries here
     * mean the lines are not pulled up (nothing fitted) or are held by something */
    console_printf("i2c scan %s: %u device(s), scan took %lu ms, %lu bus recoveries", bus->name, found,
                   HAL_GetTick() - start, bus->recoveries - recoveries);
    if (bus == &i2c_bus1) i2c1_found = found;
}

/* ---- init ------------------------------------------------------------------ */

void sensors_scan_all(void)
{
    scan_bus(&i2c_bus1, I2C_OK);
    scan_bus(&i2c_bus2, I2C_OK);
    scan_bus(&i2c_bus3, I2C_OK);
}


void sensors_init(void)
{
    snap_lock = xSemaphoreCreateMutex();
    configASSERT(snap_lock != NULL);
    memset(&snap, 0, sizeof snap);
    snap.imu_period_min_us = UINT32_MAX;

    scan_bus(&i2c_bus1, i2c_bus_init(&i2c_bus1));
    scan_bus(&i2c_bus2, i2c_bus_init(&i2c_bus2));
    scan_bus(&i2c_bus3, i2c_bus_init(&i2c_bus3));

    i2c_status_t st = icm20608_init(&imu, &i2c_bus2);
    snap.imu_health = (st == I2C_OK) ? SENSOR_OK : SENSOR_ABSENT;
    console_printf("ICM20608: %s (addr 0x%02x, WHO_AM_I 0x%02x), +/-8 g, +/-1000 dps, ODR 100 Hz",
                   i2c_status_str(st), imu.addr, imu.whoami);

    st = ap3216c_init(&light, &i2c_bus2, ALS_RANGE);
    snap.light_health = (st == I2C_OK) ? SENSOR_OK : SENSOR_ABSENT;
    console_printf("AP3216C: %s (addr 0x%02x), ALS range 0..5162 lx, PS + IR on", i2c_status_str(st),
                   AP3216C_ADDR);

    st = aht_init(&env, &i2c_bus3);
    snap.env_health = (st == I2C_OK) ? SENSOR_OK : SENSOR_ABSENT;
    console_printf("AHT: %s (addr 0x%02x, variant %s)", i2c_status_str(st), AHT_ADDR,
                   aht_variant_name(env.variant));
}

/* ---- tasks ----------------------------------------------------------------- */

static bool imu_saturated(const icm20608_raw_t *r)
{
    const int16_t v[6] = {r->ax, r->ay, r->az, r->gx, r->gy, r->gz};
    for (int i = 0; i < 6; i++) {
        if (v[i] == INT16_MAX || v[i] == INT16_MIN) return true;
    }
    return false;
}

static void sensor_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t prev_cycles = cycles_now();
    bool have_prev = false;
    icm20608_raw_t prev_raw = {0};
    uint32_t same_frames = 0;
    uint32_t n = 0;

    for (;;) {
        bool late = xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(IMU_PERIOD_MS)) == pdFALSE;
        uint32_t now_cycles = cycles_now();
        uint32_t period_us = cycles_to_us(now_cycles - prev_cycles);
        prev_cycles = now_cycles;

        /* IMU */
        if (snap.imu_health != SENSOR_ABSENT) {
            icm20608_raw_t raw;
            i2c_status_t st = icm20608_read(&imu, &raw);
            bool stuck = false;
            if (st == I2C_OK) {
                same_frames = (memcmp(&raw, &prev_raw, sizeof raw) == 0) ? same_frames + 1 : 0;
                prev_raw = raw;
                stuck = same_frames >= STUCK_FRAMES;
            }
            xSemaphoreTake(snap_lock, portMAX_DELAY);
            if (late) snap.imu_overruns++;
            if (have_prev) {
                if (period_us < snap.imu_period_min_us) snap.imu_period_min_us = period_us;
                if (period_us > snap.imu_period_max_us) snap.imu_period_max_us = period_us;
            }
            if (st != I2C_OK) {
                snap.imu_errors++;
                snap.imu_health = SENSOR_FAULT;
            } else {
                snap.imu = raw;
                snap.imu_tick_ms = HAL_GetTick();
                snap.imu_samples++;
                if (imu_saturated(&raw)) snap.imu_saturated++;
                snap.imu_health = stuck ? SENSOR_STUCK : SENSOR_OK;
            }
            uint32_t seq = snap.imu_samples, t_ms = HAL_GetTick();
            xSemaphoreGive(snap_lock);
            if (capture_active()) {
                if (st == I2C_OK) capture_push_imu(seq, t_ms, &raw);
                else capture_push_error(t_ms, true);
            }
            have_prev = true;
        }

        /* Light / proximity at 10 Hz */
        if (++n % LIGHT_EVERY_N_IMU == 0U && snap.light_health != SENSOR_ABSENT) {
            ap3216c_reading_t r;
            i2c_status_t st = ap3216c_read(&light, &r);
            xSemaphoreTake(snap_lock, portMAX_DELAY);
            if (st != I2C_OK) {
                snap.light_errors++;
                snap.light_health = SENSOR_FAULT;
            } else {
                snap.light = r;
                snap.lux_centi = ap3216c_lux_centi(&light, r.als_raw);
                snap.light_tick_ms = HAL_GetTick();
                snap.light_samples++;
                snap.light_health = SENSOR_OK;
            }
            xSemaphoreGive(snap_lock);
            if (capture_active()) {
                if (st == I2C_OK) capture_push_light(HAL_GetTick(), &r);
                else capture_push_error(HAL_GetTick(), false);
            }
        }
    }
}

static void env_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        int32_t t = 0, rh = 0;
        i2c_status_t st = aht_trigger(&env);
        if (st == I2C_OK) {
            vTaskDelay(pdMS_TO_TICKS(AHT_CONVERSION_MS));
            for (int tries = 0; tries < 5; tries++) {
                st = aht_fetch(&env, &t, &rh);
                if (st != I2C_ERR_TIMEOUT) break; /* still busy: wait a little more */
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        bool in_range = (t >= -4000 && t <= 8500 && rh >= 0 && rh <= 10000);

        xSemaphoreTake(snap_lock, portMAX_DELAY);
        if (st != I2C_OK || !in_range) {
            snap.env_errors++;
            snap.env_health = SENSOR_FAULT;
        } else {
            snap.temp_centi = t;
            snap.rh_centi = rh;
            snap.env_tick_ms = HAL_GetTick();
            snap.env_samples++;
            snap.env_health = SENSOR_OK;
        }
        xSemaphoreGive(snap_lock);

        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ENV_PERIOD_MS));
    }
}

void sensors_start(void)
{
    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(sensor_task, "sensor", 512, NULL, 4, NULL);
    if (snap.env_health != SENSOR_ABSENT) {
        ok &= xTaskCreate(env_task, "env", 384, NULL, 2, NULL);
    }
    configASSERT(ok == pdPASS);
}

/* ---- accessors ------------------------------------------------------------- */

void sensors_get(sensor_snapshot_t *out)
{
    xSemaphoreTake(snap_lock, portMAX_DELAY);
    *out = snap;
    xSemaphoreGive(snap_lock);
}

void sensors_take_period(uint32_t *min_us, uint32_t *max_us)
{
    xSemaphoreTake(snap_lock, portMAX_DELAY);
    *min_us = snap.imu_period_min_us;
    *max_us = snap.imu_period_max_us;
    snap.imu_period_min_us = UINT32_MAX;
    snap.imu_period_max_us = 0;
    xSemaphoreGive(snap_lock);
}

const char *sensor_health_str(sensor_health_t h)
{
    switch (h) {
    case SENSOR_OK:    return "ok";
    case SENSOR_FAULT: return "FAULT";
    case SENSOR_STUCK: return "STUCK";
    default:           return "ABSENT";
    }
}

const char *sensors_aht_variant(void)
{
    return aht_variant_name(env.variant);
}

unsigned sensors_i2c1_devices(void)
{
    return i2c1_found;
}

uint8_t sensors_imu_whoami(void)
{
    return imu.whoami;
}
