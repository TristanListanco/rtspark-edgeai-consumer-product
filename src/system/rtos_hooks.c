/*
 * FreeRTOS hooks and the run-time statistics counter.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "clock.h"
#include "console.h"
#include "fault.h"
#include "stm32f4xx_hal.h"

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    console_panic("STACK OVERFLOW in task '%s'", name);
    fault_halt();
}

void vApplicationMallocFailedHook(void)
{
    console_panic("MALLOC FAILED: FreeRTOS heap exhausted (free %u B)", (unsigned)xPortGetFreeHeapSize());
    fault_halt();
}

void vAssertCalled(const char *file, int line)
{
    console_panic("ASSERT failed at %s:%d", file, line);
    fault_halt();
}

/* Run-time stats counter: TIM5 (32-bit) at 1 MHz, extended to 64 bits by counting
 * overflows (every ~71.6 min). 1 us resolution is needed because a 10 kHz counter is
 * phase-locked to the 1 kHz tick and reports short periodic tasks as 0 % CPU.
 * The overflow IRQ runs at priority 4 (above configMAX_SYSCALL_INTERRUPT_PRIORITY)
 * so the kernel never masks it while reading the counter; it calls no FreeRTOS API. */
#define STATS_TIMER_HZ       1000000U
#define STATS_TIMER_IRQ_PRIO 4U

static volatile uint32_t stats_overflows;

void rtos_stats_timer_init(void)
{
    __HAL_RCC_TIM5_CLK_ENABLE();
    TIM5->CR1 = 0;
    TIM5->PSC = (clock_apb1_timer_hz() / STATS_TIMER_HZ) - 1U;
    TIM5->ARR = 0xFFFFFFFFU;
    TIM5->CNT = 0;
    TIM5->EGR = TIM_EGR_UG; /* load the prescaler now (also sets UIF) */
    TIM5->SR = 0;
    TIM5->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM5_IRQn, STATS_TIMER_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(TIM5_IRQn);
    TIM5->CR1 = TIM_CR1_CEN;
}

void TIM5_IRQHandler(void)
{
    if (TIM5->SR & TIM_SR_UIF) {
        TIM5->SR = ~TIM_SR_UIF;
        stats_overflows++;
    }
}

uint64_t rtos_stats_timer_value(void)
{
    uint32_t hi;
    uint32_t lo;
    do {
        hi = stats_overflows;
        lo = TIM5->CNT;
    } while (hi != stats_overflows);
    return ((uint64_t)hi << 32) | lo;
}
