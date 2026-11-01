# Lab 4 Plan: Edge AI Consumer Product on RT-Spark

Source: *BCA182 Laboratory Activity No. 4 – Design and Development of an Edge AI Consumer Product* (100 pts).
**Solo submission.** The deadline comes from the instructor. The schedule below assumes about **5 weeks**, so stretch or compress it to fit.

---

## Hard rules from the handout

These apply to every milestone.

- **Build/upload:** PlatformIO with `platform = ststm32` and `framework = stm32cube`, plus a **custom RT-Spark board manifest**. A generic F407 entry is not trusted for pins, clocks, or memory layout.
- **Software stack:** STM32Cube HAL/LL for peripherals and FreeRTOS for scheduling. FreeRTOS is **not** included automatically, so you add it yourself.
- **Inference runs on the STM32F407.** With no network, prediction and the user response must keep working. A threshold rule can only be a baseline or fallback.
- **One firmware:** every sensor/module that is present must be reachable in it, through the product function or a diagnostic/service mode.
- **Data source:** all data comes from the board's own sensors. A phone may only be used to annotate.
- **Repository:** public GitHub repo with a real commit history and a reproducible build (not just a `.bin`). Never commit secrets or personal data.
- **Honest numbers:** every number in the report must trace back to a saved log. Fabricated figures are an academic-integrity issue.

---

## Timeline at a glance

| #  | Milestone                                        | When     | Rubric weight it feeds     | Gate to pass before moving on |
|----|--------------------------------------------------|----------|----------------------------|-------------------------------|
| M0 | Repo, toolchain, references                      | W1 (d1–2) | Repo/report (5)           | Empty project builds; repo public |
| M1 | Product concept & requirements                   | W1       | Concept & design (10)      | Classes are reproducible ≥60× each |
| M2 | Bring-up: PlatformIO + HAL + FreeRTOS skeleton   | W1–W2    | Architecture (15)          | Scheduler runs ≥2 tasks, logs over serial |
| M3 | All drivers + integrated diagnostic mode         | W2       | Board audit (15)           | Every present component exercised live |
| M4 | Capture firmware + labeled dataset               | W2–W3    | Data & model (15)          | Dataset frozen, session split fixed |
| M5 | Features, baseline, model, footprint check       | W3       | Data & model (15)          | Model beats baseline and fits on the MCU |
| M6 | Integrated product firmware + on-device inference | W3–W4    | On-device inference (20), Architecture (15) | Works end-to-end offline |
| M7 | System testing & evaluation                      | W4–W5    | Testing & evaluation (15)  | All required numbers measured and logged |
| M8 | Report, repo polish, demo video, Q&A prep        | W5       | Report/repo (5), Demo (5)  | Clean-clone build passes; video recorded |

The handout requires this order. Confirm sensors and data are good **before** training, and confirm the model fits on the MCU **before** finishing the UI. M5's footprint spike covers the second check.

---

## Suggested product concept (replace if you have a better one)

The handout requires the problem paragraph, **at least two alternatives**, and a justification. Use this shortlist as a starting point:

| Candidate | Classes | Pros | Cons |
|---|---|---|---|
| **A. Smart keepsafe/storage box (chosen: KeepSafe Box, see `docs/01-concept.md`):** monitors a box holding medicine, documents, or valuables | `IDLE`, `CARRIED`, `OPENED/HANDLED`, `ROUGH` (shake/knock) | IMU and light/proximity both feed the classifier. All classes are safe and quick to reproduce many times. AHT10 adds a "storage humidity" warning. | You must make sure it is not just a copy of a generic "item tracker" |
| B. Laundry/appliance vibration monitor | `IDLE`, `WASH`, `SPIN`, `UNBALANCED` | Good fit for spectral (FFT) features | Needs real machine cycles, so collecting ≥60 instances per class is slow |
| C. Context-aware study desk | `AWAY`, `PRESENT-IDLE`, `WORKING`, `BUMP` | Proximity and light map naturally to presence | The IMU signal on a desk is weak and classes blur together |

**Selection rule:** each class needs about **60+ independent instances**: 30 for training, 10 held out for testing, and 20 fresh system-test trials. Pick classes you can reproduce safely that many times.

---

## M0: Repo, toolchain, references (Week 1, days 1–2) ✅ done 2026-11-01

**Do**
- [x] Create a **public** GitHub repo with README skeleton, LICENSE, and a `.gitignore` that covers `.pio/`, Python venvs, and any raw data containing personal info.
- [x] Install VS Code, PlatformIO, and the `ststm32` platform (`ststm32@20.0.0`, STM32CubeF4 1.28.3). *Correction found in M2: the M0 build actually used the platform's default GCC 7.2.1; GCC 14.2.1 is pinned since M2.* The custom `boards/rt_spark.json` builds a HAL blink smoke test.
- [ ] *(Optional)* Install STM32CubeMX, only to generate or review init code.
- [x] Set up the Python env (Python 3.12 `.venv`, pinned in `ml/requirements.txt`). The sklearn → emlearn → Cortex-M4 C export is smoke-tested.
- [x] List references in `docs/references.md`. RT-Thread BSP facts are captured in `docs/hardware-notes.md` (unverified).
- [ ] **Still to do:** get the RT-Spark **schematic** from the instructor/lab kit and download the sensor datasheet PDFs (keep them local, don't commit).
- [x] Set up the repo layout:

```
platformio.ini
boards/rt_spark.json           # custom board manifest
include/                       # FreeRTOSConfig.h, stm32f4xx_hal_conf.h, app headers
lib/FreeRTOS/                  # kernel + portable/GCC/ARM_CM4F + MemMang/heap_4.c
lib/<drivers>/                 # icm20608, aht10, ap3216c, lcd, w25qxx, ...
src/                           # main.c, tasks/, app/, model/
ml/                            # capture.py, features.py, train.py, export, notebooks
data/                          # manifest.csv, EXCLUSIONS.md, anonymized samples
docs/                          # report, diagrams, test plan, evidence/ (logs, photos)
```

**Done when:** the repo is public, the first commit is pushed, and the project opens and builds in PlatformIO.

---

## M1: Product concept & requirements (Week 1) · 10 pts ✅ done 2026-11-01: KeepSafe Box

**Do**
- [x] Write a one-paragraph problem statement covering target user, problem, operating environment, and product goal.
- [x] Describe at least 2 alternative ideas and why you chose the final one (use the table above).
- [x] Define ≥3 classes with **operational definitions**: what physically happens, how to reproduce it, and how long it lasts. For anomaly detection instead, define *nominal* plus two safely simulated abnormal scenarios, and get instructor approval for anything hazardous.
- [x] Draw the block diagram: Sense → Process (filter/window/features) → Infer locally → Act → Record/transmit.
- [x] Draft the component-to-function matrix (preliminary). Tag every component as *primary sensing / context sensing / user interaction / logging-communication / diagnostic-service*. Finalize it after M3.
- [x] Write acceptance criteria:
  - expected behavior per class
  - which errors matter most (e.g., a missed `ROUGH` is worse than a false one)
  - measurement plan
  - privacy, accessibility, and safety risks
- [x] Check the primary function uses **≥2 distinct sensing modalities**, **≥1 user input**, and **≥1 local output**.

**Evidence:** `docs/01-concept.md` and `docs/diagrams/block-diagram.(png|svg)`. Commit and tag `m1-concept`.

---

## M2: Bring-up with PlatformIO + HAL + FreeRTOS (Weeks 1–2) ✅ done 2026-11-01: see `docs/02-bringup-freertos.md`

**Do**
- [ ] *(Moved to M3, needs you at the board)* **Physical inventory:** record board revision, photos, fitted modules, and which connectors are empty. A connector is not a sensor.
- [x] **Board manifest** `boards/rt_spark.json`: start from a generic F407 manifest, then set the MCU (`stm32f407zgt6`, 168 MHz) and flash (1 MB). Set RAM to the **128 KB main SRAM** and treat the 64 KB **CCM RAM** separately, because DMA cannot reach it. Also set the linker script and upload/debug protocol.
- [x] **Clocks:** `HSE_VALUE` = 8 MHz (BSP, confirmed by a working PLL and UART baud), PLL at 168 MHz. *LSE for the RTC moves to M3 with the RTC driver.*
- [x] **Upload/debug:** check which probe your board actually has (onboard or external ST-LINK via SWD), then set `upload_protocol` and `debug_tool` to match.

  ```ini
  [env:rt_spark]
  platform = ststm32
  framework = stm32cube
  board = rt_spark
  upload_protocol = stlink        ; verify against your probe
  debug_tool = stlink
  monitor_speed = 115200
  board_build.stm32cube.custom_config_header = yes
  ```
- [x] **FreeRTOS integration.** This is the most common place to get stuck.
  - Add a pinned FreeRTOS-Kernel version: core, `portable/GCC/ARM_CM4F`, and `heap_4.c`.
  - Map `vPortSVCHandler` → `SVC_Handler` and `xPortPendSVHandler` → `PendSV_Handler`, and give SysTick to FreeRTOS. Avoid duplicate handlers.
  - Move the **HAL timebase to TIM6/TIM7** (`stm32f4xx_hal_timebase_tim.c`) so HAL and FreeRTOS don't fight over SysTick.
  - Set `NVIC_PRIORITYGROUP_4` and a consistent `configMAX_SYSCALL_INTERRUPT_PRIORITY`. Any ISR that calls `...FromISR` must have a numerically higher (lower urgency) priority than that value.
  - Enable the FPU with hard-float flags that are consistent at compile and link time.
  - Turn on the safety nets: `configASSERT`, `configCHECK_FOR_STACK_OVERFLOW = 2`, `configUSE_MALLOC_FAILED_HOOK = 1`, run-time stats, and trace facility.
- [x] **Console:** UART with `printf` retarget, protected by a mutex so lines from different tasks don't interleave. Timestamp every line.
- [x] **Heartbeat demo:** run 2–3 tasks (LED blink, tick logger) to prove the scheduler starts.

**Evidence:** `platformio.ini`, `pio run` build log, upload log, and the first serial log with scheduler startup. Save them in `docs/evidence/m2/`.
**Done when:** a fresh clone builds and uploads with one command and the serial log shows multiple tasks running. Tag `m2-bringup`.

---

## M3: Drivers + integrated diagnostic mode (Week 2) · 15 pts ✅ done 2026-11-01: see `docs/02-hardware-inventory.md`

**Do**
- [x] Write or port HAL drivers. Each one returns a status code. If init fails, mark the component `FAULT` and let the system keep running.

  | Component | Check |
  |---|---|
  | ICM20608 IMU | WHO_AM_I, accel/gyro in g and °/s, measured bias at rest |
  | AHT10 | °C/%RH, busy/calibration bit. Note board **self-heating** offset |
  | AP3216C | ambient light (lux), proximity counts, IR overflow flags |
  | LCD | init, text/graphics, refresh rate |
  | LEDs / LED matrix | patterns. DMA buffers must be in main SRAM, not CCM |
  | Buzzer | tone/PWM test |
  | Buttons | EXTI + debounce, short/long press |
  | RTC | LSE running, set/read time, survives reset |
  | SPI flash | JEDEC ID, erase/write/read-back test |
  | microSD/TF | detect, mount (FatFs added as middleware), write/read test, "card absent" handling |
  | Wi-Fi module | presence/status at minimum (see risk register) |
  | IR, CAN, RS485, USB, expansion | only if installed or equipped, otherwise document as absent/not equipped |

- [x] If sensors share an I²C bus, guard it with a **bus mutex**.
- [x] **Diagnostic mode,** entered by button long-press or serial command:
  - live timestamped readings with units on LCD and serial
  - self-tests for LED, buzzer, buttons, flash, SD, RTC, and Wi-Fi
  - a per-component PASS/FAIL/ABSENT summary
- [x] Measure **actual sampling intervals and jitter** against target rates. Record calibration offsets and init-failure behavior.
- [ ] Get **instructor-verified exceptions** in writing for any absent or inoperative component. *(List prepared in `02-hardware-inventory.md` §5. **Still to do:** get the instructor to sign it off, and photograph both sides of the board.)*

**Evidence:** `docs/02-hardware-inventory.md` with the inventory table (bus, address, pins, status), the final component-to-function matrix, serial logs, photos, and short video clips of each self-test.
**Done when:** every present component is exercised live from the *same* FreeRTOS firmware. Tag `m3-diagnostics`.

---

## M4: Capture firmware + labeled dataset (Weeks 2–3)

**Do**
- [ ] Write the **capture protocol** (`data/PROTOCOL.md`):
  - sampling rates (e.g., IMU 100 Hz, light/proximity 10 Hz, AHT10 1 Hz)
  - window length and hop (e.g., 2 s / 1 s)
  - units
  - label definitions
  - variation plan: different people, surfaces, orientations, rooms, times of day
- [ ] Add a **capture mode** to the firmware. A button or serial command starts and stops a labeled recording, which streams CSV over UART and/or logs to SD, with session ID, label, timestamp, and sequence counter. A host script `ml/capture.py` saves one file per session.
- [ ] **Watch the USB cable.** A taut cable distorts IMU data. Use a power bank with SD logging, or a long slack cable, and keep that setup the same for data capture and real use.
- [ ] **Volume:** per class, ≥30 training windows and ≥10 held-out test windows, each from **separate recording sessions**. Aim for about 50 + 15 so exclusions don't drop you below the minimum.
- [ ] Do **not** record the M7 system-test trials yet. They must come after the model is frozen.
- [ ] Clean the data: drop invalid reads (error flags, saturation, stuck values) and record every exclusion in `data/EXCLUSIONS.md`.
- [ ] Write `data/manifest.csv` with columns: file, session, label, anonymized participant ID, date, location, rates, split, notes.

**Done when:** the dataset is frozen and the **session-level train/test split** is decided and committed *before* any training. Tag `m4-dataset`.

---

## M5: Features, baseline, model, footprint check (Week 3) · 15 pts

**Do**
- [ ] **Features/DSP:**
  - per-axis and magnitude statistics: mean, std, min/max, RMS, peak-to-peak, mean-crossings
  - FFT band energies, only if the signal is periodic
  - light/proximity statistics (mean, delta, max)
  - Write **one feature spec** that the Python code and the C code both implement exactly.
- [ ] **Baseline:** a threshold/rule classifier (e.g., accel-magnitude std plus a light threshold), evaluated on the same held-out sessions.
- [ ] **Model:** a small MLP or a decision tree / random forest in scikit-learn, exported to C with `emlearn`. Edge Impulse with the EON compiler is an alternative. Tune with **GroupKFold by session**, and only touch the held-out sessions for the final evaluation.
- [ ] **Metrics:**
  - confusion matrix
  - per-class precision, recall, and F1, plus macro-F1
  - baseline vs. model table
  - an explanation of the misclassifications and any class imbalance
- [ ] **Footprint spike:** drop the exported model into the M3 firmware right away. Measure the Flash/RAM it adds (from the linker map file) and time one inference. This is the "verify it fits before finishing the UI" check.
- [ ] Record versions: Python, scikit-learn, exporter, `arm-none-eabi-gcc`, FreeRTOS kernel, STM32CubeF4.

**Evidence:** `ml/train.py` (or a notebook) with a fixed seed, the training config, exported model files, a metrics report, and plots. Store them in `docs/evidence/m5/`.
**Done when:** the model beats the baseline on held-out sessions (or you can explain why it doesn't) and fits with a comfortable memory margin. Tag `m5-model`.

---

## M6: Integrated product firmware with on-device inference (Weeks 3–4) · 20 + 15 pts

**Do**
- [ ] Implement the task architecture (draft below). Use `vTaskDelayUntil` or timer-triggered sampling, never busy-waits. Pass data between tasks with queues, task notifications, mutexes, and event groups.
- [ ] **Sensor validation:** apply range checks, stuck-value detection, and I²C error checks. Reject an invalid window and show `SENSOR FAULT` instead of a prediction.
- [ ] **Inference pipeline:** window → features (same spec as Python) → model → post-processing (e.g., confidence threshold or 2-of-3 vote) → decision logic.
- [ ] **User feedback:**
  - LCD shows the state, the confidence, the key sensed values, and **why** the action was taken
  - LED color and buzzer patterns for alerts
  - buttons for acknowledge/mute, calibrate, and diagnostics
- [ ] **Logging:** write RTC-timestamped events to SPI flash or SD. If the SD card is missing, the product keeps running.
- [ ] **Optional comms task:** Wi-Fi telemetry that never blocks, uses timeouts and reconnect, and falls back to local-only operation.
- [ ] **Golden-vector check:** add a replay mode that sends held-out windows over UART. Compare the MCU's predictions with Python's and report the agreement percentage. This proves the MCU runs the exported model correctly.
- [ ] **Instrumentation:** a serial `stats` command prints per-task stack high-water marks, current and min-ever free heap, run-time stats, and inference timing (DWT cycle counter). Add stack-overflow and malloc-failed hooks. A watchdog is optional.

**Done when:** on battery with no network, the board senses, infers, and responds, and diagnostics still reach every component. Tag `m6-integrated`.

### Draft FreeRTOS task table (replace numbers with measured values)

| Task | Function | Priority | Stack (words) | Period / trigger | Communication |
|---|---|---|---|---|---|
| `SensorTask` | Sample IMU and light/proximity, validate, timestamp, fill window | 4 | 512 | 10 ms (`vTaskDelayUntil`) | I²C mutex → `windowQueue` |
| `EnvTask` | AHT10 temperature/humidity, storage-condition check | 2 | 256 | 1 s | Mutex-protected env struct |
| `InferenceTask` | Features, model, decision | 3 | 1024–2048 | Each window (~1 s) | `windowQueue` → `resultQueue`, `logQueue` |
| `UiTask` | LCD, LED, buzzer, modes, button handling | 2 | 768 | 50 ms and events | `resultQueue`, button event group (from EXTI) |
| `LoggerTask` | RTC-stamped events to flash/SD | 1 | 1024 | On event | `logQueue`, SPI/SDIO mutex |
| `ConsoleTask` | Serial CLI: `diag`, `stats`, `capture`, `replay` | 1 | 512 | On RX | UART stream buffer |
| `CommsTask` (opt.) | Wi-Fi telemetry, reconnect | 1 | 1024 | 10 s / event | `telemetryQueue`, timeouts |

---

## M7: System testing & evaluation (Weeks 4–5) · 15 pts

Write `docs/05-test-plan.md` **before** running any test. For each test, list ID, condition, procedure, expected result, and pass criterion. Save every raw log to `docs/evidence/m7/` and compute statistics with a script (`ml/analyze_tests.py`).

| ID | Test | Requirement |
|---|---|---|
| T1 | Product trials | **≥20 new independent trials per class** that were never used for training or tuning. Report a confusion matrix and expected-vs-observed response. |
| T2 | Inference latency | **≥50 on-board inferences.** Report median and **p95** time from the DWT cycle counter, plus sampling-to-response delay (last sample timestamp to actuator change). |
| T3 | Memory | Firmware Flash/RAM, model footprint (from the map file), FreeRTOS heap (min-ever free), per-task stack high-water marks, and any overflow risks |
| T4 | Soak | **≥15 min continuous run.** Count crashes, hangs, missed sensor reads (gaps in sequence counters), unresponsive controls, and false alerts. |
| T5 | Offline | **≥5 consecutive minutes** with the network disabled. Inference and feedback must continue. |
| T6 | Controlled failure | At least one of: network loss, injected invalid sensor read (debug command), SD card removed, or restart. The scheduler and UI must stay responsive. |
| T7 | Privacy, safety, accessibility | Assess at least one of each, e.g. no raw personal data leaves the device; no hazardous drop tests; both visual and audible cues with high-contrast LCD text. Label the product an *experimental prototype*. |

**Done when:** every number has a log file behind it, and you have **≥2 improvements backed by test results**. Tag `m7-evaluation`.

---

## M8: Report, repo, demo (Week 5) · 5 + 5 pts

**Do**
- [ ] **Lab report sections:**
  - problem and design
  - verified hardware inventory
  - architecture/block diagram
  - FreeRTOS task table
  - dataset collection
  - model and baseline
  - test results
  - limitations
  - technical references
- [ ] **README contents:**
  - overview
  - hardware and exceptions
  - build/flash steps
  - how to enter diagnostics and capture mode
  - how to reproduce training
  - troubleshooting notes
  - license and attribution for FreeRTOS, ST HAL, and any ported drivers
- [ ] **Data and model files:** data manifest, anonymized sample data or the capture procedure, model artifacts, and training config.
- [ ] **Demo video** showing:
  - boot and scheduler log
  - diagnostics for every verified component
  - each product class in action
  - **operation with the network disabled**
  - one failure case
- [ ] **Clean-clone test:** clone the repo into a fresh folder, then `pio run -t upload`.
- [ ] **Q&A rehearsal:** be ready to explain the firmware, model, hardware roles, and test results.
- [ ] Tag `v1.0-submission`.

---

## Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| HAL tick and FreeRTOS SysTick conflict, duplicate handlers, or wrong IRQ priorities | Hard faults, firmware never starts | HAL timebase on TIM6/7, handler mapping, priority group 4, `configASSERT`. Do this in M2 before writing any drivers. |
| No HAL driver for the onboard Wi-Fi module (often an SPI RW007 whose driver is RT-Thread-specific; confirm yours) | Lose Wi-Fi integration time | Timebox to about 1 day. Fall back to presence/status in diagnostics, and get an instructor exception if it can't be driven. Wi-Fi is optional for the product decision. |
| DMA buffers placed in CCM RAM | Silent DMA failures (LCD, LED matrix, SD) | Use section attributes to keep DMA buffers in main SRAM. CCM is fine for stacks and model buffers. |
| Feature mismatch between Python and C | Good offline accuracy, wrong on-device predictions | One feature spec and the golden-vector replay test in M6 |
| Train/test leakage | Inflated accuracy, lost points | Split by session, keep held-out sessions untouched, and record system-test trials only after the model is frozen |
| USB cable constraining motion | Data unlike real use | Power bank with SD logging, used the same way for capture and use |
| Not enough time to collect about 60 instances per class | Missing required evidence | Script the capture, start M4 early, and ask a few volunteers to join sessions so the data covers more than one participant |
| AHT10 self-heating from the board | Temperature reads high | Calibrate against a reference thermometer and use AHT10 for context only |
| Missing middleware (FatFs, CMSIS-DSP) | Build failures late in the project | Add it during M3/M5, not during M6 |

---

## Final submission checklist (quantities from the handout)

- [ ] ≥3 classes (or anomaly detection with ≥20 nominal and ≥20 abnormal test windows across 2 scenarios)
- [ ] ≥30 training and ≥10 held-out test windows per class from separate sessions
- [ ] Baseline vs. trained model comparison, confusion matrix, per-class precision/recall
- [ ] ≥3 independently scheduled FreeRTOS tasks with queues/semaphores/mutexes/event groups, plus the task table
- [ ] Primary function uses ≥2 sensing modalities, ≥1 input, ≥1 local output
- [ ] Every present component reachable in the product or diagnostic mode, with exceptions documented
- [ ] ≥20 independent system trials per class
- [ ] ≥50 inference timings with median and p95, plus sampling-to-response delay
- [ ] Flash, RAM, heap, and stack high-water-mark report, plus model footprint
- [ ] ≥15 min soak test, ≥5 min offline run, and 1 controlled failure case
- [ ] At least one privacy, one safety, and one accessibility assessment; "experimental prototype" wording
- [ ] ≥2 improvements backed by tests
- [ ] Repo contents: source, `platformio.ini`, board manifest, FreeRTOSConfig, drivers, build/flash steps, license/attribution, troubleshooting notes, data manifest, model artifacts
- [ ] Demo video, including network-disabled operation
