"""Run the simulator tests. Usage: python tests/run_tests.py [m1 m2 ...]   (default: all)"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from simlib import *

SLICE = 10          # SLICE_TICKS default


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


def t_m4():
    r = Report("M4  TCB + fake starting context")
    elf = build("m4_stack")
    ev = run(elf, ms=5, args=["-b", "checkpoint", "-s", "tcbs", "-s", "stacks",
                              "-a", "tcbs", "-a", "stacks", "-a", "task_a", "-a", "task_b", "-a", "task_exit"])
    tcbs, stacks, A = ev["S"]["tcbs"], ev["S"]["stacks"], ev["A"]
    T = lambda i: tcbs[i * 8:(i + 1) * 8]            # one TCB = 8 bytes
    S = lambda i: stacks[i * 128:(i + 1) * 128]
    for i, fn in ((1, "task_a"), (2, "task_b")):
        t = T(i); base = A["stacks"] + 128 * i
        r.check(u16(t, 0) == base + 127 - 37, f"task {i}: saved SP = {u16(t, 0):#06x} = stack top - 37 bytes")
        r.check(t[2] == 1 and t[3] == i, f"task {i}: state READY, id {t[3]}")
        r.check(u16(t, 6) == base, f"task {i}: stack_low = {u16(t, 6):#06x} = start of its stack array")
        st = S(i)
        ex, en = A["task_exit"] // 2, A[fn] // 2         # AVR function pointers are WORD addresses
        want = bytes([ex & 255, ex >> 8, en & 255, en >> 8]) + bytes(33)
        got = bytes(st[127 - 36:128][::-1])
        r.check(got == want, f"task {i}: top 37 bytes = exit(lo,hi) {ex:#06x}, entry(lo,hi) {en:#06x}, 33 zero bytes (r0,SREG,r1..r31)")
        r.check(set(st[:90]) == {0xA5}, f"task {i}: 90 unused bytes incl. 8 canary bytes still 0xA5")
    r.check(u16(T(1), 4) == A["tcbs"] + 16 and u16(T(2), 4) == A["tcbs"] + 8,
            "circular list: task1.next -> task2, task2.next -> task1")

    r.info("-- now actually start a task through its fake frame --")
    elf = build("m4_launch")
    ev = run(elf, ms=5, args=["-s", "tcbs", "-m", "0x3e", "1", "-m", "0x4a", "1", "-p", "B5"])
    r.check(ev["M"][0x3E][0] == 0x5A, "task body ran (GPIOR0 marker = 0x5A)")
    r.check(ev["M"][0x4A][0] == 1, "r1 was 0 when the task started (marker = 1)")
    r.check(any(pin == "B5" for _, pin, _ in ev["P"]), "task toggled PB5")
    r.check(ev["S"]["tcbs"][8 + 2] == 2, "task returned into task_exit -> state = DEAD")
    return r.fails


def t_m5():
    r = Report("M5  context switch (naked ISR) - switch on every tick")
    elf = build("m5_switch", "-DSLICE_TICKS=1")
    ev = run(elf, ms=200, args=["-v", "11", "-k", "rr_current", "-s", "count", "-s", "tcbs", "-a", "tcbs"])
    v = ev["V"]
    tcb = lambda i: ev["A"]["tcbs"] + 8 * i
    nxt = {tcb(1): tcb(2), tcb(2): tcb(3), tcb(3): tcb(1)}
    r.check(len(v) >= 195, f"{len(v)} tick interrupts in 200 ms")
    r.check(v[0][2] == tcb(1), f"first task running is task 1 (rr_current = {v[0][2]:#06x})")
    r.check(all(nxt[b] == a for _, _, b, a in v), "every interrupt switched to the next TCB in the ring: 1 -> 2 -> 3 -> 1 ...")
    c = [int.from_bytes(ev["S"]["count"][2 * i:2 * i + 2], "little") for i in range(3)]
    r.check(all(x > 0 for x in c), f"all three tasks made progress: counters = {c}")
    cost = sorted({b - a for a, b, _, _ in v})
    r.info(f"ISR cost, vector jmp to end of reti: {cost} cycles = {cost[-1] / 16:.1f} us ({cost[-1] / 16000 * 100:.2f} % of one 1 ms tick)")
    return r.fails


def runs(seq):
    """[1,1,2,2,2] -> [(1, 2), (2, 3)]  (value, run length)"""
    out = []
    for x in seq:
        if out and out[-1][0] == x:
            out[-1][1] += 1
        else:
            out.append([x, 1])
    return [tuple(o) for o in out]


def t_m6():
    r = Report("M6  Round-Robin scheduler, dead tasks, idle")
    ARGS = ["-v", "11", "-k", "rr_current", "-s", "count", "-a", "tcbs"]
    tid = lambda ev, p: (p - ev["A"]["tcbs"]) // 8      # TCB address -> task id

    ev = run(build("m5_switch"), ms=400, args=ARGS)
    seq = runs(tid(ev, kb) for _, _, kb, _ in ev["V"])
    r.info("who ran during each tick (task id, ticks in a row): " + " ".join(f"T{a}x{n}" for a, n in seq[:7]) + " ...")
    full = seq[:-1]                                      # last run may be cut off by the end of the test
    r.check(all(n == SLICE for _, n in full), f"every slice lasted exactly {SLICE} ticks ({len(full)} slices checked)")
    r.check([a for a, _ in full] == [(i % 3) + 1 for i in range(len(full))], "slices go 1, 2, 3, 1, 2, 3 ... (round robin)")

    ev = run(build("m6_dead"), ms=400, args=ARGS)
    ids = [tid(ev, kb) for _, _, kb, _ in ev["V"]]
    c = [int.from_bytes(ev["S"]["count"][2 * i:2 * i + 2], "little") for i in range(3)]
    r.check(c[1] == 100, f"task 2 ran its 100 loops and returned (count = {c[1]})")
    last2 = max(i for i, x in enumerate(ids) if x == 2)
    r.check(last2 < 15 and 2 not in ids[last2 + 1:], f"task 2 is never scheduled again after tick {last2 + 1}")
    after = runs(ids[last2 + 1:])[:-1]
    alternating = all(a != b for (a, _), (b, _) in zip(after, after[1:])) and {a for a, _ in after} == {1, 3}
    r.check(all(n == SLICE for _, n in after) and alternating,
            "after that tasks 1 and 3 alternate in full slices: " + " ".join(f"T{a}x{n}" for a, n in after[:5]) + " ...")

    ev = run(build("m6_idle"), ms=100, args=ARGS)
    ids = [tid(ev, kb) for _, _, kb, _ in ev["V"]]
    r.check(len(ids) >= 98 and ids[10:] == [0] * (len(ids) - 10), f"all tasks returned -> idle task (id 0) runs from tick ~3; ticks keep coming ({len(ids)} in 100 ms)")

    ev = run(build("m6_none"), ms=100, args=ARGS)
    ids = [tid(ev, kb) for _, _, kb, _ in ev["V"]]
    r.check(len(ids) >= 98 and set(ids) == {0}, "kernel started with no tasks -> idle only, no crash")
    return r.fails


TESTS = {"m1": t_m1, "m2": t_m2, "m3": t_m3, "m4": t_m4, "m5": t_m5, "m6": t_m6}

if __name__ == "__main__":
    names = [a.lower() for a in sys.argv[1:]] or list(TESTS)
    bad = sum(TESTS[n]() for n in names)
    print("\nRESULT:", "ALL PASSED" if bad == 0 else f"{bad} CHECK(S) FAILED")
    sys.exit(1 if bad else 0)
