"""Run the simulator tests. Usage: python tests/run_tests.py [m1 m2 ... m9]   (default: all)"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from simlib import *

SLICE = 10          # SLICE_TICKS default
PRINT_EVERY = 10    # src/main.c: a demo task prints a line every 10th turn


def t_m1():
    r = Report("M1  LED blink on PB5 (direct register writes)")
    elf = build("m1_blink")
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
    r.check(all(abs(g - 11 * 4 * 16) <= 8 for g in gaps),
            "bit time = (UBRR+1) x 16 = 4 x 16 = 64 cycles (+-8 cycles polling jitter) -> exactly 250000 baud")
    r.info("note: real 8N1 hardware sends 10 bits/char = 640 cycles/char; simavr's 11-bit model is ~10 % slower")
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


def stack_use(stacks, i):
    """Bytes of stack task i has ever used (0xA5 fill pattern high-water mark) and whether its canary is intact."""
    st = stacks[i * 128:(i + 1) * 128]
    first_used = next((k for k, b in enumerate(st) if b != 0xA5), 128)
    return 128 - first_used, set(st[:8]) == {0xA5}


REPORT = re.compile(r"^ *(\d+)\.(\d{3}) s \| (T[123]) has the CPU \| turn +(\d+) \| (D13|D8|D9) LED (on|off)$")
LED_OF = {"T1": ("D13", "B5"), "T2": ("D8", "B0"), "T3": ("D9", "B1")}     # task -> (pin name printed, port pin)


def pin_level(events, pin, cycle):
    """Level of `pin` at `cycle`, from the pin-change events (low before its first change)."""
    level = 0
    for c, p, v in events:
        if c > cycle:
            break
        if p == pin:
            level = v
    return level


def expected_report(n):
    """(turn, time in ms) that report line n must show. Task n % 3 first runs at tick SLICE * (n % 3) and gets a turn
    every 3 * SLICE ticks, so its turn k starts at tick 3 * SLICE * (k - 1) + SLICE * (n % 3)."""
    turn = PRINT_EVERY * (n // 3 + 1)
    return turn, 3 * SLICE * (turn - 1) + SLICE * (n % 3)


def t_m7():
    r = Report("M7  demo: 3 tasks, 3 LEDs, UART report lines (main.elf, 5 s)")
    ev = run(build("main"), ms=5000, args=["-u", "-p", "B5", "-p", "B0", "-p", "B1", "-v", "11",
                                           "-s", "kernel_fault", "-s", "stacks"])
    text = bytes(b for _, b in ev["U"]).decode()
    intro, sep, body = text.partition("a task prints a line:\r\n\r\n")
    r.check(intro.startswith("\r\nRR-OS: ") and sep != "", "start-up text printed first: " + repr(intro.split("\r\n")[1]))
    lines = body.split("\r\n")[:-1]                     # complete lines only
    r.info("first report lines:")
    for line in lines[:3]:
        r.info("    " + line)
    found = [REPORT.match(line) for line in lines]
    r.check(len(lines) >= 30 and all(found), f"{len(lines)} report lines in 5 s, all in the expected format (no garbled line)")
    found = [m for m in found if m]
    names = [m.group(3) for m in found]
    r.check(names == [f"T{i % 3 + 1}" for i in range(len(names))], "lines come in strict rotation T1 T2 T3 T1 T2 T3 ...")
    times = [int(m.group(1)) * 1000 + int(m.group(2)) for m in found]
    exact = all((int(m.group(4)), times[n]) == expected_report(n) and m.group(5) == LED_OF[m.group(3)][0]
                for n, m in enumerate(found))
    r.check(exact, "numbers in the lines are exact: turn 10 at 0.270 / 0.280 / 0.290 s, turn 20 at 0.570 / 0.580 / 0.590 s, ...")
    # Tick k is the k-th run of vector 11; sample the LED pin 5 ms into the slice that tick started.
    truthful = all((pin_level(ev["P"], LED_OF[m.group(3)][1], ev["V"][times[n] - 1][1] + 5 * 16000) == 1) == (m.group(6) == "on")
                   for n, m in enumerate(found))
    r.check(truthful, "every 'LED on' / 'LED off' in the lines matches the real pin level at that time")
    for pin, half in (("B5", 250), ("B0", 500), ("B1", 1000)):
        t = [c for c, p, v in ev["P"] if p == pin]
        gaps = [(b - a) / 16000 for a, b in zip(t, t[1:])]
        mean = (t[-1] - t[0]) / (len(t) - 1) / 16000
        r.check(abs(mean - half) < half * 0.01 and max(abs(g - half) for g in gaps) <= 25,
                f"{pin}: {len(t)} toggles, mean {mean:.1f} ms (want {half}), worst single gap off by {max(abs(g - half) for g in gaps):.1f} ms")
    r.check(ev["S"]["kernel_fault"] == b"\x00", "no stack fault reported")
    for i in (1, 2, 3, 0):
        used, ok = stack_use(ev["S"]["stacks"], i)
        r.check(ok and used < 128 - 8 - 16, f"{'task ' + str(i) if i else 'idle'}: used {used} of 128 stack bytes at most, canary intact")
    return r.fails


def t_m8():
    r = Report("M8  measurements and stress tests")
    base = ["-v", "11", "-k", "rr_current", "-m", "0x3e", "1", "-p", "B5", "-p", "B0", "-p", "B1", "-s", "stacks", "-s", "ticks"]

    # ---- 1. registers + flags survive context switches --------------------------------------------------------
    r.info("-- 1. registers r0-r31 and flags C,T survive switches (3 assembly tasks, mirror-pair checksums) --")
    for label, flags, ms in (("switch every tick (SLICE_TICKS=1)", "-DSLICE_TICKS=1", 2000), ("default 10-tick slices", "", 3000)):
        ev = run(build("m8_regs", flags), ms=ms, args=base)
        sw = sum(1 for _, _, kb, ka in ev["V"] if kb != ka)
        hb = {p: sum(1 for _, q, _ in ev["P"] if q == p) for p in ("B5", "B0", "B1")}
        r.check(ev["M"][0x3E][0] == 0 and min(hb.values()) > 1000,
                f"{label}: {sw} real task switches in {ms} ms, GPIOR0 fail flags = {ev['M'][0x3E][0]:#04x}, "
                f"checksum passes (heartbeats) per task = {hb}")
    r.info("-- negative controls: break the kernel on purpose, the same test must now FAIL --")
    for flag, what in (("-DFAULT_R7", "ISR corrupts r7 after every restore"), ("-DFAULT_CARRY", "ISR clears the carry flag after every restore")):
        ev = run(build("m8_regs", "-DSLICE_TICKS=1 " + flag), ms=500, args=base)
        g = ev["M"][0x3E][0]
        r.check(g != 0, f"{what}: test detects it (GPIOR0 fail flags = {g:#04x})")

    # ---- 2. tick period with the full kernel running ----------------------------------------------------------
    r.info("-- 2. tick period with the scheduler running (main.elf, 3 tasks) --")
    ev = run(build("main"), ms=2000, args=["-v", "11", "-k", "rr_current", "-s", "ticks"])
    v = ev["V"]
    mean = (v[-1][0] - v[0][0]) / (len(v) - 1)
    ticks = int.from_bytes(ev["S"]["ticks"], "little")
    r.check(abs(mean - 16000) < 0.1, f"average tick period = {mean:.3f} cycles = {mean / 16000:.5f} ms over {len(v)} ticks")
    r.check(ticks == len(v), f"`ticks` in SRAM = {ticks} = number of tick interrupts seen")

    # ---- 3. context-switch cost -------------------------------------------------------------------------------
    r.info("-- 3. interrupt cost: vector jmp to end of reti (hardware entry adds 4 cycles more) --")
    sw = [b - a for a, b, kb, ka in v if kb != ka]
    st = [b - a for a, b, kb, ka in v if kb == ka]
    r.check(len(sw) > 0 and len(st) > 0, f"{len(sw)} switching ticks, {len(st)} non-switching ticks observed")
    r.info(f"tick WITHOUT task change: {min(st)}..{max(st)} cycles ({max(st) / 16:.1f} us)")
    r.info(f"tick WITH task change   : {min(sw)}..{max(sw)} cycles ({max(sw) / 16:.1f} us)  [+4 hardware entry]")
    per_slice = sum(st) / len(st) * 9 + sum(sw) / len(sw)
    r.info(f"overhead of the kernel: {per_slice / 160000 * 100:.2f} % of CPU time (10 ticks = 160000 cycles per slice)")
    r.check(max(sw) < 400, "switch costs less than 400 cycles = 25 us")

    # ---- 4. stack use and the deliberate overflow -------------------------------------------------------------
    r.info("-- 4. stack canary: deliberate overflow of task 2 --")
    ev = run(build("m8_overflow"), ms=200, args=["-u", "-v", "11", "-k", "rr_current", "-s", "kernel_fault",
                                                  "-s", "stacks", "-a", "tcbs"])
    text = bytes(b for _, b in ev["U"]).decode()
    r.check("!STACK OVERFLOW T2" in text, f"UART says: {text.strip()!r}")
    r.check(ev["S"]["kernel_fault"] == b"\x03", "kernel_fault = 3 (= task id 2 + 1)")
    v = ev["V"]
    to_task2 = (v[-1][3] - ev["A"]["tcbs"]) // 8
    lat = ev["U"][0][0] - v[-1][1]
    r.check(to_task2 == 2, f"last completed tick handed the CPU to task 2 (tick {len(v)}); the very next tick found the canary smashed and halted the system")
    r.check(lat < 16000 + 2000, f"detection latency = {lat} cycles = {lat / 16000:.2f} ms after task 2 got the CPU (at most one tick)")
    r.check(len(ev["V"]) == 10, "no more ticks completed after the panic: kernel stopped (interrupts off)")
    st2 = ev["S"]["stacks"][2 * 128:3 * 128]
    r.info(f"canary bytes of task 2 after overflow: {st2[:8].hex()} (was a5a5a5a5a5a5a5a5)")
    r.check(set(st2[:8]) != {0xA5}, "the canary really was overwritten")
    _, ok1 = stack_use(ev["S"]["stacks"], 1)
    r.info(f"task 1 canary intact: {ok1} (task 2's overflow ran downward into task 1's stack top, which is stale)")
    ev = run(build("m8_overflow", "-DLEVELS=1"), ms=200, args=["-u", "-s", "kernel_fault"])
    r.check(ev["U"] == [] and ev["S"]["kernel_fault"] == b"\x00", "control: same program recursing only 2 calls deep (~60 bytes) -> no false alarm")
    return r.fails


FRAME_LEN = 141     # src/trace.c: 13-byte head + the paused task's 128-byte stack


def split_stream(uart):
    """[(cycle, byte)] -> (text, frames). A byte >= 0x80 starts a frame (text never has bit 7 set)."""
    text, frames, i = bytearray(), [], 0
    while i < len(uart):
        c, b = uart[i]
        if b >= 0x80 and i + FRAME_LEN <= len(uart):
            f = bytes(x for _, x in uart[i:i + FRAME_LEN])
            frames.append(dict(cycle=c, to=f[0] & 0x7F, tick=int.from_bytes(f[1:5], "little"), nsw=u16(f, 5), frm=f[7],
                               portb=f[8], sp=u16(f, 9), low=u16(f, 11), stack=f[13:]))
            i += FRAME_LEN
            continue
        text.append(b)
        i += 1
    return text.decode("ascii"), frames


def t_m9():
    r = Report("M9  link to the dashboard: a switch frame per task change, only after 'T' (main.elf, 3 s)")
    T_ON, T_OFF = 100, 2000                              # ms: the simulated PC sends 'T', later 't'
    ev = run(build("main"), ms=3000, args=["-u", "-r", str(T_ON), "84", "-r", str(T_OFF), "116", "-v", "11", "-k", "rr_current",
                                           "-p", "B5", "-p", "B0", "-p", "B1", "-s", "stacks", "-a", "tcbs", "-a", "stacks", "-a", "task_exit"])
    A, uart, V = ev["A"], ev["U"], ev["V"]
    first = next((k for k, (_, b) in enumerate(uart) if b >= 0x80), None)
    r.check(first is not None and uart[first][0] > T_ON * 16000, f"no frame byte before 'T' was sent: the first one comes at {uart[first][0] / 16000:.2f} ms")
    text, frames = split_stream(uart)
    lines = text.partition("a task prints a line:\r\n\r\n")[2].split("\r\n")[:-1]
    r.check(len(lines) >= 25 and all(REPORT.match(l) for l in lines), f"{len(lines)} report lines between the frames, none garbled")
    names = [REPORT.match(l).group(3) for l in lines]
    r.check(names == [f"T{i % 3 + 1}" for i in range(len(names))], "the lines still come in strict rotation T1 T2 T3")
    on = [f for f in frames if f["cycle"] < T_OFF * 16000 + 16000]
    late = [f for f in frames if f["cycle"] >= T_OFF * 16000 + 16000]
    r.info(f"{len(frames)} frames; first: tick {frames[0]['tick']}, switch #{frames[0]['nsw']}, T{frames[0]['frm']} -> T{frames[0]['to']}")
    r.check(len(on) > 150 and all(b["nsw"] == a["nsw"] + 1 and b["tick"] - a["tick"] == SLICE for a, b in zip(on, on[1:])),
            f"{len(on)} frames while on: consecutive switch numbers, 10 ticks apart, none missing")
    r.check(late == [], f"after 't' at {T_OFF} ms the frames stop (frames later: {len(late)})")
    # every frame against the simulator's own record of that switch
    sw = [v for v in V if v[2] != v[3]]                 # tick interrupts that changed rr_current
    tid = lambda a: (a - A["tcbs"]) // 8
    def pins_at(cycle):
        return sum(m for pin, m in (("B5", 0x20), ("B0", 0x01), ("B1", 0x02)) if pin_level(ev["P"], pin, cycle))
    match = all(f["nsw"] <= len(sw) and V.index(sw[f["nsw"] - 1]) + 1 == f["tick"] and tid(sw[f["nsw"] - 1][2]) == f["frm"]
                and tid(sw[f["nsw"] - 1][3]) == f["to"] and pins_at(sw[f["nsw"] - 1][1]) == f["portb"] for f in on)
    r.check(match, "each frame's tick, switch number, paused task, next task and PORTB equal what the simulator recorded at that switch")
    exit_w = A["task_exit"] // 2
    stack_ok = all(f["low"] == A["stacks"] + 128 * f["frm"] and f["low"] <= f["sp"] < f["low"] + 128 - 35
                   and f["stack"][:8] == b"\xa5" * 8 and (f["stack"][126] << 8 | f["stack"][127]) == exit_w for f in on)
    r.check(stack_ok, f"each frame's stack: right address, saved SP inside it, canary intact, task_exit ({exit_w:#06x}) planted at the top")
    cost = [v[1] - v[0] for v in sw if T_ON * 16000 < v[0] < T_OFF * 16000]
    plain = [v[1] - v[0] for v in sw if v[0] > T_OFF * 16000 + 16000]
    r.info(f"tick WITH task change: {min(plain)}..{max(plain)} cycles without frames, {min(cost)}..{max(cost)} cycles "
           f"({max(cost) / 16:.0f} us) while sending frames: {(max(cost) - max(plain)) / 160000 * 100:.2f} % more CPU time")
    r.check(max(cost) < 16000 // 4, "a switch with a frame still takes far less than a tick (< 0.25 ms)")
    for i in (1, 2, 3):
        used, ok = stack_use(ev["S"]["stacks"], i)
        r.check(ok and used < 128 - 8 - 16, f"task {i}: used {used} of 128 stack bytes with frames on, canary intact")
    return r.fails


TESTS = {"m1": t_m1, "m2": t_m2, "m3": t_m3, "m4": t_m4, "m5": t_m5, "m6": t_m6, "m7": t_m7, "m8": t_m8, "m9": t_m9}

if __name__ == "__main__":
    names = [a.lower() for a in sys.argv[1:]] or list(TESTS)
    bad = sum(TESTS[n]() for n in names)
    print("\nRESULT:", "ALL PASSED" if bad == 0 else f"{bad} CHECK(S) FAILED")
    sys.exit(1 if bad else 0)
