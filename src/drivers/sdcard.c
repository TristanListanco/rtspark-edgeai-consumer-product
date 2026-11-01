/*
 * SDIO: D0-D3 PC8-PC11, CK PC12, CMD PD2 (AF12). SDIOCLK = PLLQ 48 MHz;
 * identification at 400 kHz (HAL), then 48 / (2 + 2) = 12 MHz, 4-bit bus.
 * Transfers use DMA and block the calling task on a notification until the
 * HAL completion/error callback fires, so other tasks keep running.
 * IRQ priorities 6 (>= configMAX_SYSCALL_INTERRUPT_PRIORITY: callbacks notify tasks).
 */
#include "drivers/sdcard.h"

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "stm32f4xx_hal.h"
#include "task.h"

#define SD_IRQ_PRIO     6U
#define XFER_TIMEOUT_MS 1000U

static SD_HandleTypeDef hsd;
static DMA_HandleTypeDef hdma_rx;
static DMA_HandleTypeDef hdma_tx;
static SemaphoreHandle_t sd_lock;
static TaskHandle_t waiter;
static volatile bool xfer_error;
static sd_info_t info;
static uint8_t block0[512] __attribute__((aligned(4)));

static void dma_setup(DMA_HandleTypeDef *h, DMA_Stream_TypeDef *stream, uint32_t dir)
{
    h->Instance = stream;
    h->Init.Channel = DMA_CHANNEL_4;
    h->Init.Direction = dir;
    h->Init.PeriphInc = DMA_PINC_DISABLE;
    h->Init.MemInc = DMA_MINC_ENABLE;
    h->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    h->Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    h->Init.Mode = DMA_PFCTRL; /* SDIO is the flow controller */
    h->Init.Priority = DMA_PRIORITY_VERY_HIGH;
    h->Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    h->Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    h->Init.MemBurst = DMA_MBURST_INC4;
    h->Init.PeriphBurst = DMA_PBURST_INC4;
    HAL_DMA_DeInit(h);
    HAL_DMA_Init(h);
}

static void hw_setup(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {.Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP, .Speed = GPIO_SPEED_FREQ_VERY_HIGH,
                             .Alternate = GPIO_AF12_SDIO};
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_12;
    gpio.Pull = GPIO_NOPULL; /* clock */
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_2;
    gpio.Pull = GPIO_PULLUP; /* CMD */
    HAL_GPIO_Init(GPIOD, &gpio);

    dma_setup(&hdma_rx, DMA2_Stream3, DMA_PERIPH_TO_MEMORY);
    dma_setup(&hdma_tx, DMA2_Stream6, DMA_MEMORY_TO_PERIPH);
    __HAL_LINKDMA(&hsd, hdmarx, hdma_rx);
    __HAL_LINKDMA(&hsd, hdmatx, hdma_tx);

    HAL_NVIC_SetPriority(SDIO_IRQn, SD_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, SD_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, SD_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);
}

static bool wait_transfer(void)
{
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(XFER_TIMEOUT_MS)) == 0 || xfer_error) {
        return false;
    }
    uint32_t start = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - start > XFER_TIMEOUT_MS) return false;
        vTaskDelay(1);
    }
    return true;
}

sd_info_t sd_init(void)
{
    if (sd_lock == NULL) {
        sd_lock = xSemaphoreCreateMutex();
        configASSERT(sd_lock != NULL);
        hw_setup();
    }
    memset(&info, 0, sizeof info);

    hsd.Instance = SDIO;
    hsd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    hsd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    hsd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide = SDIO_BUS_WIDE_1B; /* identification is always 1-bit */
    hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd.Init.ClockDiv = 2;
    hsd.State = HAL_SD_STATE_RESET;

    xSemaphoreTake(sd_lock, portMAX_DELAY);
    if (HAL_SD_Init(&hsd) != HAL_OK || HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B) != HAL_OK) {
        info.error = (int)hsd.ErrorCode;
        HAL_SD_DeInit(&hsd);
        xSemaphoreGive(sd_lock);
        return info;
    }
    HAL_SD_CardInfoTypeDef ci;
    HAL_SD_CardCIDTypeDef cid;
    HAL_SD_GetCardInfo(&hsd, &ci);
    HAL_SD_GetCardCID(&hsd, &cid);
    xSemaphoreGive(sd_lock);

    info.present = true;
    info.blocks = ci.LogBlockNbr;
    info.type = (uint8_t)ci.CardType;
    info.manufacturer = cid.ManufacturerID;
    info.product[0] = (char)(cid.ProdName1 >> 24);
    info.product[1] = (char)(cid.ProdName1 >> 16);
    info.product[2] = (char)(cid.ProdName1 >> 8);
    info.product[3] = (char)cid.ProdName1;
    info.product[4] = (char)cid.ProdName2;
    info.product[5] = '\0';

    /* Read-only check: block 0 should be an MBR or a FAT boot sector */
    if (sd_read_blocks(block0, 0, 1)) {
        info.mbr_signature = block0[510] == 0x55 && block0[511] == 0xAA;
        info.partition_type = block0[0x1C2];
    }
    return info;
}

const sd_info_t *sd_info(void)
{
    return &info;
}

bool sd_read_blocks(uint8_t *buf, uint32_t block, uint32_t count)
{
    if (!info.present && hsd.State != HAL_SD_STATE_READY) return false;
    xSemaphoreTake(sd_lock, portMAX_DELAY);
    waiter = xTaskGetCurrentTaskHandle();
    xfer_error = false;
    (void)ulTaskNotifyTake(pdTRUE, 0); /* clear any stale notification */
    bool ok = HAL_SD_ReadBlocks_DMA(&hsd, buf, block, count) == HAL_OK && wait_transfer();
    waiter = NULL;
    xSemaphoreGive(sd_lock);
    return ok;
}

bool sd_write_blocks(const uint8_t *buf, uint32_t block, uint32_t count)
{
    if (!info.present) return false;
    xSemaphoreTake(sd_lock, portMAX_DELAY);
    waiter = xTaskGetCurrentTaskHandle();
    xfer_error = false;
    (void)ulTaskNotifyTake(pdTRUE, 0);
    bool ok = HAL_SD_WriteBlocks_DMA(&hsd, (uint8_t *)buf, block, count) == HAL_OK && wait_transfer();
    waiter = NULL;
    xSemaphoreGive(sd_lock);
    return ok;
}

static void notify_from_isr(void)
{
    if (waiter != NULL) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(waiter, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *h) { (void)h; notify_from_isr(); }
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *h) { (void)h; notify_from_isr(); }
void HAL_SD_ErrorCallback(SD_HandleTypeDef *h)
{
    (void)h;
    xfer_error = true;
    notify_from_isr();
}

void SDIO_IRQHandler(void) { HAL_SD_IRQHandler(&hsd); }
void DMA2_Stream3_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_rx); }
void DMA2_Stream6_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_tx); }
