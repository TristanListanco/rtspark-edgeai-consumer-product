/*
 * RT-Spark board definitions.
 * Pin facts come from the RT-Thread RT-Spark BSP (board/Kconfig, CubeMX MSP, port
 * drivers, Arduino pin map). RT-Thread pin numbers decode as 16 * port + pin.
 * Verification status is tracked in docs/hardware-notes.md and docs/02-hardware-inventory.md.
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f4xx_hal.h"

/* LEDs: LED_R = PF12 (red), LED_G = PF11 (green; the BSP calls it "LED_B").
 * Both active-low, verified by eye in M2. */
#define LED_R_PORT        GPIOF
#define LED_R_PIN         GPIO_PIN_12
#define LED_G_PORT        GPIOF
#define LED_G_PIN         GPIO_PIN_11
#define LED_ON_LEVEL      GPIO_PIN_RESET

/* Console: USART1 on PA9 (TX) / PA10 (RX), bridged to USB by the onboard ST-LINK V2-1 */
#define CONSOLE_USART     USART1
#define CONSOLE_BAUD      115200U

/* I2C buses
 *  i2c1 (hardware I2C1, PB8 SCL / PB9 SDA): EEPROM (BSP "swi2c1")
 *  i2c2 (hardware I2C2, PF1 SCL / PF0 SDA): ICM20608, AP3216C, ES8388 codec (BSP "swi2c2")
 *  i2c3 (bit-banged,    PE0 SCL / PE1 SDA): AHT temperature/humidity (BSP "swi2c3") */
#define I2C1_SCL_PORT     GPIOB
#define I2C1_SCL_PIN      GPIO_PIN_8
#define I2C1_SDA_PORT     GPIOB
#define I2C1_SDA_PIN      GPIO_PIN_9
#define I2C2_SCL_PORT     GPIOF
#define I2C2_SCL_PIN      GPIO_PIN_1
#define I2C2_SDA_PORT     GPIOF
#define I2C2_SDA_PIN      GPIO_PIN_0
#define I2C3_SCL_PORT     GPIOE
#define I2C3_SCL_PIN      GPIO_PIN_0
#define I2C3_SDA_PORT     GPIOE
#define I2C3_SDA_PIN      GPIO_PIN_1

/* Keys (BSP Arduino map D36-D39) */
#define KEY_UP_PORT       GPIOC
#define KEY_UP_PIN        GPIO_PIN_5
#define KEY_DOWN_PORT     GPIOC
#define KEY_DOWN_PIN      GPIO_PIN_1
#define KEY_LEFT_PORT     GPIOC
#define KEY_LEFT_PIN      GPIO_PIN_0
#define KEY_RIGHT_PORT    GPIOC
#define KEY_RIGHT_PIN     GPIO_PIN_4

/* Buzzer (BSP Arduino map D35) */
#define BUZZER_PORT       GPIOB
#define BUZZER_PIN        GPIO_PIN_0

/* LCD: ST7789V3 240x240 on FSMC bank 1 NE3 (PG10), 8-bit data bus, A18 (PD13) = D/CX.
 * Data D0-D7: PD14 PD15 PD0 PD1 PE7 PE8 PE9 PE10, NOE PD4, NWE PD5. */
#define LCD_RST_PORT      GPIOD
#define LCD_RST_PIN       GPIO_PIN_3
#define LCD_BL_PORT       GPIOF
#define LCD_BL_PIN        GPIO_PIN_9

/* LED matrix: 19 x WS2812 on PA7 (TIM3_CH2, DMA1 Stream5 Ch5), enable PF2 (low = on) */
#define MATRIX_DATA_PORT  GPIOA
#define MATRIX_DATA_PIN   GPIO_PIN_7
#define MATRIX_EN_PORT    GPIOF
#define MATRIX_EN_PIN     GPIO_PIN_2

/* SPI2 (PB13 SCK, PC2 MISO, PC3 MOSI): W25Qxx flash CS PB12; RW007 Wi-Fi CS PF10,
 * RW007 INT/BUSY PG11, RST PG15 (BOOT0 shares SCK, BOOT1 shares its CS) */
#define FLASH_CS_PORT     GPIOB
#define FLASH_CS_PIN      GPIO_PIN_12
#define WIFI_CS_PORT      GPIOF
#define WIFI_CS_PIN       GPIO_PIN_10
#define WIFI_INT_PORT     GPIOG
#define WIFI_INT_PIN      GPIO_PIN_11
#define WIFI_RST_PORT     GPIOG
#define WIFI_RST_PIN      GPIO_PIN_15

/* microSD: SDIO 4-bit, D0-D3 PC8-PC11, CK PC12, CMD PD2 */

void board_led_init(void);
void board_led_set(GPIO_TypeDef *port, uint16_t pin, int on);
void board_led_toggle(GPIO_TypeDef *port, uint16_t pin);

#endif /* BOARD_H */
