# A Bare-Metal Round-Robin OS on the ATmega328P

7th-semester minor project - Saanvi Chauhan, ECE. Mentor: Dr. Divya Prakash.

Several independent tasks share one ATmega328P (Arduino UNO, 16 MHz). A 1 ms timer interrupt preempts the running task; the
kernel saves its context (PC, r0-r31, SREG, SP), picks the next task in Round-Robin order and restores that task's context so it
continues exactly where it stopped. Register-level C and a little inline assembly. **No Arduino core, no RTOS, no HAL**; only the
avr-libc headers `avr/io.h` and `avr/interrupt.h` (register names and the `ISR` macro).

## What it does (demo, `src/main.c`)
| Task | LED | UNO pin | Blinks (toggle period) |
|------|-----|---------|------------------------|
| T1 | PB5 | D13 (on-board "L") | 250 ms |
| T2 | PB0 | D8 | 500 ms |
| T3 | PB1 | D9 | 1000 ms |

Every 10th time a task gets the CPU it prints one line, so the serial monitor (**250000 baud**, 8N1) explains what is happening:
```
RR-OS: a Round-Robin OS running 3 tasks on one CPU
Every 1 ms a timer interrupt runs the kernel, and every
10 ms the kernel pauses the running task and starts
the next one: T1 -> T2 -> T3 -> T1 -> ...
T1 blinks D13 (250 ms), T2 D8 (500 ms), T3 D9 (1 s)
Every 10th turn, a task prints a line:

    0.270 s | T1 has the CPU | turn     10 | D13 LED on
    0.280 s | T2 has the CPU | turn     10 | D8 LED off
    0.290 s | T3 has the CPU | turn     10 | D9 LED off
    0.570 s | T1 has the CPU | turn     20 | D13 LED off
```
The time is counted from the moment the kernel started. The three lines of a group are 10 ms apart (one slice each), and all
tasks always show the same turn number: equal turns in a fixed order, which is Round-Robin.
Settings: 1 ms tick, 10-tick (10 ms) slice, 128-byte stacks, up to 4 tasks (`src/kernel.h`).

## The live dashboard (`src/trace.c` + RR-OS Scope)
The serial monitor shows one line every 100 ms. To see everything the board does, there is a small link to a web dashboard
(RR-OS Scope, kept next to the Arduino sketch in the `visualizer` folder; it is not part of this repository).
When the dashboard sends the letter `T`, the kernel adds one binary **switch frame** (141 bytes) to the serial output at every
task change: the tick, the switch number, which task was paused and which one gets the CPU, `PORTB` (the LEDs), the paused
task's saved stack pointer and its whole 128-byte stack, including the 35 bytes the interrupt just saved (PC, r0-r31, SREG).
Frames start with a byte of 0x80 or more, which the text never contains. The Arduino IDE's Serial Monitor never sends `T`,
so it only ever shows the text. Details: `docs/M9_dashboard_link.md`.

## Wiring
* T1 needs nothing (on-board LED).
* T2: D8 -> 220 ohm resistor -> LED anode (long leg); LED cathode (short leg) -> GND.
* T3: D9 -> 220 ohm resistor -> LED anode; cathode -> GND.
* USB cable to the PC provides power and the serial port (the UNO's USB chip forwards the UART TX and RX pins).

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
Find the port (Windows Device Manager -> Ports, e.g. COM5; Linux `/dev/ttyACM0`) and run:
```bash
avrdude -c arduino -P COM5 -b 115200 -p m328p -U flash:w:build/main.hex
```
(`make flash PORT=COM5` does the same.) Then open a serial monitor at **250000 baud**. Clone boards with a CH340 chip may need
`-b 57600` (old bootloader) - untested.

**Or with the Arduino IDE:** put copies of `src/*.c` and `src/*.h` in a sketch folder next to an `.ino` file that holds only a
comment, choose *Arduino Uno* and the port, and click *Upload*. `main()` is in `main.c`, so the Arduino core is never linked in
(checked with `avr-nm`: the program contains only this project's functions). The IDE uses an older compiler (avr-gcc 7.3 with
link-time optimisation), so its cycle counts and stack use differ a little from the `make` build that the simulator numbers
below come from. For the dashboard, upload with `python build_firmware.py --upload COM5` in the `visualizer` folder: it uploads
exactly the build whose addresses and source lines the dashboard knows.

## Simulate and test (no board needed)
The tests run the firmware in **simavr** and read cycle-exact events (pin changes, UART bytes, interrupt durations, memory).
One-time setup of the simulator side is described in `docs/M0_toolchain.md` (build simavr with MSYS2, then `sh tools/build_simtest.sh`).
```bash
python tests/run_tests.py          # all groups M1..M9   (or: python tests/run_tests.py m6 m9)
```
The complete output of the last run is in [`docs/TEST_RESULTS.txt`](docs/TEST_RESULTS.txt): 76 checks, all passed.

## What has been tested in simulation (simavr, ATmega328P model at 16 MHz, `make` build)
- LED blink on PB5 with direct register writes; toggle period exact in cycles (M1).
- UART output at 250000 baud: correct bytes and bit time of 64 cycles = (UBRR+1)x16 (M2).
- 1 ms tick: average period 16000.001 cycles (= 1.00000 ms) over 1985 ticks with the whole demo running; the tick counter equals
  the interrupt count (M3, M8). In a separate 6 s run, single ticks were at most 24 cycles (1.5 us) late, 21 with frames on.
- Task start from a hand-built stack frame; every byte of the frame checked; r1 = 0 at task start; returning task becomes DEAD (M4).
- Context switch: tasks rotate 1, 2, 3 on every tick; all make progress (M5).
- Round-Robin with exact 10-tick slices, dead tasks skipped, idle task when nothing is ready or no tasks exist (M6).
- Demo: the start-up text comes first; every report line has the expected format and they come in strict T1 T2 T3 rotation with
  exact numbers (turn 10 at 0.270 / 0.280 / 0.290 s, then every 300 ms); every printed "LED on/off" matches the real pin;
  LED periods 250/500/1000 ms (mean within 1 %, single toggles up to 20 ms late) (M7).
- Registers r0-r31 and flags C and T survive 1999 + 299 real switches (mirror-pair checksum tasks); the same test fails when the kernel is
  deliberately broken (r7 corruption, carry corruption) (M8).
- Cost of a tick: 331 cycles (20.7 us) without and 380 cycles (23.8 us) with a task change, +4 cycles hardware entry; about 2.1 % of the CPU (M8).
- Stack canary: deliberate overflow detected 0.99 ms after the task got the CPU, message printed, system halted; no false alarm on the demo (M8).
- Stack use in the demo: tasks 61 bytes of 128, idle 37 bytes (M7/M8).
- Dashboard link: no frame before `T`, none after `t`; 190 frames in a row with consecutive switch numbers 10 ticks apart; each frame's
  tick, tasks and `PORTB` equal the simulator's own record of that switch; stack address, canary and the planted `task_exit` address
  right in every frame. A switch that sends a frame takes 3128 cycles (196 us) instead of 380; task stacks then reach 76 bytes (M9).

## Tested on the real UNO
Checked through the serial port: the PC read the board's output and compared every line and frame with what the program must send.

**23 Oct 2026 (9600-baud version, before the dashboard link):**
1. Uploading works with `avrdude` (the same command as `make flash`) and from the Arduino IDE, and every read-back of the
   flash matched. After the bootloader the program started each of the 6 times the board was reset with the serial port open.
2. In a 3-minute run all 1780 report lines were exactly as predicted. No `!STACK OVERFLOW`.
3. The deliberate overflow (`tests/fw/m8_overflow.c`) printed `!STACK OVERFLOW T2` and then stayed silent: the system halted.
   (The stack check has not changed since; the simulator's M8 overflow test passes with the current code.)

**29 Oct 2026 (current version, 250000 baud, Arduino IDE build uploaded with `build_firmware.py --upload COM5`):**
1. A 3-minute run with frames on: 17837 frames, none missing, every slice exactly 10 ticks, strict T1 -> T2 -> T3; in every frame
   the stack address and the planted `task_exit` address match the build and the canary is intact; all 53511 LED bits in the
   frames follow the blink rule; all 1782 report lines exactly as predicted; deepest stack use 65 of 128 bytes; frames used
   58 % of what 250000 baud can carry.
2. Without `T` (as with the Arduino IDE's Serial Monitor) the board sends only the text: no byte of 0x80 or more.
3. Clock: 0.058 % slow compared with the PC (about 2.1 s per hour), from the least-delayed frame of each second over 178 s.
   (23 Oct, from report lines: 0.061 % slow.)
4. The dashboard page was run against the real board for 30, 60 and 75 s: it got in sync about 2 s after connecting, every check
   on the page stayed clean, and it came back in sync by itself after "Restart board". In these runs the page ran in a test
   browser (headless Microsoft Edge) whose USB serial port was replaced by a small bridge to COM5 (Node.js `serialport`), because
   a test cannot click the browser's port chooser. So the page's own logic was tested with the real board; the browser's Web
   Serial connection itself was not part of these runs.

Still to check by eye: the LEDs on D13, D8 and D9 blinking at 250 / 500 / 1000 ms, and the D8/D9 wiring. The serial check
proves the tick and the pin states the program writes, not the LEDs themselves.
Not covered by anything: differences between simavr and the real silicon in interrupt latency or sleep wake-up, and electrical effects.

## Project layout
```
src/            kernel.c/.h  context.h  timer.c/.h  uart.c/.h  trace.c/.h  main.c   <- the firmware (507 lines of C incl. comments)
tests/          run_tests.py  simlib.py  fw/*.c (test firmwares)
tools/          simtest.c (simavr harness)  build_simtest.sh  build_simavr.sh  run_demo.py
docs/           M0..M9 step-by-step notes, ARCHITECTURE.md, LEARNING_GUIDE.md, VIVA_QA.md, REFERENCES.md, TEST_RESULTS.txt
Makefile
```
Start with `docs/LEARNING_GUIDE.md`.

## Scope
Round-Robin only. No priorities, mutexes, semaphores or `sleep()` (a task waits by watching the tick counter).
