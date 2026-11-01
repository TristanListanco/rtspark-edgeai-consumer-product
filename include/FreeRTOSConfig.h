/*
 * FreeRTOS configuration for RT-Spark (STM32F407ZGT6, Cortex-M4F @ 168 MHz).
 * Kernel: FreeRTOS-Kernel V11.3.2, port GCC/ARM_CM4F, heap_4.
 *
 * Timebase split (see src/system/hal_timebase_tim6.c):
 *   SysTick -> FreeRTOS tick (1 kHz), owned by the kernel port.
 *   TIM6    -> HAL tick (HAL_GetTick/HAL_Delay), so HAL never touches SysTick.
 * Run-time stats counter: TIM5 at 1 MHz, extended to 64 bits (src/system/rtos_hooks.c).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#ifndef __ASSEMBLER__
#include <stdint.h>
extern uint32_t SystemCoreClock;
void vAssertCalled(const char *file, int line);
void rtos_stats_timer_init(void);
uint64_t rtos_stats_timer_value(void);
#endif

/* ---- Scheduler ---------------------------------------------------------- */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                128 /* words */
#define configMAX_TASK_NAME_LEN                 12
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1

/* ---- Synchronisation primitives ---------------------------------------- */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0

/* ---- Memory ------------------------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
/* heap_4 array lives in main SRAM (DMA-capable). Tune from measured
 * xPortGetMinimumEverFreeHeapSize() in M6/M7. */
#define configTOTAL_HEAP_SIZE                   ((size_t)(64 * 1024))
#define configAPPLICATION_ALLOCATED_HEAP        0

/* ---- Hooks and safety nets --------------------------------------------- */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configCHECK_HANDLER_INSTALLATION        1
#define configASSERT(x)                         do { if (!(x)) { vAssertCalled(__FILE__, __LINE__); } } while (0)

/* ---- Diagnostics: trace, stack high-water marks, run-time stats --------
 * Stats are formatted by the application from uxTaskGetSystemState(): the
 * kernel's formatting functions print the 64-bit counter with %lu. */
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           1
#define configRUN_TIME_COUNTER_TYPE             uint64_t
#define configRECORD_STACK_HIGH_ADDRESS         1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() rtos_stats_timer_init()
#define portGET_RUN_TIME_COUNTER_VALUE()        rtos_stats_timer_value()

/* ---- Software timers ---------------------------------------------------- */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

/* ---- Cortex-M interrupt priorities --------------------------------------
 * STM32F4 implements 4 priority bits (0 = most urgent, 15 = least).
 * HAL_Init() selects NVIC_PRIORITYGROUP_4 (all bits pre-emption).
 * Any ISR that calls a ...FromISR() API must use priority 5..15.
 * ISRs at 0..4 are never masked by the kernel but must not call FreeRTOS APIs. */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                         __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                         4
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ---- Optional API functions -------------------------------------------- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1

/* ---- Map the port's handlers onto the CMSIS vector names ---------------
 * The startup file's vector table points at SVC_Handler / PendSV_Handler /
 * SysTick_Handler; these defines make the kernel provide them directly.
 * Do NOT define SysTick_Handler anywhere else (HAL uses TIM6). */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
