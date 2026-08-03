#!/usr/bin/env python3
"""Build a benchmark environment, load it into the RP2350's SRAM, capture output.

Loads with `picotool load -x`, which writes to SRAM and executes, so an iteration
never touches flash. The board's own flash image is left alone and comes back on
the next power cycle.

Usage:
    tools/run_bench.py [env] [-o results.txt] [--timeout 90]

The benchmark firmware waits for the host to open the CDC port before printing,
so there is no race between re-enumeration and the start of the report.
"""

import argparse
import glob
import os
import subprocess
import sys
import time

import serial

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
END_MARKER = "end of benchmark"
PIO = "pio"


def run(cmd, **kw):
    return subprocess.run(cmd, cwd=REPO, check=True, **kw)


def build(env):
    print(f"[build] {env}", file=sys.stderr)
    run([PIO, "run", "-e", env], stdout=subprocess.DEVNULL)
    elf = os.path.join(REPO, ".pio", "build", env, "firmware.elf")
    if not os.path.exists(elf):
        sys.exit(f"no ELF produced at {elf}")
    return elf


def load_to_ram(elf):
    print("[load] picotool load -x (SRAM, flash untouched)", file=sys.stderr)
    # -f forces a reboot into BOOTSEL from the running application; -x starts the
    # loaded image instead of leaving the board in BOOTSEL.
    run(["picotool", "load", "-x", "-f", elf],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_for_port(timeout=25.0):
    """Wait for the CDC port to come back after the RAM image starts."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        ports = sorted(glob.glob("/dev/ttyACM*"))
        if ports:
            # Give udev a moment to finish applying permissions.
            time.sleep(0.4)
            return ports[0]
        time.sleep(0.2)
    sys.exit("serial port never reappeared")


def capture(port, timeout):
    print(f"[read] {port}", file=sys.stderr)
    lines = []
    deadline = time.time() + timeout
    # Opening with dsrdtr default asserts DTR, which is what the firmware waits
    # for before it starts printing.
    with serial.Serial(port, 115200, timeout=1.0) as ser:
        ser.dtr = True
        while time.time() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            lines.append(line)
            print(line)
            if END_MARKER in line:
                return lines
    print("[warn] timed out before end marker", file=sys.stderr)
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("env", nargs="?", default="bench_2350b")
    ap.add_argument("-o", "--output")
    ap.add_argument("--timeout", type=float, default=90.0)
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()

    elf = (os.path.join(REPO, ".pio", "build", args.env, "firmware.elf")
           if args.no_build else build(args.env))
    load_to_ram(elf)
    lines = capture(wait_for_port(), args.timeout)

    if args.output:
        with open(args.output, "w") as fh:
            fh.write(f"# {args.env}\n")
            fh.write("\n".join(lines) + "\n")
        print(f"[saved] {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
