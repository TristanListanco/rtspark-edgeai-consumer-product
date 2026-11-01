#include "drivers/ap3216c.h"

#include "FreeRTOS.h"
#include "task.h"

#define REG_SYS_CONFIG 0x00
#define REG_IR_LOW     0x0A
#define REG_ALS_CONFIG 0x10

#define SYS_SW_RESET   0x04
#define SYS_ALS_PS_IR  0x03

/* ALS resolution in 1/10000 lux per count, per range (datasheet) */
static const uint32_t als_res_x10000[4] = {3500, 788, 197, 49};

static void wait_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        HAL_Delay(ms);
    }
}

i2c_status_t ap3216c_init(ap3216c_t *dev, i2c_bus_t *bus, uint8_t als_range)
{
    dev->bus = bus;
    dev->als_range = als_range & 0x03U;

    i2c_status_t st = i2c_reg_write(bus, AP3216C_ADDR, REG_SYS_CONFIG, SYS_SW_RESET);
    if (st != I2C_OK) {
        return st;
    }
    wait_ms(15);
    st = i2c_reg_write(bus, AP3216C_ADDR, REG_ALS_CONFIG, (uint8_t)(dev->als_range << 4));
    if (st == I2C_OK) {
        st = i2c_reg_write(bus, AP3216C_ADDR, REG_SYS_CONFIG, SYS_ALS_PS_IR);
    }
    if (st != I2C_OK) {
        return st;
    }

    /* No ID register: confirm presence by reading the mode back */
    uint8_t mode = 0;
    st = i2c_reg_read(bus, AP3216C_ADDR, REG_SYS_CONFIG, &mode, 1);
    if (st == I2C_OK && (mode & 0x07U) != SYS_ALS_PS_IR) {
        st = I2C_ERR_BUS;
    }
    return st;
}

i2c_status_t ap3216c_read(ap3216c_t *dev, ap3216c_reading_t *out)
{
    /* Registers 0x0A..0x0F: IR L/H, ALS L/H, PS L/H. Read one at a time; the
     * datasheet does not guarantee auto-increment across the data block. */
    uint8_t r[6];
    for (unsigned i = 0; i < sizeof r; i++) {
        i2c_status_t st = i2c_reg_read(dev->bus, AP3216C_ADDR, (uint8_t)(REG_IR_LOW + i), &r[i], 1);
        if (st != I2C_OK) {
            return st;
        }
    }
    out->ir = (uint16_t)((r[1] << 2) | (r[0] & 0x03U));
    out->ir_overflow = (r[0] & 0x80U) != 0;
    out->als_raw = (uint16_t)((r[3] << 8) | r[2]);
    out->ps = (uint16_t)(((r[5] & 0x3FU) << 4) | (r[4] & 0x0FU));
    out->ps_object = (r[5] & 0x80U) != 0;
    return I2C_OK;
}

uint32_t ap3216c_lux_centi(const ap3216c_t *dev, uint16_t als_raw)
{
    return ((uint32_t)als_raw * als_res_x10000[dev->als_range]) / 100U;
}
