# RT-Spark Edge AI Consumer Product

BCA182 Embedded Systems Programming, Laboratory Activity No. 4 (MSU-IIT).

> **Status:** M0–M3 done. Concept: **KeepSafe Box** ([docs/01-concept.md](docs/01-concept.md)). FreeRTOS bring-up: [docs/02-bringup-freertos.md](docs/02-bringup-freertos.md). Verified hardware + diagnostics: [docs/02-hardware-inventory.md](docs/02-hardware-inventory.md). Next: M4 dataset capture.
> See [PLAN.md](PLAN.md) for milestones.

This is an original consumer product on the RT-Thread **RT-Spark (STM32F407ZGT6)** board. It reads onboard sensors, runs a trained ML model **on the microcontroller**, and responds to the user locally, with no Internet connection required.

**Stack:** PlatformIO (`ststm32@20.0.0`, GCC 14.2.1) · STM32CubeF4 1.28.3 HAL/LL · FreeRTOS V11.3.2 · scikit-learn + emlearn

## Repository layout

```
platformio.ini        PlatformIO project (env: rt_spark)
boards/rt_spark.json  Custom RT-Spark board manifest
include/              stm32f4xx_hal_conf.h, FreeRTOSConfig.h, board/app headers
lib/FreeRTOS/         FreeRTOS-Kernel V11.3.2 (vendored subset: ARM_CM4F port, heap_4)
src/system/           Clock tree, HAL timebase (TIM6), fault handlers, RTOS hooks, syscalls
src/drivers/          I2C buses, ICM20608, AP3216C, AHT2x, ST7789 LCD, WS2812, buzzer, keys,
                      RTC, SPI2 + W25Qxx, SDIO microSD, RW007 probe (headers in include/drivers/)
src/app/              init, sensors, UI, CLI, diagnostics, monitor, I/O probes
scripts/              fpu.py (hard-float build), serial_capture.py (log capture + scripted CLI),
                      gen_font.py (LCD font from DejaVu Sans Mono)
ml/                   Data capture, feature extraction, training, model export
data/                 Data manifest, protocol, anonymized samples
docs/                 Report, diagrams, test plan, evidence logs
```

## Build and flash

Prerequisites: VS Code with the PlatformIO IDE extension, or PlatformIO Core. The first build downloads `ststm32@20.0.0` and the toolchain automatically.

```sh
pio run                  # build
pio run -t upload        # flash via ST-LINK (SWD)
pio device monitor       # serial console, 115200 8N1 (USART1, USB-to-serial)
```

## Diagnostics and service CLI

Connect to the ST-LINK virtual COM port (115200 8N1), e.g. `pio device monitor`. On the board, hold **DOWN** for 1.5 s to see the self-test page.

| Command | Purpose |
|---|---|
| `help` | list commands |
| `diag` | live self-test of every component (console + LCD page) |
| `live on` / `live off` | 1 Hz timestamped sensor readings (+ RTOS stats every 30 s) |
| `stats` | FreeRTOS heap / stack / CPU table |
| `rtc set YYYY-MM-DD HH:MM:SS` | set the clock (kept across resets) |
| `flash test` | write/verify/erase test on the blank last flash sector |
| `calib` | IMU offsets and noise at rest |
| `i2cscan`, `ioprobe`, `sd`, `beep`, `reboot` | scans, Wi-Fi/CAN/RS485 probes, SD re-detect, buzzer, reset |

Scripted evidence capture (types commands for you):

```sh
.venv/bin/python scripts/serial_capture.py --port <VCP port> --seconds 40 --out log.txt --send diag --send stats
```

## ML environment

```sh
uv venv .venv --python 3.12
uv pip install --python .venv/bin/python -r ml/requirements.txt
```

Without `uv`: `python3.12 -m venv .venv && .venv/bin/pip install -r ml/requirements.txt`.

## Hardware

See [docs/hardware-notes.md](docs/hardware-notes.md). The verified inventory goes in `docs/02-hardware-inventory.md` once M3 is done.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Editor shows `'stm32f4xx_hal.h' file not found` | Run `pio run -t compiledb` (clangd) or let the PlatformIO extension rebuild IntelliSense |
| `ModuleNotFoundError: distutils` when importing emlearn | Install `setuptools` (already pinned in `ml/requirements.txt`) |
| clangd shows `-Wformat` warnings for `%lu` with `uint32_t` | It is analysing for the host. `.clangd` sets `--target=arm-none-eabi`; restart clangd |
| Firmware hangs in a HAL timeout at boot | Don't call HAL_Delay/HAL timeouts between the first FreeRTOS API call and `vTaskStartScheduler()` (interrupts are masked); do hardware init in a task |
| Board prints nothing on the serial port | Use the ST-LINK VCP port (`pio device list`, "STM32 STLink"), 115200 8N1. Press RESET to see the boot banner |
| `This port can only be used when the project options are configured to enable hardware floating point support` | `scripts/fpu.py` is missing from `extra_scripts` |

## License and attribution

- Project code: [MIT](LICENSE)
- STM32CubeF4 HAL/LL and CMSIS: © STMicroelectronics / Arm, under their respective licenses (BSD-3-Clause / Apache-2.0)
- FreeRTOS-Kernel V11.3.2 (`lib/FreeRTOS`, unmodified subset): MIT, © Amazon.com, Inc. See `lib/FreeRTOS/LICENSE.md`
- LCD font (`src/drivers/font8x16.c`): rendered from DejaVu Sans Mono Bold (Bitstream Vera license + public-domain DejaVu changes)
- ST7789V3 init values, FSMC timing, WS2812 timing and RW007 handshake constants follow the RT-Thread RT-Spark BSP and rw007 package (Apache-2.0), re-implemented on STM32 HAL
- Board pin and clock facts are taken from the [RT-Thread RT-Spark BSP](https://github.com/RT-Thread/rt-thread/tree/master/bsp/stm32/stm32f407-rt-spark) (Apache-2.0) for reference only. No RT-Thread code is used.
