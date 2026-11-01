/*
 * WS2812 driver: one PWM period per data bit, duty cycles streamed into TIM3->CCR2
 * by DMA1 Stream5 (channel 5 = TIM3_CH2). Timing (from the RT-Spark BSP):
 *   84 MHz / 10 / 10 = 840 kHz -> 1.19 us bit; "0" = 2 counts high (238 ns),
 *   "1" = 7 counts high (833 ns). Trailing zero-duty slots give the >50 us reset.
 * The DMA buffer is a normal .bss array, i.e. in main SRAM (DMA cannot reach CCM).
 */
#include "drivers/ws2812.h"

#include <string.h>

#include "board.h"

#define BITS_PER_LED  24U
#define RESET_SLOTS   64U /* 64 x 1.19 us = 76 us low */
#define DUTY_0        2U
#define DUTY_1        7U
#define DMA_IRQ_PRIO  6U

static TIM_HandleTypeDef htim3;
static DMA_HandleTypeDef hdma_tim3_ch2;
static uint8_t grb[WS2812_COUNT][3];
static uint16_t dma_buf[WS2812_COUNT * BITS_PER_LED + RESET_SLOTS];
static volatile bool busy;
static volatile uint32_t frames_done;

void ws2812_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {.Pin = MATRIX_DATA_PIN, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_NOPULL,
                             .Speed = GPIO_SPEED_FREQ_HIGH, .Alternate = GPIO_AF2_TIM3};
    HAL_GPIO_Init(MATRIX_DATA_PORT, &gpio);

    HAL_GPIO_WritePin(MATRIX_EN_PORT, MATRIX_EN_PIN, GPIO_PIN_RESET); /* enable */
    gpio = (GPIO_InitTypeDef){.Pin = MATRIX_EN_PIN, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
                              .Speed = GPIO_SPEED_FREQ_LOW};
    HAL_GPIO_Init(MATRIX_EN_PORT, &gpio);

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 10 - 1;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 10 - 1;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim3);

    TIM_OC_InitTypeDef oc = {.OCMode = TIM_OCMODE_PWM1, .Pulse = 0, .OCPolarity = TIM_OCPOLARITY_HIGH,
                             .OCFastMode = TIM_OCFAST_DISABLE};
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_2);
    __HAL_TIM_ENABLE_OCxPRELOAD(&htim3, TIM_CHANNEL_2); /* new duty takes effect each period */

    hdma_tim3_ch2.Instance = DMA1_Stream5;
    hdma_tim3_ch2.Init.Channel = DMA_CHANNEL_5;
    hdma_tim3_ch2.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_tim3_ch2.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tim3_ch2.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim3_ch2.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_tim3_ch2.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_tim3_ch2.Init.Mode = DMA_NORMAL;
    hdma_tim3_ch2.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_tim3_ch2.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_tim3_ch2);
    __HAL_LINKDMA(&htim3, hdma[TIM_DMA_ID_CC2], hdma_tim3_ch2);

    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, DMA_IRQ_PRIO, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);

    memset(grb, 0, sizeof grb);
    ws2812_show(); /* all off */
}

void ws2812_set(unsigned index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < WS2812_COUNT) {
        grb[index][0] = g;
        grb[index][1] = r;
        grb[index][2] = b;
    }
}

void ws2812_fill(uint8_t r, uint8_t g, uint8_t b)
{
    for (unsigned i = 0; i < WS2812_COUNT; i++) {
        ws2812_set(i, r, g, b);
    }
}

bool ws2812_show(void)
{
    if (busy) {
        return false;
    }
    uint16_t *p = dma_buf;
    for (unsigned led = 0; led < WS2812_COUNT; led++) {
        for (unsigned c = 0; c < 3; c++) {
            for (int bit = 7; bit >= 0; bit--) {
                *p++ = (grb[led][c] & (1U << bit)) ? DUTY_1 : DUTY_0;
            }
        }
    }
    memset(p, 0, RESET_SLOTS * sizeof *p);
    busy = true;
    if (HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_2, (uint32_t *)dma_buf,
                              (uint16_t)(sizeof dma_buf / sizeof dma_buf[0])) != HAL_OK) {
        busy = false;
        return false;
    }
    return true;
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3) {
        HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_2);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0); /* hold the line low */
        busy = false;
        frames_done++;
    }
}

uint32_t ws2812_frames_done(void)
{
    return frames_done;
}

void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_tim3_ch2);
}
