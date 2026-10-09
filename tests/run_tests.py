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


TESTS = {"m1": t_m1}

if __name__ == "__main__":
    names = [a.lower() for a in sys.argv[1:]] or list(TESTS)
    bad = sum(TESTS[n]() for n in names)
    print("\nRESULT:", "ALL PASSED" if bad == 0 else f"{bad} CHECK(S) FAILED")
    sys.exit(1 if bad else 0)
