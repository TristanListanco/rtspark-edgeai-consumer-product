/*
 * Cortex-M fault handlers. SVC_Handler, PendSV_Handler and SysTick_Handler are
 * provided by the FreeRTOS port (mapped in FreeRTOSConfig.h).
 * MemManage/BusFault/UsageFault are not enabled separately, so they escalate to HardFault.
 */
#include "board.h"
#include "console.h"
#include "fault.h"

void fault_halt(void)
{
    __disable_irq();
    for (;;) {
        HAL_GPIO_TogglePin(LED_R_PORT, LED_R_PIN);
        for (volatile uint32_t i = 0; i < 2000000U; i++) {
        }
    }
}

/* Called from HardFault_Handler with the stacked exception frame:
 * r0, r1, r2, r3, r12, lr, pc, xpsr */
__attribute__((used)) void hardfault_report(const uint32_t *frame)
{
    console_panic("HARDFAULT pc=0x%08lx lr=0x%08lx psr=0x%08lx", frame[6], frame[5], frame[7]);
    console_panic("  CFSR=0x%08lx HFSR=0x%08lx MMFAR=0x%08lx BFAR=0x%08lx",
                  SCB->CFSR, SCB->HFSR, SCB->MMFAR, SCB->BFAR);
    fault_halt();
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4            \n" /* EXC_RETURN bit 2: which stack was in use? */
        "ite eq                \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "b hardfault_report    \n");
}

void NMI_Handler(void)
{
    console_panic("NMI");
    fault_halt();
}
