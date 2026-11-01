#!/usr/bin/env python3
"""Capture the RT-Spark serial console to a file (and echo it) for a fixed duration.

Optionally types CLI commands into the board during the capture, so evidence runs
are scripted and repeatable.

Usage:
    .venv/bin/python scripts/serial_capture.py --port /dev/cu.usbmodem3103 \
        --seconds 60 --out docs/evidence/m2/serial-boot.log
    .venv/bin/python scripts/serial_capture.py --port /dev/cu.usbmodem3103 --seconds 40 \
        --out diag.log --send "diag" --send "flash test" --send-delay 8 --send-gap 4
"""
import argparse
import sys
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", required=True, help="serial device, e.g. /dev/cu.usbmodem3103 or COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--out", required=True, help="output log file")
    ap.add_argument("--send", action="append", default=[], help="CLI command to type (repeatable, in order)")
    ap.add_argument("--send-delay", type=float, default=6.0, help="seconds before the first command")
    ap.add_argument("--send-gap", type=float, default=3.0, help="seconds between commands")
    args = ap.parse_args()

    start = time.monotonic()
    deadline = start + args.seconds
    schedule = [(start + args.send_delay + i * args.send_gap, cmd) for i, cmd in enumerate(args.send)]

    with serial.Serial(args.port, args.baud, timeout=0.2) as port, open(args.out, "wb") as out:
        port.reset_input_buffer()
        while time.monotonic() < deadline:
            if schedule and time.monotonic() >= schedule[0][0]:
                _, cmd = schedule.pop(0)
                for ch in (cmd + "\r").encode():
                    port.write(bytes([ch]))  # byte by byte: the board echoes as it reads
                    time.sleep(0.005)
            chunk = port.read(4096)
            if chunk:
                out.write(chunk)
                out.flush()
                sys.stdout.buffer.write(chunk)
                sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
