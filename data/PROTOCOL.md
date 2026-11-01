# KeepSafe Box: Data Collection Protocol (M4)

Every training and test window must come from **the RT-Spark's own sensors**, recorded with this procedure. The class definitions are the ones in [`docs/01-concept.md`](../docs/01-concept.md) §4.

## 1. Signals, units, windows

| Item | Value |
|---|---|
| IMU (ICM-20608-G) | 100 Hz, raw int16. Accel ±8 g (4096 LSB/g), gyro ±1000 °/s (32.8 LSB per °/s) |
| Light / proximity (AP3216C) | 10 Hz, raw. ALS × 0.0788 = lux; PS and IR are 10-bit counts. Forward-filled onto IMU rows |
| Context (AHT20/21, RTC, firmware) | Recorded once per session in the header (`<session>.json`) |
| Trial length | IDLE 10 s, CARRIED 10 s, OPENED 8 s, ROUGH 6 s |
| Trimming | Drop the **first and last 1 s** of every trial (start/stop transitions) |
| Window | **2 s (200 samples), hop 1 s** within one trial. A window never spans two trials or two sessions |
| Train/test split | **By session, fixed at recording time** (`--split`). Never re-assigned after looking at results |

## 2. Setup (identical for every session)

1. **Board inside the lid**, components facing into the box, held by standoffs or foam and tape, so it cannot rattle. Write down the orientation once (e.g. "USB connector towards the hinge") and never change it.
2. **USB cable:** the trials stream over USB, so the cable stays connected. Use a **long, light cable**, route it out at the hinge side with a **slack loop**, and tape it to the box so it can't pull on the board. During CARRIED and ROUGH, a helper holds the laptop and walks alongside, or you carry the laptop in a bag; either way the cable must stay slack.
3. Flash the current firmware. Close any other serial monitor (VS Code), then find the port with `pio device list` (the "STM32 STLink" entry).
4. Put a few light objects in the box (pens, a small pouch). **Nothing fragile, no real medicine.**

## 3. Recording a session

```sh
.venv/bin/python ml/capture.py --port /dev/cu.usbmodem3103 \
    --label OPENED --split train --participant P01 \
    --location bedroom --lighting dark --surface desk --trials 5 --notes "lid opened ~100 deg"
```

For each trial, press **Enter**, get into position, then wait for the beeps:

`beep · beep · beep · BEEEP (GO)` → do the action → `beep-beep` (stop)

After each trial, the script prints the sample count and asks **keep / redo**. Redo any trial where the action was wrong or late, the cable pulled, or someone interrupted. Redone trials are counted in the manifest automatically.

Each session writes `data/sessions/<id>.csv/.json/.log` and appends a row to `data/manifest.csv`. Check progress at any time:

```sh
.venv/bin/python ml/dataset_status.py
```

## 4. What to do in each trial

| Class | Start position | From GO until the stop beep | Vary across trials |
|---|---|---|---|
| **IDLE** (10 s) | Box closed on a surface | **Do not touch the box.** Normal background only | 1 nothing · 2 typing on the same desk · 3 walking past · 4 putting a book or cup down next to the box · 5 a door closing in the room |
| **CARRIED** (10 s) | Box closed, in your hands just above the surface | Lift and **walk** (5–10 m) until the stop beep, then set it down **gently** | 1 two hands in front · 2 under one arm · 3 one hand by your side · 4 faster walk · 5 turning around / up a few stairs (carefully) |
| **OPENED** (8 s) | Box closed on a surface, hand on the lid | **Open the lid**, look inside, take out or put back items, rummage, until the stop beep. Then close it | 1 take an item · 2 put an item back · 3 rummage · 4 one hand only · 5 lid opened less wide. **Lighting varies by session** |
| **ROUGH** (6 s) | Box closed, in your hands or on a soft surface | **Keep doing** the rough action (about once a second) until the stop beep | 1 vigorous shaking · 2 knocking on the lid with a fist · 3 drop **≤ 20 cm onto a pillow/mattress**, pick up, repeat · 4 tip it over onto the bed and back · 5 bump it against a cushion. Use `--trials 8` for ROUGH (shorter trials) |

**Safety (from M1):** ROUGH happens only over a mattress or pillow, with drops of 20 cm or less, the board secured, and nothing fragile inside. The board is lab property.

## 5. Session plan (decide the split now, before any training)

Each class needs **at least 30 independent training windows and 10 independent test windows** from separate sessions. The plan below gives about 60 and 20 per class, leaving margin for exclusions. `dataset_status.py` counts them.

| Class | Session | Split | Participant | Conditions (location / lighting / surface) |
|---|---|---|---|---|
| all four | A | train | P01 (you) | room 1 / **bright** / desk |
| all four | B | train | P01 | room 2 / **dim** / bed or shelf |
| all four | C | train | P02 (volunteer) | room 1 or 3 / daylight / floor |
| OPENED only | C2 | train | P01 or P02 | any room / **dark** (lights off, phone torch off) / desk |
| all four | **D** | **test** | **P02 or P03** | **a different day**; a room or surface not used in A–C if possible; OPENED in **dim/dark** |

Per session use 5 trials (8 for ROUGH). With 1 s trimmed from each end of a trial, one session gives about:

| Class | Independent windows per session | Notes |
|---|---|---|
| IDLE | 20 | 4 per 10 s trial × 5 trials |
| CARRIED | 20 | 4 per 10 s trial × 5 trials |
| OPENED | 15 | 3 per 8 s trial × 5 trials, plus the extra dark session C2 |
| ROUGH | 16 | 2 per 6 s trial × 8 trials |

**Why this plan:** the M1 analysis says the model must handle exactly these cases:
- a **dark-room opening** (sessions C2 and D)
- a **firm put-down** (the end of CARRIED trials falls in the trimmed second, but IDLE trial 4 and CARRIED variations cover similar impacts)
- a **tilted carry** (CARRIED trial 2)
- **room activity** while the box is idle (IDLE trials 2–5)

**Participants:** use **anonymised IDs only** (P01, P02, …). Keep the name-to-ID list on paper, never in the repo. Get each volunteer's verbal consent and explain that only motion and light numbers are recorded: no audio, images or names.

## 6. Not part of the dataset

- **M7 system-test trials (20 per class)** are recorded **after the model is frozen**, with a different procedure. Do not record them now.
- **Bench and debug captures** use `--no-manifest --out-dir <scratch folder>` so they never enter `data/sessions/`.
- Anything removed during cleaning (M5) is listed in [`EXCLUSIONS.md`](EXCLUSIONS.md) with the reason.
