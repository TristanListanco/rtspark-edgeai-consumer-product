/*
 * M0 toolchain smoke test: HAL init + blink LED_R (PF12) on the default 16 MHz HSI.
 * Replaced in M2 by the 168 MHz clock tree and the FreeRTOS scheduler
 * (which takes over SysTick; HAL moves to a TIM timebase).
 */
#include "stm32f4xx_hal.h"

#define LED_R_PORT GPIOF
#define LED_R_PIN  GPIO_PIN_12

void SysTick_Handler(void)
{
    HAL_IncTick();
}

int main(void)
{
    HAL_Init();

    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {
        .Pin = LED_R_PIN,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };
    HAL_GPIO_Init(LED_R_PORT, &gpio);

    for (;;) {
        HAL_GPIO_TogglePin(LED_R_PORT, LED_R_PIN);
        HAL_Delay(500);
    }
}
