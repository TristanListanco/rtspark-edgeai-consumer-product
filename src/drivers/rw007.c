/*
 * RW007 presence probe, following the RT-Thread rw007 package (rw007_stm32_port.c,
 * spi_wifi_rw007.c): RST low 100 ms, release, wait for INT/BUSY (pulled down) to go
 * high, wait 200 ms, then a 16-byte full-duplex header exchange. A live module answers
 * with SLAVE_MAGIC1/2 in the same transfer.
 * BOOT0 shares SCK and BOOT1 shares CS: during reset SCK is parked low and CS is high.
 */
#include "drivers/rw007.h"

#include <string.h>

#include "FreeRTOS.h"
#include "board.h"
#include "drivers/spi2_bus.h"
#include "task.h"

#define MASTER_MAGIC1 0x67452301U
#define MASTER_MAGIC2 0xEFCDAB89U
#define SLAVE_MAGIC1  0x98BADCFEU
#define SLAVE_MAGIC2  0x10325476U
#define MASTER_CMD_PHASE 0x01U
#define SLAVE_CMD_PHASE  0x03U
#define BOOT_TIMEOUT_MS  3000U

static rw007_probe_t last;

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

rw007_probe_t rw007_probe(void)
{
    memset(&last, 0, sizeof last);
    spi2_init();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {.Pin = WIFI_INT_PIN, .Mode = GPIO_MODE_INPUT, .Pull = GPIO_PULLDOWN,
                             .Speed = GPIO_SPEED_FREQ_LOW};
    HAL_GPIO_Init(WIFI_INT_PORT, &gpio);
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_RESET);
    gpio = (GPIO_InitTypeDef){.Pin = WIFI_RST_PIN, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
                              .Speed = GPIO_SPEED_FREQ_LOW};
    HAL_GPIO_Init(WIFI_RST_PORT, &gpio);

    spi2_acquire();
    spi2_sck_park_low(true); /* BOOT0 strap = 0, BOOT1 (= CS) = 1 */
    vTaskDelay(pdMS_TO_TICKS(100));
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
    uint32_t t0 = HAL_GetTick();
    while (HAL_GPIO_ReadPin(WIFI_INT_PORT, WIFI_INT_PIN) == GPIO_PIN_RESET &&
           HAL_GetTick() - t0 < BOOT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    last.ready_signal = HAL_GPIO_ReadPin(WIFI_INT_PORT, WIFI_INT_PIN) == GPIO_PIN_SET;
    last.boot_ms = HAL_GetTick() - t0;
    spi2_sck_park_low(false);
    spi2_release();

    if (last.ready_signal) {
        /* The Wi-Fi firmware may still be starting after INT/BUSY rises: retry for 3 s */
        uint32_t ready_at = HAL_GetTick();
        for (last.attempts = 1; last.attempts <= 15; last.attempts++) {
            vTaskDelay(pdMS_TO_TICKS(200));
            uint8_t tx[16] = {0}, rx[16] = {0};
            tx[1] = (uint8_t)(MASTER_CMD_PHASE << 4); /* flag:4 (low) = 0, type:4 (high) = cmd phase */
            tx[4] = last.attempts;                    /* seq */
            put32(&tx[8], MASTER_MAGIC1);
            put32(&tx[12], MASTER_MAGIC2);

            spi2_acquire();
            HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_RESET);
            for (volatile int d = 0; d < 200; d++) { /* ~5 us CS-to-clock setup */
            }
            bool ok = spi2_txrx(tx, rx, sizeof tx);
            HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_SET);
            spi2_release();

            last.magic1 = get32(&rx[8]);
            last.magic2 = get32(&rx[12]);
            last.phase = rx[1] >> 4;
            last.magic_ok = ok && last.magic1 == SLAVE_MAGIC1 && last.magic2 == SLAVE_MAGIC2;
            if (last.magic_ok) {
                last.reply_ms = HAL_GetTick() - ready_at;
                break;
            }
        }
    }

    /* Wi-Fi is not used by the product: keep the module in reset */
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_RESET);
    return last;
}

const rw007_probe_t *rw007_last_probe(void)
{
    return &last;
}
