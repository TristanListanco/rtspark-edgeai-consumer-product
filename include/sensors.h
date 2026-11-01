/*
 * Sensor service: owns the onboard sensors, samples them on fixed schedules and
 * publishes the latest validated values in a mutex-protected snapshot.
 *   sensor task (prio 4): ICM20608 every 10 ms, AP3216C every 100 ms
 *   env task    (prio 2): AHT every 5 s (each measurement takes ~80 ms)
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

#include "drivers/ap3216c.h"
#include "drivers/aht.h"
#include "drivers/icm20608.h"

typedef enum {
    SENSOR_ABSENT, /* not found at init */
    SENSOR_OK,
    SENSOR_FAULT,  /* last read failed or value out of range */
    SENSOR_STUCK,  /* identical raw frames for too long */
} sensor_health_t;

typedef struct {
    /* ICM20608, 100 Hz */
    sensor_health_t imu_health;
    icm20608_raw_t imu;
    uint32_t imu_tick_ms;
    uint32_t imu_samples;
    uint32_t imu_errors;
    uint32_t imu_saturated;       /* frames with a ±full-scale axis */
    uint32_t imu_overruns;        /* deadlines missed by the 100 Hz loop */
    uint32_t imu_period_min_us;   /* since the last sensors_take_period() */
    uint32_t imu_period_max_us;

    /* AP3216C, 10 Hz */
    sensor_health_t light_health;
    ap3216c_reading_t light;
    uint32_t lux_centi;
    uint32_t light_tick_ms;
    uint32_t light_samples;
    uint32_t light_errors;

    /* AHT, every 5 s */
    sensor_health_t env_health;
    int32_t temp_centi;
    int32_t rh_centi;
    uint32_t env_tick_ms;
    uint32_t env_samples;
    uint32_t env_errors;
} sensor_snapshot_t;

/* Before the scheduler: bring up the buses, scan them, initialise the sensors. */
void sensors_init(void);
void sensors_start(void);
/* Re-scan all three I2C buses and print what answers. */
void sensors_scan_all(void);

void sensors_get(sensor_snapshot_t *out);
/* Copy the IMU period min/max and restart the window. */
void sensors_take_period(uint32_t *min_us, uint32_t *max_us);

const char *sensor_health_str(sensor_health_t h);
const char *sensors_aht_variant(void);
uint8_t sensors_imu_whoami(void);
/* Devices that answered on i2c1 (EEPROM bus) in the boot scan. */
unsigned sensors_i2c1_devices(void);

#endif /* SENSORS_H */
