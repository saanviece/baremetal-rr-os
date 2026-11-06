# Viva questions and honest answers (32)

Answers are short and based on this code. File references are where to point on the screen.

**1. What exactly did you build?**
A tiny preemptive kernel for the ATmega328P in about 500 lines of C (with comments, demo and dashboard link included) plus inline
assembly: a 1 ms timer tick, per-task stacks and control blocks, a context switch in a naked interrupt handler, a Round-Robin
scheduler with a 10 ms slice, an idle task, a stack canary, and a UART print that is safe to call from tasks. On request it also
sends a frame per task switch to a web dashboard that shows what the real board is doing. No Arduino core, no RTOS, no HAL.

**2. What is a context switch?**
Saving the CPU state of the running task (PC, r0-r31, SREG, and SP in its TCB) and loading the saved state of another task so it
continues exactly where it stopped. See the ISR in `src/kernel.c` and `src/context.h`.

**3. What is saved, and why each part?**
PC (where it was), r0-r31 (its data in flight), SREG (flags that a pending branch may still need), SP (kept in the TCB; it locates all of
the rest). SRAM variables are not saved because each task's variables stay where they are and its stack is private.

**4. Why is the ISR `naked`?**
So the compiler adds no prologue/epilogue. I need to control exactly what is pushed and popped, and to change SP in the middle of the
handler, which compiler-generated code would not expect.

**5. Why does `r1` have to be zero, and what do you do about it?**
avr-gcc assumes r1 == 0 in all C code (it uses it as a zero constant, e.g. `adc r26, r1` in `kernel_tick`). The hardware doesn't know that.
In the ISR I push the task's r1, then `clr r1` before calling C, and pop the task's r1 back at the end. The fake start frame has r1 = 0.

**6. In which byte order is the return address on the AVR stack? How do you know?**
Low byte at the higher address (pushed first), high byte just below it; `ret`/`reti` pop the high byte first. I checked it in simavr's
source (`_avr_push_addr`) and proved it by running: the hand-made frame in `m4_launch` jumps into the task correctly, and the M4 test
compares every byte. Function addresses are *word* addresses.

**7. How does a brand-new task start if it never ran?**
`build_frame()` writes onto its stack what a paused task would have: the entry address, 33 zero bytes for r0/SREG/r1..r31, and below
the entry a return address for `task_exit`. Starting it is the same as resuming it: load SP, pop the zeros, `reti` into the function.

**8. What happens if a task function returns?**
It lands in `task_exit()` (planted as the return address), which marks it `TASK_DEAD`; the scheduler skips dead tasks. Tested in `m6_dead`.

**9. How does Round-Robin pick the next task?**
`pick_next()` walks the circular list starting after the task that last ran and takes the first `TASK_READY` one. If none is ready it
returns the idle task. Every ready task therefore gets an equal 10 ms turn in a fixed order. No priorities.

**10. Why is the tick 1 ms, and how did you derive OCR1A = 249?**
16 MHz / 64 = 250 kHz timer clock = 4 us per count; 1 ms = 250 counts; in CTC mode the timer counts 0..OCR1A so OCR1A = 250 - 1 = 249.
Measured in simavr: 16000.001 cycles on average over 1985 ticks, i.e. 1.00000 ms. On the real board the slices were exactly 10 ticks
in every one of 17837 frames.

**11. What is the time slice and how is it implemented?**
`SLICE_TICKS` (10) ticks. `kernel_tick()` decrements `slice_left` and only calls the scheduler when it reaches 0 (or the running task is dead).
Verified: runs of exactly 10 ticks.

**12. What is the cost of the kernel?**
Measured in simavr: 331 cycles (20.7 us) for a tick without task change and 380 cycles (23.8 us) with one, plus 4 cycles of hardware
interrupt entry. About 2.1 % of the CPU. The bare switch measured in M5 was 202 cycles; the rest comes from the stack check and slice logic added in M6 (I did not profile them separately).

**13. How did you prove registers survive a switch?**
`tests/fw/m8_regs.c`: three assembly tasks keep r0..r31 as 16 mirror pairs plus the C/T flags, increment everything every loop and compare.
They ran through 1999 and 299 real switches with zero failures. To show the test is able to fail, I built the kernel with a deliberate bug
(`FAULT_R7`, `FAULT_CARRY`) and the same test caught both.

**14. Why do you toggle LEDs with `PINB = mask` instead of `PORTB ^= mask`?**
`^=` is read-modify-write in three instructions; if the tick switches tasks in the middle, the other task's change to PORTB is overwritten.
Writing a 1 to PINx toggles PORTx in one instruction, so it cannot be split (datasheet I/O-port chapter).

**15. Why can't two tasks just call `uart_puts` at the same time?**
It sends one character at a time and waits between them, so a switch in the middle of a line lets another task's characters appear inside it.
`uart_print` copies the whole line into a 256-byte ring buffer with interrupts off (about 1 070 cycles = 67 us for a 57-character
report line, counted from the instruction listing), and the UDRE interrupt sends the bytes later, one at a time.

**16. Isn't turning interrupts off in `uart_print` dangerous for the tick?**
Only if it lasts longer than 1 ms: then a second tick could come while the first one is still pending, and one would be lost. The longest
possible copy (255 characters at about 18 cycles each, plus about 45) takes about 4 650 cycles = 0.29 ms, so no tick is lost; a tick that
comes during the copy is only a little late. In the demo the copy happens right after the task got the CPU, well before the next tick:
in a 6 s simulator run the worst tick lateness was 24 cycles (1.5 us), from the UART interrupt. The tick average stays 16000 cycles.
The dashboard frame (Q29) keeps interrupts off longer, 196 us, but it runs inside the tick itself, so it cannot delay that tick.

**17. What is a critical section and where did you use one?**
Code with interrupts off so shared data can't change halfway: `kernel_ticks()`/`kernel_switches()` (multi-byte reads) and `uart_print()`.
I restore the previous SREG instead of calling `sei()`, so it is also right if called with interrupts already off.

**18. How does the stack overflow detection work, and what are its limits?**
Each stack starts with 8 bytes of `0xA5`. Every tick the kernel checks the running task: any guard byte changed, or the saved SP below the
guard, means panic (message on UART, LED blinks, system stops). Limits: it is after the fact (<= 1 ms), it can't stop the damage, a write
that jumps over the guard or writes `0xA5` is missed. No MPU on the AVR. Tested: caught 0.99 ms after the task got the CPU; no false alarm.

**19. How big does a task stack have to be?**
128 bytes is the default, but only about 77 are usable by the task itself: 8 are guard, and at every tick the interrupt needs
43 bytes on the task's stack (35 for the saved context, 2 for the return address of `call kernel_tick`, 6 that `kernel_tick` itself
uses). With the dashboard's frames on, a switch needs 15 more (about 62 left). The demo tasks used 61 bytes in total (76 with frames;
65 on the real board's Arduino IDE build), measured with the `0xA5` fill pattern, the context and the line formatting included.
(My first notes said 83 usable bytes: they forgot `kernel_tick`'s own 6 bytes.)

**20. What is the idle task for?**
So the scheduler never has "nobody to run" (all tasks dead, or none created). It runs `sleep` in a loop; the CPU halts until the next tick.
It sits outside the ring, so it never steals a Round-Robin turn.

**21. How does a task wait without `sleep()`?**
It busy-loops comparing `kernel_ticks()` with its last toggle time, so it uses its whole slice. A real RTOS would block it; that was out of scope
(and is the natural next extension). Consequence: LED toggles can be up to ~20 ms late (average exact, because I add the period to the old
time, not to "now").

**22. Is it really preemptive?**
Yes: tasks never call the kernel to give up the CPU; the Timer1 interrupt takes it away every 10 ms. Test tasks are `for(;;) count++` loops that
never yield, and still rotate.

**23. What did you take from other sources?**
Only ideas and datasheet facts, listed in `docs/REFERENCES.md`. The code is my own. I did not copy any tutorial code.

**24. What have you NOT verified on real hardware?**
On the UNO I checked uploading (avrdude and the Arduino IDE), start-up after the bootloader, the serial output, the clock and the
deliberate stack overflow (23 Oct 2026), and the dashboard link (29 Oct 2026): in a 3-minute run 17837 frames, none missing, every
slice exactly 10 ticks, strict rotation, canaries intact, all 53511 LED bits and all 1782 report lines as predicted. The clock is
0.058 % slow against the PC (about 2.1 s per hour). The LEDs themselves can only be checked by eye: the serial check proves the tick
and the pin states the program writes, not the wiring. Not covered at all: differences between simavr and the real chip in interrupt
latency or sleep wake-up. All cycle counts (tick cost, frame cost, lateness) were measured in the simulator, not on the chip.

**25. What would you do next?**
Add a blocking `sleep(ms)` so waiting tasks don't burn their slice, then semaphores/mutexes, then priorities. Profile and optimise the tick path (the stack check is the first suspect) and add a stack-usage report. These were deliberately left out of this project's scope.

**26. What do the numbers in the serial monitor mean, and how do you know they are right?**
`0.270 s | T1 has the CPU | turn 10 | D13 LED on`: at 270 ms after the kernel started, T1 began its 10th turn, and its LED was on.
Each task gets a turn every 30 ms (3 tasks x 10 ms), so turn 10 of T1, T2, T3 starts at 0.270, 0.280 and 0.290 s; the lines being
10 ms apart is the slice you can see. The M7 test checks every line against these predicted numbers and checks every printed LED state
against the real pin in the simulator.

**27. Why is the start-up text `__flash`, and why does each task have its own line buffer?**
`__flash` keeps the 303-byte text in flash; a normal string is copied into the 2 KB of RAM at reset (Harvard architecture), and
`uart_puts_flash` reads it with the `LPM` instruction. Own buffers: a task can be preempted while it is building its line, so with one
shared buffer the next task would overwrite it halfway.

**28. How does your dashboard know what the board is doing? Isn't it a simulation?**
No. The dashboard (RR-OS Scope) opens only when the board is connected and in sync, and everything on it comes from the board. When
it sends the letter `T`, `kernel_tick()` calls `trace_switch()` (`src/trace.c`) at every task switch, which queues a 141-byte frame:
tick, switch number, paused task, next task, `PORTB`, the paused task's saved SP and its whole 128-byte stack. The last 35 bytes of
that stack are what the interrupt just saved, so the page shows the real registers, flags and the PC where the task stopped, and
with the build's symbol table the C source line. (A separate simulator page exists for studying without a board.)

**29. Doesn't sending all that disturb the OS?**
A little, and I measured how much (simavr): a switch with a frame takes 3128 cycles (196 us) instead of 380, with interrupts off.
That is far less than one tick, so no tick is lost, and it adds about 1.7 % CPU time. The stacks go 15 bytes deeper during such
a switch (76 bytes used instead of 61). `trace_switch` never waits: if the send buffer has no room for the whole frame it skips the
frame, and the dashboard sees the gap in the switch numbers. In all tests no frame was skipped. Without `T` the only cost is the
`if (trace_on)` check at each switch (a switch went from 372 to 380 cycles when I added it).

**30. Why 250000 baud?**
The frames need about 14.5 kB/s; 9600 baud carries only 960 bytes/s. 250000 divides 16 MHz exactly: UBRR = 16 000 000 / (16 x 250000)
- 1 = 3, so the error is 0 % (9600 had +0.16 %). The frames use about 58 % of the line. The Arduino IDE's Serial Monitor offers 250000.

**31. How does the page tell frames from text when both come on the same wire?**
Every text character is below 0x80 (plain ASCII). A frame starts with 0x80 + task id, so a byte with bit 7 set starts a frame of a
known length (141 bytes). The page also checks the frame head (task ids, the stack pointer inside the stack); a head that cannot be
real is treated as noise, one byte is dropped and reading goes on, so it finds the next real frame. Bytes left in the USB chip from
before the reset are ignored until the start-up text arrives. And the bytes of a frame never land inside a line: both are copied
into the send buffer whole, with interrupts off. (The one exception is the crash message after a stack overflow, which is sent
directly; the page then drops the frame that was cut off.)

**32. How do you know the dashboard shows the truth?**
Three ways. (1) The M9 simulator test compares every frame with what the simulator itself recorded at that switch: tick, switch
number, both tasks and `PORTB` all equal. (2) On the real board every frame is checked as it arrives: 10-tick slices, strict
rotation, canaries, the stack address and the planted `task_exit` address (which must match the build), and the LED pins against
the blink rule; in 3 minutes all 17837 frames passed. (3) The text lines, which the board prints independently, still come out
exactly as predicted while the frames are on.
