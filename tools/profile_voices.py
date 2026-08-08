#!/usr/bin/env python3
"""Measure the production render loop against voice count, on real hardware.

Loads the production firmware into SRAM, then plays notes over the control link
(the "midi" command) while reading back the per-buffer render profile the
firmware emits. This is the number that decides whether the audio glitches: the
*worst* buffer, under real conditions - both cores busy, DMA running, USB active.

Usage:
    tools/profile_voices.py [env] [--voices 0,1,2,4,8] [-o results.txt]
"""

import argparse
import glob
import json
import os
import subprocess
import sys
import time

import serial

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_NOTE = 48
SETTLE_S = 4.0          # let the profile window turn over after a change
COLLECT_S = 9.0         # several 500-buffer windows


def build_and_load(env, skip_build=False):
    if not skip_build:
        print(f"[build] {env}", file=sys.stderr)
        subprocess.run(["pio", "run", "-e", env], cwd=REPO, check=True,
                       stdout=subprocess.DEVNULL)
    elf = os.path.join(REPO, ".pio", "build", env, "firmware.elf")
    print("[load] picotool load -x (SRAM)", file=sys.stderr)
    subprocess.run(["picotool", "load", "-x", "-f", elf], cwd=REPO, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 25
    while time.time() < deadline:
        ports = sorted(glob.glob("/dev/ttyACM*"))
        if ports:
            time.sleep(0.6)
            return ports[0]
        time.sleep(0.2)
    sys.exit("serial port never reappeared")


def note_on(ser, note):
    ser.write(json.dumps({"cmd": "midi", "bytes": [0x90, note, 100]}).encode() + b"\n")


def note_off(ser, note):
    ser.write(json.dumps({"cmd": "midi", "bytes": [0x80, note, 0]}).encode() + b"\n")


def set_param(ser, name, value):
    ser.write(json.dumps({"cmd": "setParam", "param": name, "value": value}).encode() + b"\n")


# Every modulation destination engaged, plus audible vibrato and tremolo so both
# LFOs actually run. This is the case that decides real headroom: optimisations
# that only pay off on a static patch are worth nothing if turning the knobs up
# drops polyphony.
FULL_MODULATION = {
    "vibratoDepth": 0.5, "tremoloDepth": 0.5,
    "baseCutoff_atMod": 0.6, "filterQ_atMod": 0.4, "filterEnvAmount_atMod": 0.5,
    "filterEnvSustain_atMod": 0.4,
    "vibratoDepth_atMod": 0.5, "vibratoRate_atMod": 0.4,
    "tremoloDepth_atMod": 0.5, "tremoloRate_atMod": 0.4,
    # Level defaults to unity, so only a negative atMod actually moves it - a
    # positive one would clamp immediately and measure nothing.
    "waveformShape_atMod": 0.5, "level_atMod": -0.3, "pitchBend_atMod": 2.0,
}


def apply_full_modulation(ser):
    for name, value in FULL_MODULATION.items():
        set_param(ser, name, value)
    ser.flush()
    time.sleep(0.5)


def stream_aftertouch(ser, notes, seconds, rate_hz=60.0):
    """Send polyphonic aftertouch for every held note, at the keyboard's rate.

    All the notes for one cycle go in a single command so the device does one JSON
    parse per cycle rather than one per note; the MIDI bytes that reach the synth
    are identical either way. The real keyboard produces these internally, so this
    still asks core 0 to do work it otherwise wouldn't - it is a pessimistic test.
    """
    period = 1.0 / rate_hz
    end = time.time() + seconds
    tick = 0
    while time.time() < end:
        payload = []
        for note in notes:
            # Keep the pressure moving so no change-detection cache in the voice
            # can settle.
            pressure = 20 + (tick * 37 + note * 11) % 100
            payload += [0xA0, note, pressure]
        if payload:
            ser.write(json.dumps({"cmd": "midi", "bytes": payload}).encode() + b"\n")
        tick += 1
        time.sleep(period)


def collect(ser, seconds):
    """Read render-profile records for a while and merge them."""
    worst = None
    end = time.time() + seconds
    records = []
    while time.time() < end:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", "replace").strip()
        if '"type":"render"' not in line:
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    if not records:
        return None
    # Merge: min of mins, max of maxes, mean of means, sum of fault counters.
    worst = {
        "buffers": sum(r["buffers"] for r in records),
        "minPct10": min(r["minPct10"] for r in records),
        "avgPct10": sum(r["avgPct10"] for r in records) // len(records),
        "maxPct10": max(r["maxPct10"] for r in records),
        "minCycles": min(r["minCycles"] for r in records),
        "maxCycles": max(r["maxCycles"] for r in records),
        "overBudget": sum(r["overBudget"] for r in records),
        "starved": sum(r["starved"] for r in records),
        "samples": len(records),
    }
    return worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("env", nargs="?", default="prod_ram_2350b")
    ap.add_argument("--voices", default="0,1,2,4,6,8")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--stress", action="store_true",
                    help="engage every modulation destination and stream aftertouch")
    ap.add_argument("-o", "--output")
    args = ap.parse_args()

    counts = [int(v) for v in args.voices.split(",")]
    port = build_and_load(args.env, args.no_build)

    rows = []
    with serial.Serial(port, 115200, timeout=1.0) as ser:
        ser.dtr = True
        time.sleep(3.0)          # firmware boot + calibration banner
        ser.reset_input_buffer()

        if args.stress:
            apply_full_modulation(ser)

        held = []
        for n in counts:
            # Adjust the set of held notes to exactly n.
            while len(held) > n:
                note_off(ser, held.pop())
            while len(held) < n:
                note = BASE_NOTE + 3 * len(held)   # spread them out
                note_on(ser, note)
                held.append(note)
            ser.flush()
            if args.stress and held:
                # Pressure has to be arriving *before* and *during* the window.
                stream_aftertouch(ser, held, SETTLE_S)
                ser.reset_input_buffer()
                import threading
                stop = threading.Event()
                t = threading.Thread(target=stream_aftertouch,
                                     args=(ser, held, COLLECT_S + 1.0), daemon=True)
                t.start()
                r = collect(ser, COLLECT_S)
                stop.set()
                t.join(timeout=2.0)
            else:
                time.sleep(SETTLE_S)
                ser.reset_input_buffer()
                r = collect(ser, COLLECT_S)
            if r is None:
                print(f"{n:>2} voices: no data", file=sys.stderr)
                continue
            r["voices"] = n
            rows.append(r)
            print(f"{n:>2} voices held: "
                  f"min {r['minPct10']/10:5.1f}%  avg {r['avgPct10']/10:5.1f}%  "
                  f"max {r['maxPct10']/10:6.1f}%   "
                  f"maxCycles {r['maxCycles']:>8}  "
                  f"overBudget {r['overBudget']:>4}  starved {r['starved']:>4}")

        for note in held:
            note_off(ser, note)

    if args.output:
        with open(args.output, "w") as fh:
            json.dump(rows, fh, indent=2)
        print(f"[saved] {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
