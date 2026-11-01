# RT-Spark Edge AI Consumer Product

BCA182 Embedded Systems Programming, Laboratory Activity No. 4 (MSU-IIT).

> **Status:** M0 (project setup) is done. The product concept is still to be decided (M1).
> See [PLAN.md](PLAN.md) for milestones.

This is an original consumer product on the RT-Thread **RT-Spark (STM32F407ZGT6)** board. It reads onboard sensors, runs a trained ML model **on the microcontroller**, and responds to the user locally, with no Internet connection required.

**Stack:** PlatformIO · STM32Cube HAL/LL · FreeRTOS · scikit-learn + emlearn

## Repository layout

```
platformio.ini        PlatformIO project (env: rt_spark)
boards/rt_spark.json  Custom RT-Spark board manifest
include/              stm32f4xx_hal_conf.h, FreeRTOSConfig.h (M2), app headers
lib/                  FreeRTOS kernel (M2) and sensor/peripheral drivers (M3)
src/                  Firmware application
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

## License and attribution

- Project code: [MIT](LICENSE)
- STM32CubeF4 HAL/LL and CMSIS: © STMicroelectronics / Arm, under their respective licenses (BSD-3-Clause / Apache-2.0)
- FreeRTOS kernel (added in M2): MIT, © Amazon.com, Inc.
- Board pin and clock facts are taken from the [RT-Thread RT-Spark BSP](https://github.com/RT-Thread/rt-thread/tree/master/bsp/stm32/stm32f407-rt-spark) (Apache-2.0) for reference only. No RT-Thread code is used.
