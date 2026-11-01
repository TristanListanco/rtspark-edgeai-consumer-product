# M3: Verified Hardware Inventory and Integrated Diagnostics

**Status:** done on the real board, 2026-11-01. Every component below is exercised by the **same FreeRTOS firmware** that will become the product.

| Log | Contents |
|---|---|
| [`evidence/m3/serial-m3-final.log`](evidence/m3/serial-m3-final.log) | Clean boot, then a scripted `diag`, `stats`, `live`, `i2cscan`, `ioprobe`, `calib` and `rtc` session |
| [`evidence/m3/serial-cli-diag.log`](evidence/m3/serial-cli-diag.log) | RTC set, flash write test, calibration, reboot that keeps the RTC time |
| [`evidence/m3/serial-ui-2.log`](evidence/m3/serial-ui-2.log) | Key presses and long presses on all 4 keys, LCD/buzzer/matrix self-test at boot |
| [`evidence/m3/serial-ui.log`](evidence/m3/serial-ui.log) | 13-minute run used for the self-heating measurement |
| [`evidence/m3/serial-sensors.log`](evidence/m3/serial-sensors.log) | First sensor bring-up and the I²C scans |

**Final self-test result:** 19 checks, **17 PASS, 0 FAIL, 2 ABSENT**. The two ABSENT items are the microSD (no card in the slot) and the EEPROM (not fitted).

---

## 1. Inventory

Role tags follow M1: **P** primary sensing, **C** context sensing, **U** user interaction, **L** logging/communication, **D** diagnostic/service.

| Component | Status | Interface / pins | How it was verified | Role in KeepSafe Box |
|---|---|---|---|---|
| STM32F407ZGT6 @ 168 MHz | ✅ | HSE 8 MHz → PLL | Boot report, `diag` CLOCK | — |
| **ICM-20608-G** 6-axis IMU | ✅ | I2C2 (hardware) PF1/PF0, addr 0x68 | WHO_AM_I = 0xAF; 100 samples/s, 0 errors | **P**: motion, impacts, lid tilt |
| **AP3216C** light / proximity / IR | ✅ | I2C2, addr 0x1E | Mode register read back; live lux / PS / IR | **P**: lid opening (light). **C**: proximity |
| **AHT20/AHT21** temp / humidity | ✅ | Bit-banged I2C PE0/PE1, addr 0x38 | Accepts AHT2x init 0xBE; CRC-8 passes | **C**: in-box storage conditions |
| ST7789V3 LCD 240×240 | ✅ | FSMC bank 1 NE3 (PG10), 8-bit, A18 = D/CX; RST PD3, backlight PF9 | RDDID = 81 81 B3; colour bars and text confirmed by eye | **U**: state, reason, values, diagnostics page |
| LED matrix, 19 × WS2812 | ✅ | PA7 TIM3_CH2 PWM + DMA1 Stream5, enable PF2 (low) | DMA frames complete; R/G/B and per-key colours confirmed by eye | **U**: status colour / pattern |
| Buzzer | ✅ | PB0 = TIM8_CH2N, 2.7 kHz | Beep confirmed by ear on boot and each key press | **U**: alerts |
| Keys ×4 | ✅ | PC5 UP, PC1 DOWN, PC0 LEFT, PC4 RIGHT; EXTI both edges | Idle high, so **active-low**; 253 presses + 4 long presses logged | **U**: acknowledge, arm/disarm code, screens, diagnostics |
| LED_R / LED_G | ✅ | PF12 / PF11, active-low | By eye (the BSP's "LED_B" is green) | **U**: running / heartbeat |
| RTC + 32.768 kHz LSE | ✅ | On-chip | LSE starts; time set over CLI; kept across reset | **L**: event timestamps |
| **W25Q64** SPI NOR, 8 MB | ✅ | SPI2 (PB13/PC2/PC3), CS PB12 | JEDEC EF 40 17; write test PASS on blank last sector | **L**: event log + settings (M6) |
| RW007 Wi-Fi | ✅ present | SPI2, CS PF10, INT/BUSY PG11, RST PG15 | Boots in 665–670 ms; replies with the protocol magic 98BADCFE 10325476 | **L** (optional, see §5). Held in reset |
| CAN transceiver | ✅ present | CAN1 PB9 TX / PB8 RX (AF9) | Both lines driven by an external chip; a one-shot frame returns **ACK error (LEC 3)**: the transceiver echoes, no other node | **D**: presence test only |
| RS485 transceiver | ✅ present | USART6, RX = **PG9**, DE/RE = PG8 | In receive mode PG9 is driven, PC7 floats | **D**: presence test only |
| ES8388 audio codec | ✅ present | I2C2, addr 0x10 | ACK in scan and in `diag` | **D**: presence test only (product uses the buzzer) |
| microSD slot | ⚪ no card | SDIO 4-bit + DMA2 | Init returns HAL error 0x4 (no response) and the product continues | **L**: dataset capture (M4), optional at run time |
| I²C EEPROM | ❌ not fitted | Its BSP pins PB8/PB9 are the CAN lines | No ACK; the CAN probe explains why | — |
| USB OTG FS | not used | PA11/PA12 | Connector present; no USB stack in the product | — (see §5) |
| IR transmitter/receiver | not found | — | Not in the BSP pin map or drivers | — (see §5) |
| Console | ✅ | USART1 PA9/PA10 via ST-LINK V2-1 VCP, 115200 8N1 | All logs | **D**: service CLI |

### Corrections to the handout and BSP, found on hardware

| Source said | Board actually has | Evidence |
|---|---|---|
| Handout: "AHT10" | **AHT20/AHT21** | Accepts the 0xBE init, and its 7-byte reply passes CRC-8 |
| BSP README: "W25Q128 (16 MB)" | **W25Q64 (8 MB)** | JEDEC capacity code 0x17 |
| BSP Kconfig: EEPROM on `swi2c1` (PB8/PB9) | **No EEPROM**: PB8/PB9 go to the **CAN transceiver** | Lines driven high; CAN ACK-error echo. An I²C scan there times out (110 recoveries) instead of NACKing |
| BSP README: "LED_B" | LED on PF11 is **green** | By eye |
| BSP: key polarity not documented | **Active-low** | Idle levels read high |
| BSP: RS485 UART6 pins not documented | **RX = PG9** | Line-drive probe |

## 2. Sampling intervals (measured)

| Signal | Target | Measured | Source |
|---|---|---|---|
| IMU read loop (`xTaskDelayUntil`, 10 ms) | 100 Hz | **100 samples/s; period 9,998–10,001 µs; 0 overruns; 0 I2C errors** | `live` lines |
| AP3216C | 10 Hz | 10 samples/s, 0 errors | `live` lines |
| AHT20/21 | every 5 s | Reading age cycles 0.9 → 4.9 s; 0 errors / CRC failures | `live` lines |
| CPU load with all of the above + UI at 4 Hz | — | IDLE **88 %**; sensor 4.4 %; ui 3.2 % | `stats` |

The sensor task's 4.4 % is mostly polled HAL I2C at 400 kHz. Interrupt/DMA I2C is a possible optimisation if CPU load matters later.

## 3. Calibration and offset notes

| Quantity | Measured at rest (`calib`, 200 samples, two runs) | Note |
|---|---|---|
| Accelerometer mean | [−56, +33, +993] mg; \|a\| = 995 mg | \|a\| is 0.5 % low; X/Y mostly reflect the bench tilt |
| Accelerometer noise (1 σ) | 1–2 mg | Far below the motion the classes produce |
| Gyroscope bias | X **+1.56 / +1.61 °/s**, Y +0.12–0.15, Z +0.39 °/s | Subtract a bias measured at rest, or use rate-of-change features. X bias is clearly non-zero |
| Gyroscope noise (1 σ) | 0.02–0.03 °/s | — |
| ALS range | 0–5162 lx, 0.0788 lx/count | Chosen for dark-box resolution; saturates in direct sun (still reads as "bright") |
| **AHT self-heating** | **30.0 → 37.0 °C and 76.0 → 57.5 %RH within ~10 min of power-on**; IMU die 36.4 → 42.8 °C | See below |

**Self-heating matters for the product.** The board's own heat (CPU, regulators, LCD backlight; in that run the Wi-Fi module was still running too) raises the air at the sensor by about 7 °C. That makes the measured RH read about 18 points **low**, which is exactly the wrong direction for a "too humid" warning. Planned mitigations, each to be measured before and after in M4/M6:
1. Hold the RW007 in reset. **Done** in this firmware, but not yet re-measured.
2. Sleep the CPU in the idle task (`WFI`) instead of spinning at 168 MHz for 88 % of the time.
3. Decide on **dew point / absolute humidity** plus a calibrated temperature offset against a reference thermometer, not on raw RH.

## 4. Fault handling seen during bring-up

| Situation | Behaviour | Evidence |
|---|---|---|
| No microSD card | Init fails with HAL error 0x4; `MICROSD ABSENT`; everything else runs | Boot log |
| I²C bus with no device / wrong wiring | Each failed transfer resets the I²C peripheral and clocks the bus free (110 recoveries); the scan finishes and the system continues | Boot log `i2c scan i2c1` |
| Sensor read error | Snapshot health = FAULT, error counter++. A stuck IMU (0.5 s of identical frames) → STUCK. Saturated frames counted | `sensors.c` |
| **HAL timeouts hanging before the scheduler starts** | Found on the board: FreeRTOS masks interrupts at or below priority 5 from the first API call until `vTaskStartScheduler()`, so the TIM6 HAL tick freezes and any `HAL_Delay` or timeout hangs. **Fix:** all hardware init now runs in an `init` task | PC halted in `I2C_WaitOnFlagUntilTimeout`; `src/app/app.c` |
| RW007 first SPI header too early | All-zero reply 200 ms after ready; correct magic on the next try, ~400 ms after ready. The probe now retries for up to 3 s | Boot logs |

## 5. Components for the instructor-verified exception list

| Component | Proposed status | Reason |
|---|---|---|
| I²C EEPROM | **Absent** | Not fitted. Its BSP pins are wired to the CAN transceiver |
| IR | **Absent?** | Not in the BSP. **Please confirm on the physical board** |
| microSD | Present, **no card in the kit** | Driver and diagnostics are ready. Insert a card to enable SD logging |
| Wi-Fi networking | Present, **presence/protocol check only** | The RW007 network protocol is RT-Thread-specific and not ported to HAL. The product decides offline by design; optional telemetry is a later improvement |
| CAN, RS485 | Present, **presence check only** | Transceivers verified electrically; no bus partner in the kit; not needed by the product |
| USB OTG | **Not used** | No USB function required by the product |
| ES8388 audio | Present, **presence check only** | Audible alerts use the buzzer |

## 6. Integrated diagnostic / service mode

- **Hold DOWN (≥ 1.5 s)** on the board, or type `diag`: runs the 19 live checks and shows a colour-coded PASS / WARN / FAIL / ABSENT page on the LCD, with full details on the console. Any key returns.
- **Serial CLI** (115200 8N1):

  | Command | Purpose |
  |---|---|
  | `help` | List commands |
  | `diag` | Run the self-test |
  | `live on`/`live off` | 1 Hz readings stamped with RTC time, plus RTOS stats every 30 s |
  | `stats` | Task / stack / heap / CPU table |
  | `i2cscan` | Scan the I²C buses |
  | `ioprobe` | Re-probe Wi-Fi, CAN and RS485 |
  | `rtc` / `rtc set YYYY-MM-DD HH:MM:SS` | Show or set the time |
  | `flash` / `flash test [force]` | Flash info / write test |
  | `sd` | Re-detect the microSD card |
  | `calib` | IMU at-rest offsets and noise |
  | `beep [ms]` | Sound the buzzer |
  | `reboot` | Restart the board |

- **Boot self-test** (visible): LCD red/green/blue/white, buzzer beep-beep, LED matrix R/G/B.

## 7. FreeRTOS tasks and interrupts (M3 firmware)

| Task | Prio | Stack (words) | Peak use | Activation | Communication |
|---|---|---|---|---|---|
| `init` | 5 | 1024 | — | Once at start, then deletes itself | Starts the other tasks |
| `sensor` | 4 | 512 | 372 B / 2 KB | Every 10 ms (`xTaskDelayUntil`) | I2C2 mutex; snapshot mutex |
| `keys` | 3 | 256 | 192 B / 1 KB | EXTI notification, 20 ms debounce, 50 ms long-press poll | Task notification in, `key_q` out |
| `env` | 2 | 384 | 220 B / 1.5 KB | Every 5 s | Soft-I2C bus mutex; snapshot mutex |
| `ui` | 2 | 1024 | 1012 B / 4 KB | `key_q` or every 250 ms | `key_q`, snapshot |
| `buzzer` | 2 | 160 | 152 B / 636 B | Pattern queue | `buzzer_q` |
| `monitor` | 1 | 512 | 120 B / 2 KB | Every 1 s | Snapshot; console mutex |
| `heartbeat` | 1 | 128 | 104 B / 508 B | Every 500 ms | GPIO |
| `cli` | 1 | 1024 | 956 B / 4 KB | Console RX stream buffer | Drivers' own mutexes |

Peak stack use is from `stats` in `serial-cli-diag.log`; every task keeps at least 75 % of its stack free. Heap min-ever free: 26.8 KB of 48 KB.

| IRQ | Priority | Calls FreeRTOS? |
|---|---|---|
| TIM5 (run-time stats overflow) | 4 | No (above the syscall limit, never masked) |
| EXTI0/1/4/9_5 (keys), USART1 RX, DMA1_S5 (WS2812), SDIO + DMA2_S3/S6 | 6 | Yes (`…FromISR`) |
| TIM6 (HAL tick), SysTick, PendSV | 15 | Kernel only |
