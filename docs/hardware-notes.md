# RT-Spark hardware notes (pre-bring-up)

**Status: UNVERIFIED.** These facts come from RT-Thread's official RT-Spark BSP
([`bsp/stm32/stm32f407-rt-spark`](https://github.com/RT-Thread/rt-thread/tree/master/bsp/stm32/stm32f407-rt-spark):
`README.md`, `board/Kconfig`, `board/CubeMX_Config/CubeMX_Config.ioc`, `board/linker_scripts/link.lds`).
Check each one against the schematic and the physical board in M2/M3, then move it into
`docs/02-hardware-inventory.md` with its verified status.

## Verified on hardware in M2 (2026-11-01)

| Fact | How verified |
|---|---|
| Onboard debug probe is an **ST-LINK V2-1** (USB `0483:374B`) with a virtual COM port | `pio device list`; SWD flash + verify OK |
| Console = USART1 PA9/PA10 bridged by the ST-LINK VCP, 115200 8N1 | Boot log readable |
| HSE crystal is **8 MHz**; 168 MHz PLL locks from HSE | Boot log `HSE 8 MHz -> PLL`. Correct UART baud depends on `HSE_VALUE` being right |
| STM32F407 with FPU (Cortex-M4F) | `CPACR = 0x00F00000`, hard-float code runs |
| LED_R PF12 / LED_B PF11 active level | **Pending visual check**. See `docs/02-bringup-freertos.md` §7 |

## MCU, clocks, memory

| Item | BSP value | Verify by |
|---|---|---|
| MCU | STM32F407ZGT6, Cortex-M4F | Chip marking |
| HSE crystal | 8 MHz (`RCC.HSE_VALUE=8000000`) | Schematic + PLL lock at 168 MHz |
| LSE crystal | 32.768 kHz on PC14/PC15 | RTC ticks with LSE selected |
| SYSCLK / HCLK | 168 MHz | `HAL_RCC_GetSysClockFreq()` |
| APB1 / APB1 timers | 42 MHz / 84 MHz | `HAL_RCC_GetPCLK1Freq()` |
| APB2 / APB2 timers | 84 MHz / 168 MHz | `HAL_RCC_GetPCLK2Freq()` |
| Flash | 1024 KB @ 0x08000000 | Build size report |
| Main SRAM | 128 KB @ 0x20000000 (DMA-capable) | Linker map |
| CCM RAM | 64 KB @ 0x10000000 (**not DMA-capable**) | Linker map |
| External SRAM (FSMC) | Region @ 0x68000000 in BSP linker script (LCD GRAM / `BSP_USING_SRAM`) | Schematic: is the chip fitted? |

## Debug and console

| Item | BSP value |
|---|---|
| Debug probe | "Standard ST-LINK" (README). SWD on PA13 (SWDIO) / PA14 (SWCLK) |
| Console | USB-to-serial on **USART1**: PA9 TX, PA10 RX, 115200-8-N-1 |

## Onboard components (map to the handout's list)

| Component | BSP notes | Open questions for M2/M3 |
|---|---|---|
| ICM20608 IMU | Software I²C bus `swi2c2` | Pins? Shares the bus with AP3216C, so it needs a **bus mutex** |
| AP3216C light/proximity | Software I²C bus `swi2c2` | Same bus as ICM20608 |
| Temp/humidity | BSP says **AHT21** on `swi2c3`; handout says AHT10 | Read the chip marking. Commands differ slightly (AHT21 init/status) |
| LCD | "ST7787" (likely a typo for ST7789), FSMC, PWM backlight | Controller ID, resolution, FSMC bank/pins |
| LED matrix | `BSP_USING_ONBOARD_LED_MATRIX` exists | LED type (WS2812?), drive method (SPI/PWM+DMA), size |
| LEDs | LED_R = PF12, LED_B = PF11 (README calls LED_B "green") | Colour and active level |
| Buttons | KEY_UP = PC5 (wake-up), KEY_DOWN = PC1, KEY_LEFT = PC0, KEY_RIGHT = PC4 | Active level, pull-ups |
| Buzzer | Not in BSP Kconfig | Locate on schematic (GPIO or PWM pin) |
| SPI flash | README: W25Q128 (16 MB). Kconfig: "W25Q64 spi2" | Read the **JEDEC ID** to settle it |
| Wi-Fi | RW007 on **SPI2** (shares SPI2 with flash) | Fitted? Needs SPI bus mutex. Driver is RT-Thread-specific, so time-box the port |
| microSD/TF | SDIO + FatFs option | Card-detect pin |
| RTC | LSE or LSI selectable | Backup domain / coin cell? |
| EEPROM | Software I²C `swi2c1` | Fitted? Part number? |
| Audio device | `BSP_USING_AUDIO` (play/record) | Codec part, needed for the product? |
| CAN1 | `BSP_USING_CAN1` | Transceiver fitted? |
| RS485 | UART6 with RTS pin | Transceiver fitted? |
| USB | Device (USB mouse demo) and Host (U-disk) | Connector(s) |
| IR | Not in BSP Kconfig | Fitted on your board revision? |
| Ethernet ENC28J60 / ESP8266 | Optional add-on modules | Probably **not fitted**: document as absent |

## Implications already known

- Two sensors share `swi2c2` and two devices share SPI2. Plan the bus mutexes in M3.
- The RT-Thread BSP uses **software (bit-banged) I²C** for the sensors. Check whether those pins map to a hardware I²C peripheral. If they don't, write a GPIO bit-bang I²C driver with HAL/LL.
- `emlearn` tree models take `int16_t` features by default (seen in the M0 smoke test), so M5 needs a feature-quantization step. Alternatively, use a float model.
