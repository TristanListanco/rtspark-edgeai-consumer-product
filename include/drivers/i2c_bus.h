/*
 * I2C bus abstraction: one API over hardware I2C (HAL, polled) and bit-banged
 * GPIO I2C. Every transfer takes the bus mutex once the scheduler runs, so
 * devices sharing a bus (ICM20608 + AP3216C + ES8388 on i2c2) are serialised.
 */
#ifndef I2C_BUS_H
#define I2C_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "stm32f4xx_hal.h"

typedef enum {
    I2C_OK = 0,
    I2C_ERR_NACK = -1,    /* device did not acknowledge */
    I2C_ERR_TIMEOUT = -2, /* bus or device did not respond in time */
    I2C_ERR_BUS = -3,     /* arbitration loss / bus error / stuck line */
} i2c_status_t;

typedef struct {
    const char *name;
    I2C_TypeDef *hw;             /* NULL = bit-banged */
    uint32_t hw_alternate;       /* GPIO AF for hardware buses */
    uint32_t speed_hz;
    GPIO_TypeDef *scl_port;
    uint16_t scl_pin;
    GPIO_TypeDef *sda_port;
    uint16_t sda_pin;

    /* runtime */
    I2C_HandleTypeDef hal;
    SemaphoreHandle_t lock;
    uint32_t transfers;
    uint32_t errors;
    uint32_t recoveries;
} i2c_bus_t;

extern i2c_bus_t i2c_bus1; /* PB8/PB9 hardware I2C1 */
extern i2c_bus_t i2c_bus2; /* PF1/PF0 hardware I2C2 */
extern i2c_bus_t i2c_bus3; /* PE0/PE1 bit-banged   */

i2c_status_t i2c_bus_init(i2c_bus_t *bus);

bool i2c_probe(i2c_bus_t *bus, uint8_t addr);
i2c_status_t i2c_write(i2c_bus_t *bus, uint8_t addr, const uint8_t *data, size_t len);
i2c_status_t i2c_read(i2c_bus_t *bus, uint8_t addr, uint8_t *data, size_t len);
i2c_status_t i2c_reg_read(i2c_bus_t *bus, uint8_t addr, uint8_t reg, uint8_t *data, size_t len);
i2c_status_t i2c_reg_write(i2c_bus_t *bus, uint8_t addr, uint8_t reg, uint8_t value);

const char *i2c_status_str(i2c_status_t st);

#endif /* I2C_BUS_H */
