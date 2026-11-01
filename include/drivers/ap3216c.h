/* Dyna Image AP3216C ambient light + proximity + IR sensor (I2C 0x1E). */
#ifndef AP3216C_H
#define AP3216C_H

#include "drivers/i2c_bus.h"

#define AP3216C_ADDR 0x1E

typedef struct {
    i2c_bus_t *bus;
    uint8_t als_range; /* 0..3 -> 20661 / 5162 / 1291 / 323 lux full scale */
} ap3216c_t;

typedef struct {
    uint16_t als_raw;
    uint16_t ps;        /* 10-bit proximity counts */
    uint16_t ir;        /* 10-bit IR counts */
    bool ps_object;     /* chip's own near/far decision */
    bool ir_overflow;   /* strong IR: PS reading invalid */
} ap3216c_reading_t;

i2c_status_t ap3216c_init(ap3216c_t *dev, i2c_bus_t *bus, uint8_t als_range);
i2c_status_t ap3216c_read(ap3216c_t *dev, ap3216c_reading_t *out);

/* ALS raw -> lux x 100 for the configured range */
uint32_t ap3216c_lux_centi(const ap3216c_t *dev, uint16_t als_raw);

#endif /* AP3216C_H */
