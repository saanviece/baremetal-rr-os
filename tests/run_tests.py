"""Run the simulator tests. Usage: python tests/run_tests.py [m1 m2 ...]   (default: all)"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from simlib import *


def t_m1():
    r = Report("M1  LED blink on PB5 (direct register writes)")
    elf = build("main")
    ev = run(elf, ms=2000, args=["-p", "B5"])
    t = [c for c, pin, v in ev["P"] if pin == "B5"]
    r.check(len(t) >= 4, f"PB5 changed level {len(t)} times in 2000 ms")
    gaps = [b - a for a, b in zip(t, t[1:])]
    r.check(len(set(gaps)) == 1, f"every toggle gap identical: {sorted(set(gaps))} cycles")
    ms = gaps[0] / CPU_HZ * 1000
    r.info(f"toggle gap = {gaps[0]} cycles = {ms:.2f} ms")
    r.check(490 < ms < 510, "blink half-period is about 500 ms (bring-up delay loop, not exact)")
    return r.fails


def t_m2():
    r = Report("M2  UART transmit in registers")
    elf = build("m2_uart")
    ev = run(elf, ms=30, args=["-u"])
    text = bytes(b for _, b in ev["U"]).decode()
    r.check(text == "Hello, UART\r\n", f"bytes received = {text!r}")
    gaps = [b[0] - a[0] for a, b in zip(ev["U"], ev["U"][1:])]
    bit = gaps[0] / 11          # simavr always counts 11 bits per character (it includes a parity slot)
    r.info(f"cycles between bytes = {gaps[0]}; simavr counts 11 bits/char, so one bit = {bit:.0f} cycles")
    r.check(all(abs(g - 11 * 104 * 16) <= 8 for g in gaps),
            "bit time = (UBRR+1) x 16 = 104 x 16 = 1664 cycles (+-8 cycles polling jitter) -> 9615 baud, +0.16 % vs 9600")
    r.info("note: real 8N1 hardware sends 10 bits/char = 16640 cycles/char; simavr's 11-bit model is ~10 % slower")
    return r.fails


def t_m3():
    r = Report("M3  Timer1 1 ms tick interrupt")
    elf = build("m3_tick")
    ev = run(elf, ms=1100, args=["-v", "11", "-k", "ticks", "-p", "B5", "-s", "ticks"])
    v = ev["V"]
    r.check(1098 <= len(v) <= 1100, f"tick interrupt ran {len(v)} times in 1100 ms")
    gaps = [b[0] - a[0] for a, b in zip(v, v[1:])]
    mean = (v[-1][0] - v[0][0]) / (len(v) - 1)
    r.check(abs(mean - 16000) < 0.05, f"average tick period over {len(v)} ticks = {mean:.3f} cycles (16000 = exactly 1.000 ms)")
    r.check(max(gaps) - min(gaps) <= 24, f"entry jitter = {min(gaps)}..{max(gaps)} cycles (CPU finishes its current instruction first)")
    ticks = int.from_bytes(ev["S"]["ticks"], "little")
    r.check(ticks == len(v), f"ticks variable in SRAM = {ticks}, matches interrupt count")
    r.check(all(after == before + 1 for _, _, before, after in v), "ticks went up by exactly 1 in every interrupt")
    dur = {b - a for a, b, _, _ in v}
    r.info(f"ISR duration (first vector instruction to end of reti): {sorted(dur)} cycles")
    p = [c for c, pin, val in ev["P"] if pin == "B5"]
    r.check(len(p) == 2, f"PB5 toggled {len(p)} times in 1.1 s (every 500 ticks)")
    r.info(f"PB5 toggle gap = {(p[1] - p[0]) / 16000:.2f} ms")
    return r.fails


TESTS = {"m1": t_m1, "m2": t_m2, "m3": t_m3}

if __name__ == "__main__":
    names = [a.lower() for a in sys.argv[1:]] or list(TESTS)
    bad = sum(TESTS[n]() for n in names)
    print("\nRESULT:", "ALL PASSED" if bad == 0 else f"{bad} CHECK(S) FAILED")
    sys.exit(1 if bad else 0)
