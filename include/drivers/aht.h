/* Aosong AHT10 / AHT20 / AHT21 temperature + humidity sensor (I2C 0x38).
 * The variant is detected at init: AHT2x answers the 0xBE init command and
 * appends a CRC-8 to each measurement; AHT10 uses 0xE1 and has no CRC. */
#ifndef AHT_H
#define AHT_H

#include "drivers/i2c_bus.h"

#define AHT_ADDR 0x38

typedef enum { AHT_VARIANT_UNKNOWN, AHT_VARIANT_AHT10, AHT_VARIANT_AHT2X } aht_variant_t;

typedef struct {
    i2c_bus_t *bus;
    aht_variant_t variant;
    uint32_t crc_errors;
} aht_t;

i2c_status_t aht_init(aht_t *dev, i2c_bus_t *bus);
i2c_status_t aht_trigger(aht_t *dev);
/* Call >= 80 ms after aht_trigger(). Returns I2C_ERR_TIMEOUT while still busy,
 * I2C_ERR_BUS on CRC mismatch. Outputs in 0.01 °C and 0.01 %RH. */
i2c_status_t aht_fetch(aht_t *dev, int32_t *temp_centi, int32_t *rh_centi);
const char *aht_variant_name(aht_variant_t v);

#endif /* AHT_H */
