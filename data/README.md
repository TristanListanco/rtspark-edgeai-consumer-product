# data/

| Path | Contents |
|---|---|
| `PROTOCOL.md` | **How to record**: setup, per-class trial actions, session plan and split, safety |
| `manifest.csv` | One row per recording session (created by `ml/capture.py`): label, split, anonymised participant, conditions, trial and sample counts, data-integrity counters, firmware version |
| `sessions/<id>.csv` | One row per 100 Hz IMU sample: `session,label,trial,seq,t_ms,ax,ay,az,gx,gy,gz,als,ps,ir,light_flags` (raw integers; scales in the `.json`) |
| `sessions/<id>.json` | Session header from the board (scales, firmware, RTC time, in-box temperature/RH/lux at start) + per-trial statistics |
| `sessions/<id>.log` | Every line the board sent during the session (unaltered evidence) |
| `EXCLUSIONS.md` | Anything removed after recording, and why |

Participants are anonymised IDs (P01, P02, ...). Sessions contain only motion/light numbers:
no audio, images or names. Bench/debug captures are kept out of this folder (`--no-manifest`).

Check progress against the lab minimums: `.venv/bin/python ml/dataset_status.py`
