/*
 * KeepSafe Box firmware entry point (RT-Spark, STM32F407ZGT6).
 * Boot order: HAL (NVIC group 4, TIM6 HAL tick) -> 168 MHz clock tree ->
 * cycle counter, LEDs, console -> boot report -> tasks -> FreeRTOS scheduler.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "bringup_tasks.h"
#include "clock.h"
#include "console.h"
#include "cycles.h"
#include "fault.h"

/* Flags accumulate until cleared: a power-on sets POR+BOR+PIN, a debugger or
 * NVIC_SystemReset sets SFT+PIN. Check the most specific cause first. */
static const char *reset_cause(uint32_t csr)
{
    if (csr & RCC_CSR_IWDGRSTF) return "independent watchdog";
    if (csr & RCC_CSR_WWDGRSTF) return "window watchdog";
    if (csr & RCC_CSR_LPWRRSTF) return "low-power";
    if (csr & RCC_CSR_SFTRSTF)  return "software (debugger/NVIC)";
    if (csr & RCC_CSR_PORRSTF)  return "power-on";
    if (csr & RCC_CSR_BORRSTF)  return "brown-out";
    if (csr & RCC_CSR_PINRSTF)  return "reset pin";
    return "unknown";
}

static void boot_report(clock_source_t clk)
{
    uint32_t hal = HAL_GetHalVersion();
    uint32_t csr = RCC->CSR;
    __HAL_RCC_CLEAR_RESET_FLAGS();

    console_printf("==== KeepSafe Box / RT-Spark bring-up (M2) ====");
    console_printf("build: %s %s, gcc %s", __DATE__, __TIME__, __VERSION__);
    console_printf("stack: STM32CubeF4 HAL v%lu.%lu.%lu, FreeRTOS %s", hal >> 24, (hal >> 16) & 0xFFU,
                   (hal >> 8) & 0xFFU, tskKERNEL_VERSION_NUMBER);
    console_printf("reset cause: %s (RCC_CSR=0x%08lx)", reset_cause(csr), csr);
    console_printf("clock: %s, SYSCLK %lu Hz, HCLK %lu Hz, PCLK1 %lu Hz, PCLK2 %lu Hz",
                   clock_source_name(clk), HAL_RCC_GetSysClockFreq(), HAL_RCC_GetHCLKFreq(),
                   HAL_RCC_GetPCLK1Freq(), HAL_RCC_GetPCLK2Freq());
    console_printf("FPU: CPACR=0x%08lx (0x00F00000 = CP10/CP11 full access)", SCB->CPACR);
    console_printf("FreeRTOS heap %u B, tick %u Hz, max syscall IRQ prio %u",
                   (unsigned)configTOTAL_HEAP_SIZE, (unsigned)configTICK_RATE_HZ,
                   (unsigned)configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
}

int main(void)
{
    HAL_Init();
    clock_source_t clk = clock_init();
    cycles_init();
    board_led_init();
    console_init();

    boot_report(clk);

    console_rtos_init();
    bringup_tasks_start();

    console_printf("starting scheduler (%u B heap free)", (unsigned)xPortGetFreeHeapSize());
    vTaskStartScheduler();

    /* Only reached if the idle or timer task could not be allocated */
    console_panic("scheduler returned: not enough heap for idle/timer task");
    fault_halt();
}
