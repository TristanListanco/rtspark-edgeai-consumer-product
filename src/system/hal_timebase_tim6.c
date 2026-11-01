/*
 * HAL timebase on TIM6 instead of SysTick, so FreeRTOS can own SysTick.
 * Overrides the weak HAL_InitTick/HAL_SuspendTick/HAL_ResumeTick in stm32f4xx_hal.c.
 * HAL_RCC_ClockConfig() calls HAL_InitTick() again after the PLL switch, so the
 * prescaler is always recomputed from the current APB1 timer clock.
 *
 * The ISR clears the flag directly instead of going through HAL_TIM_IRQHandler,
 * so HAL_TIM_PeriodElapsedCallback stays free for application timers.
 * TIM6 never calls FreeRTOS APIs, so its priority (TICK_INT_PRIORITY) is unrestricted.
 */
#include "clock.h"
#include "stm32f4xx_hal.h"

static TIM_HandleTypeDef htim6;

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    if (TickPriority >= (1UL << __NVIC_PRIO_BITS)) {
        return HAL_ERROR;
    }

    __HAL_RCC_TIM6_CLK_ENABLE();

    /* 1 MHz counter, update every 1 ms (uwTickFreq = 1 kHz) */
    htim6.Instance = TIM6;
    htim6.Init.Prescaler = (clock_apb1_timer_hz() / 1000000U) - 1U;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period = (1000U * (uint32_t)uwTickFreq) - 1U;
    htim6.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim6) != HAL_OK) {
        return HAL_ERROR;
    }

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, TickPriority, 0U);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    uwTickPrio = TickPriority;

    if (htim6.State == HAL_TIM_STATE_READY) {
        return HAL_TIM_Base_Start_IT(&htim6);
    }
    return HAL_OK;
}

void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&htim6, TIM_IT_UPDATE);
}

void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&htim6, TIM_IT_UPDATE);
}

void TIM6_DAC_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&htim6, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
        HAL_IncTick();
    }
}
