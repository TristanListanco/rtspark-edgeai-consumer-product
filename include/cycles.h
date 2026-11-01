/* DWT cycle counter: 1 count = 1 CPU cycle (5.95 ns at 168 MHz), wraps every ~25.6 s.
 * Used for inference timing and sampling-jitter measurement. */
#ifndef CYCLES_H
#define CYCLES_H

#include "stm32f4xx.h"

static inline void cycles_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t cycles_now(void)
{
    return DWT->CYCCNT;
}

static inline uint32_t cycles_to_us(uint32_t cycles)
{
    return cycles / (SystemCoreClock / 1000000U);
}

#endif /* CYCLES_H */
