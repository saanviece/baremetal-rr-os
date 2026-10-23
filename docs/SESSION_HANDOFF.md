# Session handoff (for a new Claude Code session)

Read this first. It summarises everything decided and done in the first Claude Code session (Oct 2026).

## Who / what
- Student: Saanvi Chauhan, ECE 7th semester. Mentor: Dr. Divya Prakash.
- Project: "A Bare-Metal Round-Robin OS on the ATmega328P" (Arduino UNO, 16 MHz). Minor project.
- Saanvi must be able to explain and defend every line. Explain in plain, simple language. Never claim something works
  unless it was run; show real output. Keep text she presents simple and not "AI-sounding".

## Hard constraints (from her slides, still apply)
- Bare-metal register-level C with avr-gcc. NO Arduino core, NO FreeRTOS/any RTOS, NO HAL.
- Only avr-libc headers `avr/io.h`, `avr/interrupt.h`. Small inline asm OK. No `_delay_ms` (use the tick).
- Round-Robin only. No priorities/mutexes/semaphores/sleep() unless she asks.
- Code small (target < ~400 lines of C). List every resource in docs/REFERENCES.md.

## Status: milestones M0-M8 all DONE and committed (one commit each), docs done
- 64 automated checks pass in simavr (`docs/TEST_RESULTS.txt`). NOT yet tested on a real UNO (she has no board yet).
- Key results: tick 16000.001 cycles avg (1.000 ms); slices exactly 10 ticks, order T1,T2,T3; tick cost 331 cycles
  (no switch) / 372 cycles (switch) +4 hw entry, ~2.1 % CPU; registers r0-r31 + flags C,T survive 1999 + 299 switches,
  and the test catches deliberate kernel bugs (FAULT_R7, FAULT_CARRY); deliberate stack overflow caught in 0.99 ms;
  demo stack use 49/128 bytes per task.
- Demo (`src/main.c`): T1 PB5/D13 250 ms, T2 PB0/D8 500 ms, T3 PB1/D9 1000 ms; UART 9600 prints T1 T2 T3 in rotation.

## Design decisions to remember (tell her if anything changes)
- Timer1 CTC, prescaler 64, OCR1A = 249 -> 1 ms tick. SLICE_TICKS = 10 (overridable with -DSLICE_TICKS=n).
- TCB = {sp (offset 0, asm relies on it), state, id, next (circular list), stack_low}. 8 bytes.
- Deviation from brief: canary is 8 x 0xA5 bytes at the bottom of each stack (TCB holds `stack_low` pointer), not inside the TCB.
  Check every tick: canary bytes intact AND saved SP >= stack_low + 8. Panic prints "!STACK OVERFLOW Tn", blinks LED, halts.
- Usable stack per task ~83 bytes (128 - 8 guard - 37 context frame).
- Context = PC (hw) + r0, SREG, r1 (then clr r1), r2..r31 pushed in naked ISR; restore is exact reverse; `reti`.
  Fake start frame: [exit addr lo,hi][entry addr lo,hi][33 zero bytes]; task returning lands in task_exit -> DEAD.
- Idle task = tcbs[0], outside the ring, runs `sleep` loop only when no user task is READY.
- Task-safe print `uart_print`: whole line into 64-byte ring buffer under short cli, drained by USART_UDRE ISR.
  Polled `uart_puts` only before kernel start / in panic.
- LED toggling with `PINB = mask` (atomic), not `PORTB ^=`.
- timer_start clears TIFR1, uart_init clears UCSR0A (defensive vs bootloader leftovers; untested on hardware).

## Environment (this Windows PC)
- Project: C:\Users\SAANVI CHAUHAN\Documents\baremetal-rr-os (git repo, branch main).
- avr-gcc 16.1.0, avr-libc 2.3.2, make 4.2.1, avrdude 8.2 via winget `ZakKemble.avr-gcc` (bin dir under
  %LOCALAPPDATA%\Microsoft\WinGet\Packages\ZakKemble.avr-gcc_...\avr-gcc-16.1.0-x64-windows\bin). Open a new terminal if `make` is not found.
- MSYS2 at C:\msys64. simavr built from source at C:\simavr_src\simavr (commit d6aed53); needed pkgconf or it builds
  without ELF support. Rebuild steps: tools/build_simavr.sh. Harness: tools/simtest.c -> tools/bin/simtest.exe
  (rebuild with `sh tools/build_simtest.sh` in an MSYS2 UCRT64 shell). simavr and tools/bin are NOT in git.
- Python 3.13 runs the tests.

## Commands
- `make` -> build/main.hex. `make flash PORT=COM3` -> avrdude -c arduino -P COM3 -b 115200 -p m328p.
- `python tools/run_demo.py 6` -> runs the OS in the simulator, prints serial lines + LED changes (no board needed).
- `python tests/run_tests.py [m1..m8]` -> all checks.
- Gotchas: simavr UART counts 11 bits/char (real 8N1 = 10). When editing files from Python on Windows, write with
  newline='\n'. Prefer the Edit tool for strings containing \n (shell/python escaping mangled them before).

## Docs in the repo
README.md, docs/M0..M8_*.md (one per milestone), ARCHITECTURE.md (memory map, TCB, stack layout, switch timeline),
LEARNING_GUIDE.md, VIVA_QA.md (25 Q&A), M8_tests.md, REFERENCES.md (datasheet PDF was NOT read directly - too large;
facts confirmed via Microchip online docs, avr-libc header, simavr source; Kevin Cuzner tutorial NOT read), TEST_RESULTS.txt.

## Non-code things already done in chat
- Wrote a ChatGPT prompt for a simple 6-7 slide progress PPT (based on her intro PPT + two screenshots: run_demo output
  and "RESULT: ALL PASSED").
- Drafted a short progress email to her mentor (subject: "Progress Update - Minor Project (Bare-Metal Round-Robin OS on ATmega328P)").

## Next steps
1. She studies the code: context.h, kernel.c (ISR, build_frame, pick_next), main.c; try SLICE_TICKS 10 -> 50 in run_demo.
2. Viva practice with docs/VIVA_QA.md (quiz her one question at a time if asked).
3. When she has a UNO: flash, then run the 6 checks in README "NOT tested on real hardware yet". Walk her through step by step.
4. Final report + live demonstration.
