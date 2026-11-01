#!/usr/bin/env python3
"""Summarise data/manifest.csv against the lab's dataset requirements.

Windows are 2 s with a 1 s hop after trimming 1 s from each end of every trial
(data/PROTOCOL.md). The handout minimums count *independent* windows, so this
reports non-overlapping 2 s windows as well as the overlapping ones used in training.

Usage: .venv/bin/python ml/dataset_status.py [--manifest data/manifest.csv]
"""
import argparse
import csv
from collections import defaultdict

LABELS = ["IDLE", "CARRIED", "OPENED", "ROUGH"]
MIN_TRAIN, MIN_TEST = 30, 10          # handout minimums (independent windows per class)
TARGET_TRAIN, TARGET_TEST = 60, 20    # project targets (margin for exclusions)
TRIM_S, WIN_S, HOP_S = 1, 2, 1


def windows(trial_seconds: int, trials: int) -> tuple[int, int]:
    usable = max(0, trial_seconds - 2 * TRIM_S)
    independent = (usable // WIN_S) * trials
    overlapping = (max(0, (usable - WIN_S) // HOP_S + 1)) * trials
    return independent, overlapping


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--manifest", default="data/manifest.csv")
    args = ap.parse_args()
    try:
        rows = list(csv.DictReader(open(args.manifest)))
    except FileNotFoundError:
        print(f"{args.manifest} does not exist yet - record a session with ml/capture.py first")
        return 0

    agg = defaultdict(lambda: {"sessions": 0, "trials": 0, "ind": 0, "ovl": 0, "cond": set(), "people": set()})
    for r in rows:
        key = (r["label"], r["split"])
        ind, ovl = windows(int(r["trial_seconds"]), int(r["trials_kept"]))
        a = agg[key]
        a["sessions"] += 1
        a["trials"] += int(r["trials_kept"])
        a["ind"] += ind
        a["ovl"] += ovl
        a["cond"].add(f'{r["lighting"] or "?"}/{r["location"] or "?"}/{r["surface"] or "?"}')
        a["people"].add(r["participant"])

    print(f"{'class':8} {'split':5} {'sess':>4} {'trials':>6} {'indep.win':>9} {'overlap.win':>11}  status")
    for label in LABELS:
        for split, minimum, target in (("train", MIN_TRAIN, TARGET_TRAIN), ("test", MIN_TEST, TARGET_TEST)):
            a = agg.get((label, split), {"sessions": 0, "trials": 0, "ind": 0, "ovl": 0, "cond": set(),
                                         "people": set()})
            status = "TARGET MET" if a["ind"] >= target else ("min met" if a["ind"] >= minimum else "NEED MORE")
            if split == "train" and a["sessions"] < 2 and a["ind"] > 0:
                status += ", need >= 2 sessions"
            print(f"{label:8} {split:5} {a['sessions']:4} {a['trials']:6} {a['ind']:9} {a['ovl']:11}  {status}")
            if a["cond"]:
                print(f"{'':15} people {sorted(a['people'])}, conditions {sorted(a['cond'])}")
    gaps = sum(int(r["seq_gaps"]) + int(r["dropped"]) for r in rows)
    print(f"\n{len(rows)} sessions, lost/dropped IMU samples overall: {gaps}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
