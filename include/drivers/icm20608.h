/* TDK InvenSense ICM-20608 6-axis IMU (I2C). Configured for ±8 g / ±1000 dps,
 * 100 Hz output data rate with ~45 Hz accel / 41 Hz gyro digital low-pass filters. */
#ifndef ICM20608_H
#define ICM20608_H

#include "drivers/i2c_bus.h"

#define ICM20608_ACCEL_LSB_PER_G      4096  /* ±8 g */
#define ICM20608_GYRO_LSB_PER_DPS_X10 328   /* ±1000 dps: 32.8 LSB per °/s */

typedef struct {
    i2c_bus_t *bus;
    uint8_t addr;
    uint8_t whoami;
} icm20608_t;

typedef struct {
    int16_t ax, ay, az;
    int16_t temp;
    int16_t gx, gy, gz;
} icm20608_raw_t;

/* Finds the device at 0x68 or 0x69, checks WHO_AM_I, resets and configures it. */
i2c_status_t icm20608_init(icm20608_t *dev, i2c_bus_t *bus);
i2c_status_t icm20608_read(icm20608_t *dev, icm20608_raw_t *out);

/* Die temperature in 0.01 °C */
int32_t icm20608_temp_centi(int16_t raw);

#endif /* ICM20608_H */
