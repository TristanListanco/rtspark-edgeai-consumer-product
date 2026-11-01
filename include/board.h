/*
 * RT-Spark board definitions.
 * Pin facts come from the RT-Thread RT-Spark BSP; verification status is tracked
 * in docs/hardware-notes.md and docs/02-hardware-inventory.md.
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f4xx_hal.h"

/* LEDs (BSP: LED_R = PF12, LED_B = PF11) */
#define LED_R_PORT        GPIOF
#define LED_R_PIN         GPIO_PIN_12
#define LED_B_PORT        GPIOF
#define LED_B_PIN         GPIO_PIN_11
#define LED_ON_LEVEL      GPIO_PIN_RESET /* active-low: confirmed or corrected during M2 bring-up */

/* Console: USART1 on PA9 (TX) / PA10 (RX), bridged to USB by the onboard ST-LINK V2-1 */
#define CONSOLE_USART     USART1
#define CONSOLE_BAUD      115200U

void board_led_init(void);
void board_led_set(GPIO_TypeDef *port, uint16_t pin, int on);
void board_led_toggle(GPIO_TypeDef *port, uint16_t pin);

#endif /* BOARD_H */
