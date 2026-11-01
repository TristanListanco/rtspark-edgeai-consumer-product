/*
 * Clock tree (matches the RT-Thread BSP CubeMX configuration):
 *   HSE 8 MHz / PLLM 8 * PLLN 336 = VCO 336 MHz
 *   SYSCLK = VCO / PLLP 2 = 168 MHz, USB/SDIO = VCO / PLLQ 7 = 48 MHz
 *   AHB /1 = 168 MHz, APB1 /4 = 42 MHz (timers 84 MHz), APB2 /2 = 84 MHz (timers 168 MHz)
 * If the crystal does not start, the same VCO is built from the 16 MHz HSI.
 */
#include "clock.h"
#include "stm32f4xx_hal.h"

static HAL_StatusTypeDef pll_from(uint32_t pll_source, uint32_t pllm)
{
    RCC_OscInitTypeDef osc = {0};

    if (pll_source == RCC_PLLSOURCE_HSE) {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        osc.HSEState = RCC_HSE_ON;
    } else {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
        osc.HSEState = RCC_HSE_OFF;
        osc.HSIState = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    }
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = pll_source;
    osc.PLL.PLLM = pllm;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    return HAL_RCC_OscConfig(&osc);
}

clock_source_t clock_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    clock_source_t src = CLOCK_SRC_HSE_PLL;
    if (pll_from(RCC_PLLSOURCE_HSE, 8) != HAL_OK) {
        src = CLOCK_SRC_HSI_PLL;
        if (pll_from(RCC_PLLSOURCE_HSI, 16) != HAL_OK) {
            return CLOCK_SRC_HSI_ONLY;
        }
    }

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    /* 5 wait states for 168 MHz at 2.7-3.6 V (RM0090 Table 10) */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        return CLOCK_SRC_HSI_ONLY;
    }
    return src;
}

const char *clock_source_name(clock_source_t src)
{
    switch (src) {
    case CLOCK_SRC_HSE_PLL: return "HSE 8 MHz -> PLL";
    case CLOCK_SRC_HSI_PLL: return "HSI 16 MHz -> PLL (HSE FAILED)";
    default:                return "HSI 16 MHz only (PLL FAILED)";
    }
}

uint32_t clock_apb1_timer_hz(void)
{
    /* Timers on APB1 run at 2x PCLK1 whenever the APB1 prescaler is not 1 */
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    return ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1) ? pclk1 : 2U * pclk1;
}
