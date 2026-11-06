# Session handoff (for a new Claude Code session)

Read this first. It summarises everything decided and done in the Claude Code sessions so far (Oct 2026).

## Who / what
- Student: Saanvi Chauhan, ECE 7th semester. Mentor: Dr. Divya Prakash.
- Project: "A Bare-Metal Round-Robin OS on the ATmega328P" (Arduino UNO, 16 MHz). Minor project.
- Saanvi must be able to explain and defend every line. Explain in plain, simple language. Never claim something works
  unless it was run; show real output. Keep text she presents simple and not "AI-sounding".

## Hard constraints (from her slides, still apply)
- Bare-metal register-level C with avr-gcc. NO Arduino core, NO FreeRTOS/any RTOS, NO HAL.
- Only avr-libc headers `avr/io.h`, `avr/interrupt.h`. Small inline asm OK. No `_delay_ms` (use the tick).
- Round-Robin only. No priorities/mutexes/semaphores/sleep() unless she asks.
- Code small (target was < ~400 lines of C; now 507 in the five .c files incl. comments and the dashboard link). List every
  resource in docs/REFERENCES.md.

## Status
- Milestones M0-M8 DONE and committed (one commit each), docs done.
- Session 2 (23 Oct) and session 3 (28-29 Oct) are NOT committed (she has not asked). Session 2: plain-language report lines.
  Session 3: the dashboard link (M9) and 250000 baud, because she asked for a dashboard that "works only when the Arduino is
  connected and synced" and "shows what is happening on the board itself".
- 76 automated checks pass (`docs/TEST_RESULTS.txt`, groups M1-M9).
- Real UNO (COM5, genuine Arduino, USB 2341:0043): 23 Oct 9600-baud tests (see README); 29 Oct with the current build: 3-minute run,
  17837 frames none missing, slices exactly 10 ticks, rotation strict, canaries intact, all 53511 LED bits and 1782 report lines
  as predicted, deepest stack 65/128 (IDE build), clock 0.058 % slow. Still open: LEDs on D13/D8/D9 checked by eye.
- Key simulator results (`make` build): tick 16000.001 cycles avg over 1985 ticks; worst tick 24 cycles late (21 with frames,
  separate 6 s run); tick cost 331 / 380 cycles (+4 hw entry), ~2.1 % CPU; with a frame a switch costs 3128 cycles (196 us);
  stacks 61 bytes (76 with frames); registers survive 1999 + 299 switches, mutation test catches FAULT_R7/FAULT_CARRY;
  overflow caught in 0.99 ms.
- Doc correction made in session 3: usable stack per task is ~77 bytes (128 - 8 guard - 43 at every tick: 35 context + 2 call
  + 6 kernel_tick's own), ~62 with frames on; the old docs said 83 (forgot kernel_tick's 6 bytes).

## The dashboard link (src/trace.c, docs/M9_dashboard_link.md)
- The PC sends 'T' -> `trace_on = 1` (USART_RX ISR); 't' -> 0; reset -> 0. So the Arduino IDE's Serial Monitor sees only text.
- At every switch (`kernel_tick`, interrupts off) `trace_switch(prev, next, ticks, switches)` queues 141 bytes via `uart_queue`:
  [0x80|to][tick u32][switches u16][from][PORTB][sp u16][stack_low u16][128 stack bytes]. Never waits: no room -> frame skipped.
- `head[13]` is static (not on the paused task's stack). `uart_queue` works on a local copy of tx_head (cost 3684 -> 3128 cycles).
- 250000 baud: UBRR = 3, exact. Frames use ~58 % of the line (14.5 kB/s on the board).

## Demo output (src/main.c, unchanged since session 2)
```
    0.270 s | T1 has the CPU | turn     10 | D13 LED on
```
Task i (0..2) gets turn k at 30*(k-1) + 10*i ms; LED on iff floor((ms - 10*i) / half_period) is odd (half = 250/500/1000).
LED state at a switch tick t (for frames): task i toggles at due = 10*i + k*half, but only during its own slice
[30m+10i, 30m+10i+10): when = max(due, start of its first slice ending after due); count toggles with when < t.

## Design decisions to remember (tell her if anything changes)
- Timer1 CTC, prescaler 64, OCR1A = 249 -> 1 ms tick. SLICE_TICKS = 10 (overridable with -DSLICE_TICKS=n).
- TCB = {sp (offset 0, asm relies on it), state, id, next (circular list), stack_low}. 8 bytes.
- Deviation from brief: canary is 8 x 0xA5 bytes at the bottom of each stack (TCB holds `stack_low` pointer), not inside the TCB.
- Context = PC (hw) + r0, SREG, r1 (then clr r1), r2..r31 pushed in naked ISR; restore is exact reverse; `reti`.
  Fake start frame: [exit addr lo,hi][entry addr lo,hi][33 zero bytes]; task returning lands in task_exit -> DEAD.
- Idle task = tcbs[0], outside the ring, `sleep` loop only when no user task is READY.
- `uart_print`: whole line into a 256-byte ring buffer under cli via `uart_queue` (~18 cycles/char + ~45, from the listing),
  drained by USART_UDRE ISR. Polled `uart_puts`/`uart_puts_flash` only before the kernel starts / in panic.
- LED toggling with `PINB = mask` (atomic). timer_start clears TIFR1, uart_init clears UCSR0A.
- simtest: `-r <ms> <byte>` types a byte into the UART receiver; simavr's text echo is switched off (UART flag STDIO cleared).

## Environment (this Windows PC)
- Repo (master copy): C:\Users\SAANVI CHAUHAN\Documents\baremetal-rr-os (git, branch main).
- Her main workspace: C:\Users\SAANVI CHAUHAN\Desktop\minor project
  - `round_robin\` = Arduino IDE sketch: byte-identical COPY of src/*.c/*.h (incl. trace.c/h) + comment-only round_robin.ino.
    After changing src/, copy the files there again.
  - `visualizer\` = RR-OS Scope. `index.html` = the LIVE dashboard (board only; gate until in sync; Web Serial at 250000;
    sends 'T'; checks every frame; clock from the least-delayed frame per second). `simulator.html` = the old emulator page
    (avr8js, step-through of the ISR), live panel removed. `python build_firmware.py --upload COM5` builds the sketch with the IDE's
    arduino-cli, writes firmware.js (symbols, disassembly, source lines) and uploads the same hex (verified). The build is
    reproducible (same hex). `tests\` holds the page tests (see visualizer\README.md). "Open RR-OS Scope.cmd" opens Edge.
  - `PROMPT_FOR_CLAUDE.md` = a briefing she pastes into claude.ai for her report / questions (updated in session 3).
- avr-gcc 16.1.0, avr-libc 2.3.2, make 4.2.1, avrdude 8.2 via winget `ZakKemble.avr-gcc` (on PATH in Git Bash).
- Arduino IDE 2 with core arduino:avr 1.8.6 (avr-gcc 7.3 + LTO); arduino-cli NOT on PATH:
  %LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe. UNO on COM5.
- MSYS2 at C:\msys64. simavr at C:\simavr_src\simavr (commit d6aed53). Rebuild the harness with
  `MSYSTEM=UCRT64 CHERE_INVOKING=1 /c/msys64/usr/bin/bash.exe -lc 'cd <repo> && sh tools/build_simtest.sh'`. Python 3.13, Node 24.

## Commands
- `make` -> build/main.hex. `make flash PORT=COM5`.
- `python tests/run_tests.py [m1..m9]` -> all checks. `python tools/run_demo.py 6` -> the OS in the simulator, no board needed.
- Gotchas: the Arduino IDE's Serial Monitor holds COM5; close it before uploading or connecting the dashboard. After an upload or a
  closed port, the USB chip may hand over a few hundred stale bytes on the next open (ignored until the start-up text).
  simavr UART counts 11 bits/char (real 8N1 = 10). Write files from Python with newline='\n'.

## Docs in the repo
README.md, docs/M0..M9_*.md, ARCHITECTURE.md, LEARNING_GUIDE.md, VIVA_QA.md (32 Q&A), M8_tests.md, REFERENCES.md,
TEST_RESULTS.txt.

## Next steps
1. She checks the LEDs by eye, tries the live dashboard in Edge with the real port chooser (not yet done by a person), and decides
   whether to commit sessions 2 + 3.
2. She studies trace.c and the dashboard (docs/M9, VIVA Q28-32), then the core as before (context.h, kernel.c, main.c).
3. Final report + live demonstration (board + RR-OS Scope live dashboard).
