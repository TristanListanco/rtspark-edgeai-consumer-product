#!/usr/bin/env python3
"""Record one labeled KeepSafe Box session from the RT-Spark board over USB serial.

One run = one recording session of ONE class, made of several timed trials. For each
trial the board counts down with 3 short beeps; a long beep means GO; a double beep
means stop. The person handling the box never needs to look at the laptop.

Outputs (all under --out-dir, default data/sessions):
    <session>.log   every byte the board sent (untouched evidence)
    <session>.csv   one row per IMU sample, light values forward-filled
    <session>.json  session header: scales, firmware, conditions, per-trial stats
and one row appended to data/manifest.csv (unless --no-manifest).

Example:
    .venv/bin/python ml/capture.py --port /dev/cu.usbmodem3103 --label OPENED --split train \
        --participant P01 --location bedroom --lighting dark --surface desk --trials 5
"""
import argparse
import csv
import datetime as dt
import json
import os
import sys
import time

import serial

LABELS = {"IDLE": 10, "CARRIED": 10, "OPENED": 8, "ROUGH": 6}  # default trial seconds
MANIFEST_FIELDS = [
    "session", "label", "participant", "date", "start_time", "location", "lighting", "surface",
    "power", "trials_kept", "trials_redone", "trial_seconds", "imu_samples", "dropped", "seq_gaps",
    "imu_errors", "firmware", "notes", "split",
]


class Board:
    """Line-oriented access to the board; keeps every received line for the raw log."""

    def __init__(self, port: str):
        self.ser = serial.Serial(port, 115200, timeout=0.1)
        self.buf = b""
        self.raw: list[str] = []

    def send(self, cmd: str) -> None:
        for ch in (cmd + "\r").encode():
            self.ser.write(bytes([ch]))
            time.sleep(0.003)

    def lines(self, timeout: float):
        """Yield decoded lines until `timeout` seconds pass without the caller stopping."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.buf += self.ser.read(4096)
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                text = line.decode(errors="replace").rstrip("\r")
                self.raw.append(text)
                yield text

    def wait_for(self, prefix: str, timeout: float) -> str | None:
        for line in self.lines(timeout):
            if line.startswith(prefix):
                return line
        return None


def parse_trial(lines: list[str]) -> dict:
    """Turn one trial's D/L/E lines into rows and integrity statistics."""
    rows, light, prev_seq = [], None, None
    gaps = imu_errors = light_errors = 0
    for ln in lines:
        f = ln.split(",")
        try:
            if f[0] == "D" and len(f) == 9:
                seq, t = int(f[1]), int(f[2])
                if prev_seq is not None and seq != prev_seq + 1:
                    gaps += seq - prev_seq - 1
                prev_seq = seq
                rows.append([seq, t] + [int(x) for x in f[3:9]] + (light or ["", "", "", ""]))
            elif f[0] == "L" and len(f) == 6:
                light = [int(f[2]), int(f[3]), int(f[4]), int(f[5])]
            elif f[0] == "E" and len(f) == 3:
                if f[2] == "imu":
                    imu_errors += 1
                else:
                    light_errors += 1
        except ValueError:
            continue  # a console line cut a data line in half: ignore it, it shows as a gap
    return {"rows": rows, "seq_gaps": gaps, "imu_errors": imu_errors, "light_errors": light_errors}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", required=True)
    ap.add_argument("--label", required=True, choices=sorted(LABELS))
    ap.add_argument("--participant", required=True, help="anonymised ID, e.g. P01 (never a name)")
    ap.add_argument("--location", default="", help="e.g. bedroom, study-desk, hallway")
    ap.add_argument("--lighting", default="", choices=["", "bright", "dim", "dark", "daylight"])
    ap.add_argument("--surface", default="", help="e.g. desk, bed, floor, shelf")
    ap.add_argument("--power", default="usb", choices=["usb", "battery"])
    ap.add_argument("--trials", type=int, default=5)
    ap.add_argument("--seconds", type=int, default=0, help="trial length (default per label)")
    ap.add_argument("--auto", type=float, default=0.0,
                    help="no prompts: wait this many seconds between trials (bench tests)")
    ap.add_argument("--split", required=True, choices=["train", "test"],
                    help="decided BEFORE training from data/PROTOCOL.md (session plan), never after")
    ap.add_argument("--notes", default="")
    ap.add_argument("--out-dir", default="data/sessions")
    ap.add_argument("--manifest", default="data/manifest.csv")
    ap.add_argument("--no-manifest", action="store_true", help="bench test: do not add to the dataset")
    args = ap.parse_args()

    if not args.participant.startswith("P") or not args.participant[1:].isdigit():
        sys.exit("participant must be an anonymised ID like P01")
    secs = args.seconds or LABELS[args.label]
    now = dt.datetime.now()
    session = f"{now:%Y%m%d-%H%M%S}-{args.label}-{args.participant}"
    os.makedirs(args.out_dir, exist_ok=True)

    board = Board(args.port)
    board.send("live off")
    time.sleep(0.3)
    board.send(f"cap start {args.label} {session}")
    header = board.wait_for("H,1,", 5)
    if header is None:
        sys.exit("board did not answer 'cap start' - is the KeepSafe firmware running on that port?")
    meta = dict(kv.split("=", 1) for kv in header.split(",")[4:] if "=" in kv)
    print(f"session {session}: {args.label}, {args.trials} trials x {secs} s, firmware {meta.get('fw')}")

    kept, redone, trial_stats, all_rows = 0, 0, [], []
    trial_no = 0
    while kept < args.trials:
        trial_no += 1
        if args.auto:
            time.sleep(args.auto)
        else:
            input(f"\ntrial {kept + 1}/{args.trials}: get into position, then press Enter "
                  f"(3 beeps, GO on the long beep, stop on the double beep)... ")
        board.send(f"cap trial {trial_no} {secs}")
        lines, end = [], None
        for ln in board.lines(secs + 15):
            if ln.startswith(("D,", "L,", "E,")):
                lines.append(ln)
            elif ln.startswith(f"T,{trial_no},end,"):
                end = ln
                break
        if end is None:
            print("  ! no end marker from the board - trial discarded")
            redone += 1
            continue
        _, _, _, _, n_lines, dropped = end.split(",")
        t = parse_trial(lines)
        expected = secs * 100
        print(f"  trial {trial_no}: {len(t['rows'])} IMU samples (expected ~{expected}), "
              f"board dropped {dropped}, seq gaps {t['seq_gaps']}, imu errors {t['imu_errors']}")
        keep = True
        if not args.auto:
            answer = input("  keep this trial? [Enter = keep / r = redo] ").strip().lower()
            keep = answer != "r"
        if not keep:
            redone += 1
            print("  -> redo (logged in the manifest)")
            continue
        kept += 1
        trial_stats.append({"trial": trial_no, "samples": len(t["rows"]), "dropped": int(dropped),
                            "seq_gaps": t["seq_gaps"], "imu_errors": t["imu_errors"],
                            "light_errors": t["light_errors"]})
        all_rows += [[session, args.label, trial_no] + r for r in t["rows"]]

    board.send("cap stop")
    board.wait_for("C,stop,", 3)

    base = os.path.join(args.out_dir, session)
    with open(base + ".log", "w") as f:
        f.write("\n".join(board.raw) + "\n")
    with open(base + ".csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["session", "label", "trial", "seq", "t_ms", "ax", "ay", "az", "gx", "gy", "gz",
                    "als", "ps", "ir", "light_flags"])
        w.writerows(all_rows)
    info = {
        "session": session, "label": args.label, "participant": args.participant,
        "started": now.isoformat(timespec="seconds"), "location": args.location,
        "lighting": args.lighting, "surface": args.surface, "power": args.power,
        "trial_seconds": secs, "split": args.split, "trials": trial_stats, "trials_redone": redone,
        "board_header": meta, "notes": args.notes,
    }
    with open(base + ".json", "w") as f:
        json.dump(info, f, indent=2)

    totals = {k: sum(t[k] for t in trial_stats) for k in ("samples", "dropped", "seq_gaps", "imu_errors")}
    if not args.no_manifest:
        new = not os.path.exists(args.manifest)
        with open(args.manifest, "a", newline="") as f:
            w = csv.DictWriter(f, fieldnames=MANIFEST_FIELDS)
            if new:
                w.writeheader()
            w.writerow({
                "session": session, "label": args.label, "participant": args.participant,
                "date": f"{now:%Y-%m-%d}", "start_time": f"{now:%H:%M:%S}", "location": args.location,
                "lighting": args.lighting, "surface": args.surface, "power": args.power,
                "trials_kept": kept, "trials_redone": redone, "trial_seconds": secs,
                "imu_samples": totals["samples"], "dropped": totals["dropped"], "seq_gaps": totals["seq_gaps"],
                "imu_errors": totals["imu_errors"], "firmware": meta.get("fw", ""), "notes": args.notes,
                "split": args.split,
            })
    print(f"\nsaved {base}.csv ({totals['samples']} samples, {kept} trials, {redone} redone)"
          f"{'' if args.no_manifest else ' and added it to ' + args.manifest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
