/*
 * Buzzer: PB0 = TIM8_CH2N (AF3). TIM8 runs continuously at BUZZER_TONE_HZ; a beep
 * switches PB0 from GPIO output (low = silent) to the timer's alternate function,
 * so the pin is never left floating between beeps.
 *
 *  Task    Prio  Stack (words)  Activation           Communication
 *  buzzer  2     160            pattern queue        buzzer_q (depth 4)
 */
#include "drivers/buzzer.h"

#include <stdbool.h>

#include <string.h>

#include "FreeRTOS.h"
#include "board.h"
#include "queue.h"
#include "task.h"

#define MAX_STEPS 8

typedef struct {
    uint8_t count;
    uint16_t steps[MAX_STEPS];
} pattern_t;

static TIM_HandleTypeDef htim8;
static QueueHandle_t buzzer_q;

static void pin_tone(bool on)
{
    GPIO_InitTypeDef gpio = {.Pin = BUZZER_PIN, .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW};
    if (on) {
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Alternate = GPIO_AF3_TIM8;
    } else {
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
    }
    HAL_GPIO_Init(BUZZER_PORT, &gpio);
}

static void buzzer_task(void *arg)
{
    (void)arg;
    pattern_t p;
    for (;;) {
        xQueueReceive(buzzer_q, &p, portMAX_DELAY);
        for (unsigned i = 0; i < p.count; i++) {
            pin_tone((i % 2U) == 0U);
            vTaskDelay(pdMS_TO_TICKS(p.steps[i]));
        }
        pin_tone(false);
    }
}

void buzzer_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();
    pin_tone(false);

    /* TIM8 on APB2: 168 MHz timer clock -> 1 MHz counter -> BUZZER_TONE_HZ, 50 % duty */
    htim8.Instance = TIM8;
    htim8.Init.Prescaler = (2U * HAL_RCC_GetPCLK2Freq() / 1000000U) - 1U;
    htim8.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim8.Init.Period = (1000000U / BUZZER_TONE_HZ) - 1U;
    htim8.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim8.Init.RepetitionCounter = 0;
    htim8.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim8);

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = (htim8.Init.Period + 1U) / 2U;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(&htim8, &oc, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_2); /* also sets MOE */

    buzzer_q = xQueueCreate(4, sizeof(pattern_t));
    configASSERT(buzzer_q != NULL);
    BaseType_t ok = xTaskCreate(buzzer_task, "buzzer", 160, NULL, 2, NULL);
    configASSERT(ok == pdPASS);
}

int buzzer_play(const uint16_t *steps, unsigned count)
{
    pattern_t p = {.count = (uint8_t)(count > MAX_STEPS ? MAX_STEPS : count)};
    memcpy(p.steps, steps, p.count * sizeof steps[0]);
    return xQueueSend(buzzer_q, &p, 0) == pdPASS;
}

bool buzzer_ok(void)
{
    return (TIM8->CR1 & TIM_CR1_CEN) && (TIM8->BDTR & TIM_BDTR_MOE) && (TIM8->CCER & TIM_CCER_CC2NE);
}

int buzzer_beep(uint16_t ms)
{
    return buzzer_play(&ms, 1);
}
