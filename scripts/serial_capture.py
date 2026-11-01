#!/usr/bin/env python3
"""Capture the RT-Spark serial console to a file (and echo it) for a fixed duration.

Usage:
    .venv/bin/python scripts/serial_capture.py --port /dev/cu.usbmodem3103 \
        --seconds 60 --out docs/evidence/m2/serial-boot.log
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
    args = ap.parse_args()

    deadline = time.monotonic() + args.seconds
    with serial.Serial(args.port, args.baud, timeout=0.2) as port, open(args.out, "wb") as out:
        port.reset_input_buffer()
        while time.monotonic() < deadline:
            chunk = port.read(4096)
            if chunk:
                out.write(chunk)
                out.flush()
                sys.stdout.buffer.write(chunk)
                sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
