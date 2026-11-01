# M2: Board Bring-up with PlatformIO, STM32Cube HAL and FreeRTOS

**Status:** done on real hardware, 2026-11-01. All numbers below come from
[`evidence/m2/serial-boot.log`](evidence/m2/serial-boot.log) (70 s capture after a debugger reset),
[`evidence/m2/build.log`](evidence/m2/build.log), [`evidence/m2/upload.log`](evidence/m2/upload.log) and
[`evidence/m2/size.txt`](evidence/m2/size.txt).

## Results at a glance

| Check | Result |
|---|---|
| Build | `pio run`: 0 warnings with `-Wall -Wextra`, GCC 14.2.1 |
| Upload | `pio run -t upload` via onboard ST-LINK V2-1: programmed and **Verified OK** |
| Clock | HSE 8 MHz → PLL → **SYSCLK 168 MHz**, PCLK1 42 MHz, PCLK2 84 MHz |
| FPU | Enabled (`CPACR = 0x00F00000`); ELF is hard-float (`Tag_ABI_VFP_args: VFP registers`) |
| Scheduler | FreeRTOS V11.3.2 started; 3 application tasks + IDLE + timer service running |
| Periodic timing (100 Hz probe) | 68 consecutive 1 s reports (6,800 wake-ups): period **9,999–10,000 µs**, 0 overruns. One 9,994 µs period at start-up only |
| CPU load | IDLE **98.68 %**; probe 0.05 %; monitor 1.26 % (blocking UART output) |
| Heap (heap_4, 48 KB) | 44,928 B free before scheduler start; **min-ever free 42,904 B** |
| Stacks (peak / allocation) | probe 152/1020 B · monitor 652/2044 B · heartbeat 104/508 B · IDLE 104/508 B · Tmr Svc 112/1020 B |
| Memory (build) | Flash 19,460 B of 1 MB (1.9 %); main SRAM 52,744 B of 128 KB (40.2 %, of which 49,152 B is the FreeRTOS heap); CCM RAM unused |

## 1. Toolchain and build configuration

| Item | Version / setting | Where |
|---|---|---|
| PlatformIO Core | 6.2.0 | — |
| Platform | `ststm32@20.0.0` | `platformio.ini` |
| Framework | `stm32cube` → STM32CubeF4 1.28.3 (HAL driver v1.8.5) | `platformio.ini` |
| Compiler | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1), **pinned** with `platform_packages` | `platformio.ini` |
| RTOS | FreeRTOS-Kernel **V11.3.2**: core + `portable/GCC/ARM_CM4F` + `MemMang/heap_4.c` | `lib/FreeRTOS/` |
| C library | newlib-nano; syscalls route stdout to the console | `src/system/syscalls.c` |
| Float ABI | `-mfpu=fpv4-sp-d16 -mfloat-abi=hard` for compile **and** link | `scripts/fpu.py` |
| Upload / debug | Onboard ST-LINK V2-1 (USB `0483:374B`), SWD, OpenOCD | `platformio.ini` |
| Console | USART1 PA9/PA10 at 115200 8N1 via the ST-LINK virtual COM port | `src/app/console.c` |

## 2. Board manifest (`boards/rt_spark.json`)

The handout warns that a generic F407 board entry must not be trusted. The custom manifest sets:

- `mcu = stm32f407zgt6`, `f_cpu = 168 MHz`, `cpu = cortex-m4`, `product_line = STM32F407xx`
- Flash **1 MB**, RAM **128 KB**: only the main SRAM. The 64 KB CCM RAM at `0x10000000` is listed separately by the linker (`CCMRAM` region) because **DMA cannot reach it**.
- Upload/debug protocol `stlink`, OpenOCD target `stm32f4x`, SVD `STM32F40x.svd` for register view.
- The crystal value is not in the manifest but in `include/stm32f4xx_hal_conf.h` (`HSE_VALUE = 8000000`).

## 3. Clock tree (`src/system/clock.c`)

| Signal | Configuration | Measured (boot log) |
|---|---|---|
| HSE | 8 MHz crystal | PLL locked from HSE |
| PLL | M = 8, N = 336, P = 2, Q = 7 → VCO 336 MHz | — |
| SYSCLK / HCLK | 168 MHz | 168,000,000 Hz |
| APB1 (PCLK1) / APB1 timers | /4 → 42 MHz / 84 MHz | 42,000,000 Hz |
| APB2 (PCLK2) / APB2 timers | /2 → 84 MHz / 168 MHz | 84,000,000 Hz |
| USB / SDIO | VCO / Q = 48 MHz | — |
| Flash latency | 5 wait states (RM0090, 168 MHz at 3.3 V) | — |

**Why this proves the crystal frequency:** USART1's baud-rate divider is computed from PCLK2, which the HAL derives from `HSE_VALUE`. If the real crystal differed from 8 MHz, 115200-baud output would be garbled, and the 100 Hz probe would drift against the debugger's wall clock. Both are correct.
**Fault handling:** if the HSE does not start, the code rebuilds the same 168 MHz PLL from the 16 MHz HSI and reports `HSE FAILED` at boot. This path has not been exercised on hardware, since the crystal can't be removed.

## 4. FreeRTOS integration

The handout's integration note asks for consistent tick, HAL timebase, interrupt priorities and heap/stack. Each point is handled as follows:

| Concern | Decision | Evidence |
|---|---|---|
| Kernel sources | Vendored subset of V11.3.2 in `lib/FreeRTOS` with `library.json`. No CubeMX middleware copy, so no duplicated kernel. | Build log: `FreeRTOS-Kernel @ 11.3.2` |
| Startup / vector table | PlatformIO's `startup_stm32f407xx.S` (one copy). The port's handlers are mapped onto the CMSIS names in `FreeRTOSConfig.h` (`vPortSVCHandler → SVC_Handler`, etc.). `configCHECK_HANDLER_INSTALLATION = 1` asserts this at scheduler start. | `nm`: exactly one `SVC_Handler`, `PendSV_Handler`, `SysTick_Handler` |
| FreeRTOS tick | SysTick, 1 kHz, owned by the kernel | `tick 1000 Hz` at boot |
| HAL timebase | **TIM6**, 1 kHz. `HAL_InitTick()` is overridden and recomputed after the PLL switch. The ISR clears the flag directly, so `HAL_TIM_PeriodElapsedCallback` stays free. | `src/system/hal_timebase_tim6.c` |
| Interrupt priorities | `NVIC_PRIORITYGROUP_4` (set by `HAL_Init`). Kernel at 15 (lowest). `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5`. **Rule:** any ISR that calls a `…FromISR()` API must use priority 5–15. Priorities 0–4 are never masked but must not call FreeRTOS. | `max syscall IRQ prio 5` at boot |
| Interrupt map so far | TIM5 stats overflow: 4 (no RTOS calls). TIM6 HAL tick: 15. SysTick/PendSV: 15 (kernel). | — |
| Heap | `heap_4`, 48 KB static array in **main SRAM** (DMA-capable). Tuned in M6/M7 from the measured min-ever free. | `heap: … min-ever free 42904 B` |
| Stacks | Per task, sized in words, verified by high-water marks. `configCHECK_FOR_STACK_OVERFLOW = 2`. | Stack table above |
| Safety nets | `configASSERT`, stack-overflow hook, malloc-failed hook, HardFault handler that prints PC/LR/CFSR/HFSR, then fast-blinks LED_R | `src/system/fault_handlers.c`, `rtos_hooks.c` |
| Run-time stats | TIM5 at **1 MHz**, extended to 64 bits with an overflow counter. Stats formatted by the app from `uxTaskGetSystemState()`, with no heap use. | CPU % to 0.01 % |

## 5. Bring-up task table

| Task | Function | Priority | Stack | Activation | Communication |
|---|---|---|---|---|---|
| `probe` | Stand-in for the 100 Hz SensorTask; measures its own wake-up period with the DWT cycle counter | 4 | 256 words | Every 10 ms (`xTaskDelayUntil`) | Sends a report to `probe_q` once per second, non-blocking |
| `monitor` | Prints probe reports, and every 10 s prints heap, stack and CPU statistics | 2 | 512 words | Blocks on `probe_q` (2 s timeout = stall detector) | Queue receive; console **mutex** |
| `heartbeat` | LED_R steady = scheduler running; LED_B toggles | 1 | 128 words | Every 500 ms (`xTaskDelayUntil`) | GPIO |
| `Tmr Svc` | FreeRTOS timer daemon (unused so far) | 6 | 256 words | On demand | Timer queue |

## 6. Problems found during bring-up and how they were fixed

| # | Problem | Impact | Fix |
|---|---|---|---|
| 1 | `ststm32@20.0.0` silently builds with **GCC 7.2.1 (2017)**. The M0 notes said 14.2.1 because the wrong toolchain folder was checked. | Wrong version recorded in evidence; old compiler | Pinned `toolchain-gccarmnoneeabi@1.140201.0`. The boot banner now prints the compiler version so the evidence can't drift again. |
| 2 | The `stm32cube` builder adds **no FPU flags**, so everything is soft-float | ARM_CM4F port will not compile; float maths slow | `scripts/fpu.py` adds hard-float flags to compile, assemble **and link** (plain `build_flags` do not reach the linker) |
| 3 | GCC 14 newlib warned that `_write/_read/_close/_lseek` "will always fail" | Library `printf` silently lost | `src/system/syscalls.c` sends stdout/stderr to the console |
| 4 | First boot reported "brown-out" | Misleading reset cause | Reset flags accumulate until cleared (a power-on sets POR+BOR+PIN). The code now checks the most specific flag first, prints raw `RCC_CSR`, and clears the flags each boot. |
| 5 | Kernel stats showed the 100 Hz task at **0 % CPU** | Wrong CPU-load evidence | A 10 kHz stats counter is phase-locked to the 1 kHz tick, so short tasks that run right after a tick never cross a counter edge. Moved to a 1 MHz counter extended to 64 bits. The kernel's own formatter prints 64-bit values with `%lu`, so the app formats the stats itself. |
| 6 | Linker warning "LOAD segment with RWX permissions" | Noise only | Comes from PlatformIO's generated linker script; silenced with `--no-warn-rwx-segments` |

## 7. Still to confirm by eye

- [ ] **LED active level.** With `LED_ON_LEVEL = active-low`, LED_R should be **steady on** and LED_B **blinking (1 s period)**. If red is off, flip `LED_ON_LEVEL` in `include/board.h`.

## 8. Reproduce

```sh
pio run -t upload
.venv/bin/python scripts/serial_capture.py --port <ST-LINK VCP port> --seconds 70 --out serial.log
# then press RESET (or: pio run -t upload again) to capture the boot banner
```

The first line of `evidence/m2/serial-boot.log` comes from the previous firmware still running just before the reset. The boot banner follows it.
