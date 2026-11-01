#include "drivers/icm20608.h"

#include "FreeRTOS.h"
#include "task.h"

#define REG_SMPLRT_DIV    0x19
#define REG_CONFIG        0x1A
#define REG_GYRO_CONFIG   0x1B
#define REG_ACCEL_CONFIG  0x1C
#define REG_ACCEL_CONFIG2 0x1D
#define REG_ACCEL_XOUT_H  0x3B
#define REG_PWR_MGMT_1    0x6B
#define REG_PWR_MGMT_2    0x6C
#define REG_WHO_AM_I      0x75

#define WHOAMI_ICM20608G  0xAF
#define WHOAMI_ICM20608D  0xAE

static void wait_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        HAL_Delay(ms);
    }
}

i2c_status_t icm20608_init(icm20608_t *dev, i2c_bus_t *bus)
{
    static const uint8_t candidates[] = {0x68, 0x69};
    i2c_status_t st = I2C_ERR_NACK;

    dev->bus = bus;
    dev->addr = 0;
    dev->whoami = 0;
    for (unsigned i = 0; i < sizeof candidates; i++) {
        uint8_t id = 0;
        st = i2c_reg_read(bus, candidates[i], REG_WHO_AM_I, &id, 1);
        if (st == I2C_OK) {
            dev->addr = candidates[i];
            dev->whoami = id;
            break;
        }
    }
    if (st != I2C_OK) {
        return st;
    }
    if (dev->whoami != WHOAMI_ICM20608G && dev->whoami != WHOAMI_ICM20608D) {
        return I2C_ERR_BUS; /* something answered, but it is not an ICM-20608 */
    }

    const struct { uint8_t reg, val; uint8_t wait_ms; } seq[] = {
        {REG_PWR_MGMT_1, 0x80, 100},  /* device reset */
        {REG_PWR_MGMT_1, 0x01, 10},   /* wake, auto-select best clock */
        {REG_PWR_MGMT_2, 0x00, 0},    /* all accel + gyro axes on */
        {REG_CONFIG, 0x03, 0},        /* gyro DLPF 41 Hz, internal rate 1 kHz */
        {REG_SMPLRT_DIV, 9, 0},       /* 1 kHz / (1 + 9) = 100 Hz output data rate */
        {REG_GYRO_CONFIG, 0x10, 0},   /* ±1000 dps */
        {REG_ACCEL_CONFIG, 0x10, 0},  /* ±8 g */
        {REG_ACCEL_CONFIG2, 0x03, 0}, /* accel DLPF 44.8 Hz */
    };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        st = i2c_reg_write(bus, dev->addr, seq[i].reg, seq[i].val);
        if (st != I2C_OK) {
            return st;
        }
        if (seq[i].wait_ms) {
            wait_ms(seq[i].wait_ms);
        }
    }

    /* Read back one setting to confirm the configuration was accepted */
    uint8_t accel_cfg = 0;
    st = i2c_reg_read(bus, dev->addr, REG_ACCEL_CONFIG, &accel_cfg, 1);
    if (st == I2C_OK && (accel_cfg & 0x18) != 0x10) {
        st = I2C_ERR_BUS;
    }
    return st;
}

i2c_status_t icm20608_read(icm20608_t *dev, icm20608_raw_t *out)
{
    uint8_t b[14];
    i2c_status_t st = i2c_reg_read(dev->bus, dev->addr, REG_ACCEL_XOUT_H, b, sizeof b);
    if (st != I2C_OK) {
        return st;
    }
    out->ax = (int16_t)((b[0] << 8) | b[1]);
    out->ay = (int16_t)((b[2] << 8) | b[3]);
    out->az = (int16_t)((b[4] << 8) | b[5]);
    out->temp = (int16_t)((b[6] << 8) | b[7]);
    out->gx = (int16_t)((b[8] << 8) | b[9]);
    out->gy = (int16_t)((b[10] << 8) | b[11]);
    out->gz = (int16_t)((b[12] << 8) | b[13]);
    return I2C_OK;
}

int32_t icm20608_temp_centi(int16_t raw)
{
    /* Datasheet: T = raw / 326.8 + 25 °C */
    return ((int32_t)raw * 1000) / 3268 + 2500;
}
