/*
 * I2C buses for the RT-Spark onboard devices.
 *
 * Hardware buses use the HAL in polled mode with a short timeout. A 14-byte IMU
 * read at 400 kHz takes ~0.4 ms, which is acceptable at 100 Hz; switch to
 * interrupt/DMA + task notification if CPU load ever matters.
 * Before every hardware init the lines are checked and, if a slave holds SDA low
 * (e.g. after a reset mid-transfer), SCL is clocked up to 9 times to release it.
 *
 * The bit-banged bus runs at ~100 kHz using the DWT cycle counter for delays.
 */
#include "drivers/i2c_bus.h"

#include "cycles.h"
#include "task.h"

#define HW_TIMEOUT_MS      10U
#define SOFT_HALF_PERIOD   5U   /* us -> ~100 kHz */
#define SOFT_STRETCH_MAX   1000U /* us of clock stretching tolerated */

i2c_bus_t i2c_bus1 = {
    .name = "i2c1", .hw = I2C1, .hw_alternate = GPIO_AF4_I2C1, .speed_hz = 100000U,
    .scl_port = GPIOB, .scl_pin = GPIO_PIN_8, .sda_port = GPIOB, .sda_pin = GPIO_PIN_9,
};
i2c_bus_t i2c_bus2 = {
    .name = "i2c2", .hw = I2C2, .hw_alternate = GPIO_AF4_I2C2, .speed_hz = 400000U,
    .scl_port = GPIOF, .scl_pin = GPIO_PIN_1, .sda_port = GPIOF, .sda_pin = GPIO_PIN_0,
};
i2c_bus_t i2c_bus3 = {
    .name = "i2c3", .hw = NULL, .speed_hz = 100000U,
    .scl_port = GPIOE, .scl_pin = GPIO_PIN_0, .sda_port = GPIOE, .sda_pin = GPIO_PIN_1,
};

/* ---- helpers -------------------------------------------------------------- */

static void delay_us(uint32_t us)
{
    uint32_t start = cycles_now();
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((cycles_now() - start) < ticks) {
    }
}

static void gpio_clock_enable(GPIO_TypeDef *port)
{
    if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
}

static void lines_as_gpio(i2c_bus_t *bus)
{
    GPIO_InitTypeDef gpio = {
        .Mode = GPIO_MODE_OUTPUT_OD, .Pull = GPIO_PULLUP, .Speed = GPIO_SPEED_FREQ_HIGH,
    };
    HAL_GPIO_WritePin(bus->scl_port, bus->scl_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(bus->sda_port, bus->sda_pin, GPIO_PIN_SET);
    gpio.Pin = bus->scl_pin;
    HAL_GPIO_Init(bus->scl_port, &gpio);
    gpio.Pin = bus->sda_pin;
    HAL_GPIO_Init(bus->sda_port, &gpio);
}

#define SCL_HIGH(b) HAL_GPIO_WritePin((b)->scl_port, (b)->scl_pin, GPIO_PIN_SET)
#define SCL_LOW(b)  HAL_GPIO_WritePin((b)->scl_port, (b)->scl_pin, GPIO_PIN_RESET)
#define SDA_HIGH(b) HAL_GPIO_WritePin((b)->sda_port, (b)->sda_pin, GPIO_PIN_SET)
#define SDA_LOW(b)  HAL_GPIO_WritePin((b)->sda_port, (b)->sda_pin, GPIO_PIN_RESET)
#define SDA_READ(b) (HAL_GPIO_ReadPin((b)->sda_port, (b)->sda_pin) == GPIO_PIN_SET)
#define SCL_READ(b) (HAL_GPIO_ReadPin((b)->scl_port, (b)->scl_pin) == GPIO_PIN_SET)

/* Release a slave stuck mid-byte: clock SCL until SDA is high, then send STOP. */
static void bus_recover(i2c_bus_t *bus)
{
    lines_as_gpio(bus);
    for (int i = 0; i < 9 && !SDA_READ(bus); i++) {
        SCL_LOW(bus);
        delay_us(SOFT_HALF_PERIOD);
        SCL_HIGH(bus);
        delay_us(SOFT_HALF_PERIOD);
    }
    SDA_LOW(bus);
    delay_us(SOFT_HALF_PERIOD);
    SCL_HIGH(bus);
    delay_us(SOFT_HALF_PERIOD);
    SDA_HIGH(bus);
    delay_us(SOFT_HALF_PERIOD);
}

static void bus_lock(i2c_bus_t *bus)
{
    if (bus->lock != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreTake(bus->lock, portMAX_DELAY);
    }
}

static void bus_unlock(i2c_bus_t *bus)
{
    if (bus->lock != NULL && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        xSemaphoreGive(bus->lock);
    }
}

/* ---- hardware back end ------------------------------------------------------ */

static i2c_status_t hw_init(i2c_bus_t *bus)
{
    if (bus->hw == I2C1) {
        __HAL_RCC_I2C1_CLK_ENABLE();
        __HAL_RCC_I2C1_FORCE_RESET();
        __HAL_RCC_I2C1_RELEASE_RESET();
    } else {
        __HAL_RCC_I2C2_CLK_ENABLE();
        __HAL_RCC_I2C2_FORCE_RESET();
        __HAL_RCC_I2C2_RELEASE_RESET();
    }

    bus_recover(bus);

    GPIO_InitTypeDef gpio = {
        .Mode = GPIO_MODE_AF_OD, .Pull = GPIO_PULLUP, .Speed = GPIO_SPEED_FREQ_HIGH,
        .Alternate = bus->hw_alternate,
    };
    gpio.Pin = bus->scl_pin;
    HAL_GPIO_Init(bus->scl_port, &gpio);
    gpio.Pin = bus->sda_pin;
    HAL_GPIO_Init(bus->sda_port, &gpio);

    bus->hal.Instance = bus->hw;
    bus->hal.Init.ClockSpeed = bus->speed_hz;
    bus->hal.Init.DutyCycle = I2C_DUTYCYCLE_2;
    bus->hal.Init.OwnAddress1 = 0;
    bus->hal.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    bus->hal.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    bus->hal.Init.OwnAddress2 = 0;
    bus->hal.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    bus->hal.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    bus->hal.State = HAL_I2C_STATE_RESET;
    return (HAL_I2C_Init(&bus->hal) == HAL_OK) ? I2C_OK : I2C_ERR_BUS;
}

static i2c_status_t hw_result(i2c_bus_t *bus, HAL_StatusTypeDef st)
{
    if (st == HAL_OK) {
        return I2C_OK;
    }
    uint32_t err = HAL_I2C_GetError(&bus->hal);
    if (err == HAL_I2C_ERROR_AF) {
        return I2C_ERR_NACK; /* normal "nobody home" answer: no recovery needed */
    }
    /* Timeout, bus error or arbitration loss: reset the peripheral and the lines */
    HAL_I2C_DeInit(&bus->hal);
    hw_init(bus);
    bus->recoveries++;
    return (st == HAL_TIMEOUT) ? I2C_ERR_TIMEOUT : I2C_ERR_BUS;
}

/* ---- bit-banged back end ----------------------------------------------------- */

static bool soft_wait_scl_high(i2c_bus_t *bus)
{
    SCL_HIGH(bus);
    for (uint32_t t = 0; t < SOFT_STRETCH_MAX; t++) {
        if (SCL_READ(bus)) {
            return true;
        }
        delay_us(1);
    }
    return false;
}

static void soft_start(i2c_bus_t *bus)
{
    SDA_HIGH(bus);
    SCL_HIGH(bus);
    delay_us(SOFT_HALF_PERIOD);
    SDA_LOW(bus);
    delay_us(SOFT_HALF_PERIOD);
    SCL_LOW(bus);
}

static void soft_stop(i2c_bus_t *bus)
{
    SDA_LOW(bus);
    delay_us(SOFT_HALF_PERIOD);
    SCL_HIGH(bus);
    delay_us(SOFT_HALF_PERIOD);
    SDA_HIGH(bus);
    delay_us(SOFT_HALF_PERIOD);
}

/* Returns I2C_OK on ACK */
static i2c_status_t soft_write_byte(i2c_bus_t *bus, uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        if (byte & (1U << bit)) SDA_HIGH(bus);
        else SDA_LOW(bus);
        delay_us(SOFT_HALF_PERIOD);
        if (!soft_wait_scl_high(bus)) return I2C_ERR_TIMEOUT;
        delay_us(SOFT_HALF_PERIOD);
        SCL_LOW(bus);
    }
    SDA_HIGH(bus); /* release for ACK */
    delay_us(SOFT_HALF_PERIOD);
    if (!soft_wait_scl_high(bus)) return I2C_ERR_TIMEOUT;
    bool nack = SDA_READ(bus);
    delay_us(SOFT_HALF_PERIOD);
    SCL_LOW(bus);
    return nack ? I2C_ERR_NACK : I2C_OK;
}

static i2c_status_t soft_read_byte(i2c_bus_t *bus, uint8_t *byte, bool ack)
{
    uint8_t value = 0;
    SDA_HIGH(bus);
    for (int bit = 7; bit >= 0; bit--) {
        delay_us(SOFT_HALF_PERIOD);
        if (!soft_wait_scl_high(bus)) return I2C_ERR_TIMEOUT;
        if (SDA_READ(bus)) value |= (uint8_t)(1U << bit);
        delay_us(SOFT_HALF_PERIOD);
        SCL_LOW(bus);
    }
    if (ack) SDA_LOW(bus);
    else SDA_HIGH(bus);
    delay_us(SOFT_HALF_PERIOD);
    if (!soft_wait_scl_high(bus)) return I2C_ERR_TIMEOUT;
    delay_us(SOFT_HALF_PERIOD);
    SCL_LOW(bus);
    SDA_HIGH(bus);
    *byte = value;
    return I2C_OK;
}

/* One combined transaction: [START addr+W wdata...] [(RE)START addr+R rdata...] STOP */
static i2c_status_t soft_transfer(i2c_bus_t *bus, uint8_t addr, const uint8_t *w, size_t wlen,
                                  uint8_t *r, size_t rlen)
{
    i2c_status_t st = I2C_OK;
    if (wlen > 0 || rlen == 0) {
        soft_start(bus);
        st = soft_write_byte(bus, (uint8_t)(addr << 1));
        for (size_t i = 0; st == I2C_OK && i < wlen; i++) {
            st = soft_write_byte(bus, w[i]);
        }
    }
    if (st == I2C_OK && rlen > 0) {
        soft_start(bus);
        st = soft_write_byte(bus, (uint8_t)((addr << 1) | 1U));
        for (size_t i = 0; st == I2C_OK && i < rlen; i++) {
            st = soft_read_byte(bus, &r[i], i + 1 < rlen);
        }
    }
    soft_stop(bus);
    if (st == I2C_ERR_TIMEOUT) {
        bus_recover(bus);
        bus->recoveries++;
    }
    return st;
}

/* ---- public API ----------------------------------------------------------- */

i2c_status_t i2c_bus_init(i2c_bus_t *bus)
{
    gpio_clock_enable(bus->scl_port);
    gpio_clock_enable(bus->sda_port);
    if (bus->lock == NULL) {
        bus->lock = xSemaphoreCreateMutex();
        configASSERT(bus->lock != NULL);
    }
    if (bus->hw != NULL) {
        return hw_init(bus);
    }
    bus_recover(bus);
    return (SDA_READ(bus) && SCL_READ(bus)) ? I2C_OK : I2C_ERR_BUS;
}

static i2c_status_t transfer(i2c_bus_t *bus, uint8_t addr, const uint8_t *w, size_t wlen, uint8_t *r,
                             size_t rlen)
{
    i2c_status_t st;
    bus_lock(bus);
    bus->transfers++;
    if (bus->hw == NULL) {
        st = soft_transfer(bus, addr, w, wlen, r, rlen);
    } else if (rlen == 0) {
        st = hw_result(bus, HAL_I2C_Master_Transmit(&bus->hal, (uint16_t)(addr << 1), (uint8_t *)w,
                                                    (uint16_t)wlen, HW_TIMEOUT_MS));
    } else if (wlen == 0) {
        st = hw_result(bus, HAL_I2C_Master_Receive(&bus->hal, (uint16_t)(addr << 1), r, (uint16_t)rlen,
                                                   HW_TIMEOUT_MS));
    } else {
        /* Register read: wlen must be 1 (8-bit register address) */
        st = hw_result(bus, HAL_I2C_Mem_Read(&bus->hal, (uint16_t)(addr << 1), w[0], I2C_MEMADD_SIZE_8BIT, r,
                                             (uint16_t)rlen, HW_TIMEOUT_MS));
    }
    if (st != I2C_OK && st != I2C_ERR_NACK) {
        bus->errors++;
    }
    bus_unlock(bus);
    return st;
}

bool i2c_probe(i2c_bus_t *bus, uint8_t addr)
{
    if (bus->hw == NULL) {
        return transfer(bus, addr, NULL, 0, NULL, 0) == I2C_OK;
    }
    bus_lock(bus);
    bool found = HAL_I2C_IsDeviceReady(&bus->hal, (uint16_t)(addr << 1), 1, HW_TIMEOUT_MS) == HAL_OK;
    if (!found && HAL_I2C_GetError(&bus->hal) != HAL_I2C_ERROR_AF &&
        HAL_I2C_GetError(&bus->hal) != HAL_I2C_ERROR_NONE) {
        HAL_I2C_DeInit(&bus->hal);
        hw_init(bus);
        bus->recoveries++;
    }
    bus_unlock(bus);
    return found;
}

i2c_status_t i2c_write(i2c_bus_t *bus, uint8_t addr, const uint8_t *data, size_t len)
{
    return transfer(bus, addr, data, len, NULL, 0);
}

i2c_status_t i2c_read(i2c_bus_t *bus, uint8_t addr, uint8_t *data, size_t len)
{
    return transfer(bus, addr, NULL, 0, data, len);
}

i2c_status_t i2c_reg_read(i2c_bus_t *bus, uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    return transfer(bus, addr, &reg, 1, data, len);
}

i2c_status_t i2c_reg_write(i2c_bus_t *bus, uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return transfer(bus, addr, buf, 2, NULL, 0);
}

const char *i2c_status_str(i2c_status_t st)
{
    switch (st) {
    case I2C_OK:          return "ok";
    case I2C_ERR_NACK:    return "nack";
    case I2C_ERR_TIMEOUT: return "timeout";
    default:              return "bus error";
    }
}
