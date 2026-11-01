#include "drivers/spi2_bus.h"

#include "FreeRTOS.h"
#include "board.h"
#include "semphr.h"

#define SPI_TIMEOUT_MS 50U

static SPI_HandleTypeDef hspi2;
static SemaphoreHandle_t lock;

void spi2_init(void)
{
    if (lock != NULL) return;
    lock = xSemaphoreCreateMutex();
    configASSERT(lock != NULL);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    /* Deselect both devices before the bus becomes active */
    HAL_GPIO_WritePin(FLASH_CS_PORT, FLASH_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, GPIO_PIN_SET);
    GPIO_InitTypeDef gpio = {.Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_HIGH};
    gpio.Pin = FLASH_CS_PIN;
    HAL_GPIO_Init(FLASH_CS_PORT, &gpio);
    gpio.Pin = WIFI_CS_PIN;
    HAL_GPIO_Init(WIFI_CS_PORT, &gpio);

    spi2_sck_park_low(false);
    /* MISO (PC2) pulled up: an unselected/absent device then reads 0xFF, not noise */
    gpio = (GPIO_InitTypeDef){.Pin = GPIO_PIN_2 | GPIO_PIN_3, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP,
                              .Speed = GPIO_SPEED_FREQ_VERY_HIGH, .Alternate = GPIO_AF5_SPI2};
    HAL_GPIO_Init(GPIOC, &gpio);

    hspi2.Instance = SPI2;
    hspi2.Init.Mode = SPI_MODE_MASTER;
    hspi2.Init.Direction = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi2.Init.NSS = SPI_NSS_SOFT;
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4; /* 42 MHz / 4 */
    hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    HAL_SPI_Init(&hspi2);
    __HAL_SPI_ENABLE(&hspi2); /* enabled now so SCK idles driven low, not floating */
}

void spi2_acquire(void) { xSemaphoreTake(lock, portMAX_DELAY); }
void spi2_release(void) { xSemaphoreGive(lock); }

bool spi2_tx(const uint8_t *tx, uint16_t len)
{
    return HAL_SPI_Transmit(&hspi2, (uint8_t *)tx, len, SPI_TIMEOUT_MS) == HAL_OK;
}

bool spi2_rx(uint8_t *rx, uint16_t len)
{
    return HAL_SPI_Receive(&hspi2, rx, len, SPI_TIMEOUT_MS) == HAL_OK;
}

bool spi2_txrx(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    return HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)tx, rx, len, SPI_TIMEOUT_MS) == HAL_OK;
}

void spi2_sck_park_low(bool park)
{
    GPIO_InitTypeDef gpio = {.Pin = GPIO_PIN_13, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_VERY_HIGH};
    if (park) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
    } else {
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Alternate = GPIO_AF5_SPI2;
    }
    HAL_GPIO_Init(GPIOB, &gpio);
}
