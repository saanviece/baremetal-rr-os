# A Bare-Metal Round-Robin OS on the ATmega328P

7th-semester minor project - Saanvi Chauhan, ECE. Mentor: Dr. Divya Prakash.

Several independent tasks share one ATmega328P (Arduino UNO, 16 MHz). A 1 ms timer interrupt preempts the running task; the
kernel saves its context (PC, r0-r31, SREG, SP), picks the next task in Round-Robin order and restores that task's context so it
continues exactly where it stopped. Register-level C and a little inline assembly. **No Arduino core, no RTOS, no HAL**; only the
avr-libc headers `avr/io.h` and `avr/interrupt.h` (register names and the `ISR` macro).

## What it does (demo, `src/main.c`)
| Task | LED | UNO pin | Blinks (toggle period) | UART line |
|------|-----|---------|------------------------|-----------|
| T1 | PB5 | D13 (on-board "L") | 250 ms | `T1` |
| T2 | PB0 | D8 | 500 ms | `T2` |
| T3 | PB1 | D9 | 1000 ms | `T3` |

The serial monitor (9600 baud, 8N1) shows `RR-OS: 3 tasks, 10 ms slice` and then `T1 T2 T3 T1 T2 T3 ...`.
Settings: 1 ms tick, 10-tick (10 ms) slice, 128-byte stacks, up to 4 tasks (`src/kernel.h`).

## Wiring
* T1 needs nothing (on-board LED).
* T2: D8 -> 220 ohm resistor -> LED anode (long leg); LED cathode (short leg) -> GND.
* T3: D9 -> 220 ohm resistor -> LED anode; cathode -> GND.
* USB cable to the PC provides power and the serial port (the UNO's USB chip forwards the UART TX pin).

## Build
Needs `avr-gcc`, `avr-libc`, `make` (see `docs/M0_toolchain.md`; on Windows: `winget install ZakKemble.avr-gcc`).
```bash
make            # builds build/main.hex and prints the size
make disasm     # build/main.lst : assembly listing
make clean
```

## Just run it (no board)
```bash
python tools/run_demo.py 6      # runs the OS for 6 simulated seconds and prints serial text + LED changes
```

## Flash to the UNO
Find the port (Windows Device Manager -> Ports, e.g. COM3; Linux `/dev/ttyACM0`) and run:
```bash
avrdude -c arduino -P COM3 -b 115200 -p m328p -U flash:w:build/main.hex
```
(`make flash PORT=COM3` does the same.) Then open a serial monitor at 9600 baud. Clone boards with a CH340 chip may need
`-b 57600` (old bootloader) - untested.

## Simulate and test (no board needed)
The tests run the firmware in **simavr** and read cycle-exact events (pin changes, UART bytes, interrupt durations, memory).
One-time setup of the simulator side is described in `docs/M0_toolchain.md` (build simavr with MSYS2, then `sh tools/build_simtest.sh`).
```bash
python tests/run_tests.py          # all groups M1..M8   (or: python tests/run_tests.py m6 m8)
```
The complete output of the last run is in [`docs/TEST_RESULTS.txt`](docs/TEST_RESULTS.txt): 64 checks, all passed.

## What has been tested - in simulation only (simavr, ATmega328P model at 16 MHz)
- LED blink on PB5 with direct register writes; toggle period exact in cycles (M1).
- UART output at 9600 baud: correct bytes and bit time of 1664 cycles = (UBRR+1)x16 (M2).
- 1 ms tick: average period 16000.001 cycles over 1967 ticks, tick counter equals interrupt count (M3, M8).
- Task start from a hand-built stack frame; every byte of the frame checked; r1 = 0 at task start; returning task becomes DEAD (M4).
- Context switch: tasks rotate 1, 2, 3 on every tick; all make progress (M5).
- Round-Robin with exact 10-tick slices, dead tasks skipped, idle task when nothing is ready or no tasks exist (M6).
- Demo: UART shows strict T1 T2 T3 rotation with no garbled lines; LED periods 250/500/1000 ms (mean within 1 %, single toggles up to 20 ms late) (M7).
- Registers r0-r31 and flags C and T survive 1999 + 299 real switches (mirror-pair checksum tasks); the same test fails when the kernel is
  deliberately broken (r7 corruption, carry corruption) (M8).
- Cost of a tick: 331 cycles (20.7 us) without and 372 cycles (23.2 us) with a task change, +4 cycles hardware entry; about 2.1 % of the CPU (M8).
- Stack canary: deliberate overflow detected 0.99 ms after the task got the CPU, message printed, system halted; no false alarm on the demo (M8).
- Stack use in the demo: tasks 49 bytes of 128, idle 37 bytes (M7/M8).

## NOT tested on real hardware yet (please do these on the UNO)
1. `avrdude` upload works and the board runs the program (bootloader hand-over; I added `TIFR1` and `UCSR0A` clears for leftover bootloader state, untested).
2. LEDs blink at the expected rates and visibly alternate; D8 and D9 wiring/resistors are right.
3. Serial monitor shows the banner and `T1 T2 T3` lines at 9600 baud without garbage (real 8N1 hardware sends 10 bits/char; simavr models 11).
4. Blink periods against a stopwatch (the UNO's resonator is less accurate than the simulator's perfect clock).
5. Run for several minutes and confirm no `!STACK OVERFLOW` message and the LED keeps its rhythm.
6. Optional: trigger the deliberate overflow test (`tests/fw/m8_overflow.c`) on the board; expect the message and a fast-blinking L LED.
Also not covered by anything: any difference between simavr and the real silicon (e.g. exact interrupt latency, sleep wake-up), and electrical effects.

## Project layout
```
src/            kernel.c/.h  context.h  timer.c/.h  uart.c/.h  main.c      <- the firmware (342 lines of C incl. comments)
tests/          run_tests.py  simlib.py  fw/*.c (test firmwares)
tools/          simtest.c (simavr harness)  build_simtest.sh  build_simavr.sh
docs/           M0..M8 step-by-step notes, ARCHITECTURE.md, LEARNING_GUIDE.md, VIVA_QA.md, REFERENCES.md, TEST_RESULTS.txt
Makefile
```
Start with `docs/LEARNING_GUIDE.md`.

## Scope
Round-Robin only. No priorities, mutexes, semaphores or `sleep()` (a task waits by watching the tick counter).
