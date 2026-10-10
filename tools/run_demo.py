"""Run the OS demo (src/main.c) in the simulator - no board needed.

Usage:  python tools/run_demo.py [seconds]      (default 6)

Prints what you would see on a real UNO: the serial monitor text and every LED change with its time.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests"))
from simlib import build, run, CPU_HZ

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 6
print("Building the OS (make) ...")
elf = build("main")
print(f"Running {secs:g} s of simulated time on a virtual ATmega328P @ 16 MHz ...\n")
ev = run(elf, ms=int(secs * 1000), args=["-u", "-p", "B5", "-p", "B0", "-p", "B1"])

LED = {"B5": "T1 LED  D13 (PB5)", "B0": "T2 LED  D8  (PB0)", "B1": "T3 LED  D9  (PB1)"}
rows = []                                           # (time in cycles, text)
line = ""
for cyc, byte in ev["U"]:                           # rebuild serial lines, stamp each with its last byte's time
    if byte == 10:
        rows.append((cyc, f"SERIAL  > {line.strip()}"))
        line = ""
    else:
        line += chr(byte)
for cyc, pin, val in ev["P"]:
    rows.append((cyc, f"{LED[pin]} -> {'ON ' if val else 'OFF'}"))

for cyc, text in sorted(rows):
    print(f"{cyc / CPU_HZ:7.3f} s  {text}")
print(f"\nDone. {len(ev['U'])} serial bytes, {len(ev['P'])} LED changes.")
