#include "drivers/aht.h"

#include "FreeRTOS.h"
#include "task.h"

#define STATUS_BUSY 0x80U
#define STATUS_CAL  0x08U

static void wait_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        HAL_Delay(ms);
    }
}

/* CRC-8, polynomial 0x31, init 0xFF (AHT20/AHT21 datasheet) */
static uint8_t crc8(const uint8_t *data, unsigned len)
{
    uint8_t crc = 0xFF;
    for (unsigned i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x31U) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

i2c_status_t aht_init(aht_t *dev, i2c_bus_t *bus)
{
    dev->bus = bus;
    dev->variant = AHT_VARIANT_UNKNOWN;
    dev->crc_errors = 0;

    wait_ms(40); /* power-on time */
    uint8_t status = 0;
    i2c_status_t st = i2c_read(bus, AHT_ADDR, &status, 1);
    if (st != I2C_OK) {
        return st;
    }

    /* AHT2x calibration command first; AHT10 NACKs it and uses 0xE1 instead */
    static const uint8_t init_2x[] = {0xBE, 0x08, 0x00};
    static const uint8_t init_10[] = {0xE1, 0x08, 0x00};
    if (i2c_write(bus, AHT_ADDR, init_2x, sizeof init_2x) == I2C_OK) {
        dev->variant = AHT_VARIANT_AHT2X;
    } else if (i2c_write(bus, AHT_ADDR, init_10, sizeof init_10) == I2C_OK) {
        dev->variant = AHT_VARIANT_AHT10;
    } else {
        return I2C_ERR_BUS;
    }
    wait_ms(10);

    st = i2c_read(bus, AHT_ADDR, &status, 1);
    if (st == I2C_OK && (status & STATUS_CAL) == 0U) {
        st = I2C_ERR_BUS; /* calibration bit never set */
    }
    return st;
}

i2c_status_t aht_trigger(aht_t *dev)
{
    static const uint8_t cmd[] = {0xAC, 0x33, 0x00};
    return i2c_write(dev->bus, AHT_ADDR, cmd, sizeof cmd);
}

i2c_status_t aht_fetch(aht_t *dev, int32_t *temp_centi, int32_t *rh_centi)
{
    uint8_t b[7] = {0};
    unsigned len = (dev->variant == AHT_VARIANT_AHT2X) ? 7U : 6U;
    i2c_status_t st = i2c_read(dev->bus, AHT_ADDR, b, len);
    if (st != I2C_OK) {
        return st;
    }
    if (b[0] & STATUS_BUSY) {
        return I2C_ERR_TIMEOUT;
    }
    if (dev->variant == AHT_VARIANT_AHT2X && crc8(b, 6) != b[6]) {
        dev->crc_errors++;
        return I2C_ERR_BUS;
    }
    uint32_t rh_raw = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
    uint32_t t_raw = (((uint32_t)b[3] & 0x0FU) << 16) | ((uint32_t)b[4] << 8) | b[5];
    /* RH = raw / 2^20 * 100 %, T = raw / 2^20 * 200 - 50 °C */
    *rh_centi = (int32_t)(((uint64_t)rh_raw * 10000U) >> 20);
    *temp_centi = (int32_t)(((uint64_t)t_raw * 20000U) >> 20) - 5000;
    return I2C_OK;
}

const char *aht_variant_name(aht_variant_t v)
{
    switch (v) {
    case AHT_VARIANT_AHT10: return "AHT10";
    case AHT_VARIANT_AHT2X: return "AHT20/21";
    default:                return "unknown";
    }
}
