# References

## Handout-listed

1. PlatformIO, STM32Cube framework: https://docs.platformio.org/en/stable/frameworks/stm32cube.html
2. PlatformIO, ST STM32 platform and board configuration: https://docs.platformio.org/en/latest/platforms/ststm32.html
3. FreeRTOS kernel documentation: https://www.freertos.org/Documentation/RTOS_book.html
4. STMicroelectronics, STM32CubeF4: https://www.st.com/en/embedded-software/stm32cubef4.html
5. RT-Spark schematic and module datasheets (wiring/pinout and electrical specs only). The schematic comes from the instructor/lab kit.

## Board

- RT-Thread RT-Spark BSP (pin/clock facts used in `docs/hardware-notes.md`): https://github.com/RT-Thread/rt-thread/tree/master/bsp/stm32/stm32f407-rt-spark

## STM32F407

- Product page and documents: https://www.st.com/en/microcontrollers-microprocessors/stm32f407zg.html
  - DS8626: STM32F405xx/STM32F407xx datasheet
  - RM0090: STM32F4 reference manual (peripherals, DMA, memory map)
  - PM0214: Cortex-M4 programming manual (NVIC priorities, SysTick, DWT)
  - UM1725: STM32F4 HAL and LL driver description
- STM32CubeF4 source (version 1.28.3 is used through PlatformIO `framework-stm32cubef4`): https://github.com/STMicroelectronics/STM32CubeF4

## PlatformIO

- Custom board manifests: https://docs.platformio.org/en/latest/platforms/creating_board.html
- `platformio.ini` options: https://docs.platformio.org/en/latest/projectconf/index.html

## FreeRTOS

- FreeRTOS-Kernel source: https://github.com/FreeRTOS/FreeRTOS-Kernel
- Cortex-M interrupt priorities (`configMAX_SYSCALL_INTERRUPT_PRIORITY`): https://www.freertos.org/RTOS-Cortex-M3-M4.html
- Stack overflow checking: https://www.freertos.org/Stacks-and-stack-overflow-checking.html

## Sensor datasheets (get the PDFs from the manufacturers)

- TDK InvenSense ICM-20608-G (6-axis IMU)
- Aosong AHT10 / AHT21 (temperature/humidity). Confirm which one is fitted.
- Dyna Image AP3216C (ambient light / proximity)
- Winbond W25Q64 / W25Q128 (SPI NOR flash). Confirm via JEDEC ID.
- Sitronix ST7789 (LCD controller). Confirm the controller.

## Machine learning

- scikit-learn: https://scikit-learn.org/stable/
- emlearn (sklearn → C for microcontrollers): https://github.com/emlearn/emlearn
- Edge Impulse (alternative): https://docs.edgeimpulse.com
