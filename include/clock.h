#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

typedef enum {
    CLOCK_SRC_HSE_PLL,  /* 8 MHz crystal -> PLL -> 168 MHz (normal) */
    CLOCK_SRC_HSI_PLL,  /* crystal failed: 16 MHz HSI -> PLL -> 168 MHz */
    CLOCK_SRC_HSI_ONLY, /* PLL failed: running on 16 MHz HSI */
} clock_source_t;

clock_source_t clock_init(void);
const char *clock_source_name(clock_source_t src);
uint32_t clock_apb1_timer_hz(void);

#endif /* CLOCK_H */
