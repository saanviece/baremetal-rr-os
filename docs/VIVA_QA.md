# Viva questions and honest answers (25)

Answers are short and based on this code. File references are where to point on the screen.

**1. What exactly did you build?**
A tiny preemptive kernel for the ATmega328P in about 340 lines of C plus inline assembly: a 1 ms timer tick, per-task stacks and
control blocks, a context switch in a naked interrupt handler, a Round-Robin scheduler with a 10 ms slice, an idle task, a stack
canary, and a UART print that is safe to call from tasks. No Arduino core, no RTOS, no HAL.

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
Measured: 16000.001 cycles average between ticks.

**11. What is the time slice and how is it implemented?**
`SLICE_TICKS` (10) ticks. `kernel_tick()` decrements `slice_left` and only calls the scheduler when it reaches 0 (or the running task is dead).
Verified: runs of exactly 10 ticks.

**12. What is the cost of the kernel?**
Measured in simavr: 331 cycles (20.7 us) for a tick without task change and 372 cycles (23.2 us) with one, plus 4 cycles of hardware
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
`uart_print` copies the whole line into a 64-byte ring buffer with interrupts off for a few dozen cycles, and the UDRE interrupt sends the bytes later.

**16. Isn't turning interrupts off in `uart_print` dangerous for the tick?**
Only if it lasts longer than 1 ms. A copy of at most 63 bytes takes well under 1 ms, so no tick is lost (one pending flag would otherwise
be missed). The test shows the tick average stays 16000 cycles.

**17. What is a critical section and where did you use one?**
Code with interrupts off so shared data can't change halfway: `kernel_ticks()`/`kernel_switches()` (multi-byte reads) and `uart_print()`.
I restore the previous SREG instead of calling `sei()`, so it is also right if called with interrupts already off.

**18. How does the stack overflow detection work, and what are its limits?**
Each stack starts with 8 bytes of `0xA5`. Every tick the kernel checks the running task: any guard byte changed, or the saved SP below the
guard, means panic (message on UART, LED blinks, system stops). Limits: it is after the fact (<= 1 ms), it can't stop the damage, a write
that jumps over the guard or writes `0xA5` is missed. No MPU on the AVR. Tested: caught 0.99 ms after the task got the CPU; no false alarm.

**19. How big does a task stack have to be?**
128 bytes is the default, but about 83 are usable by the task: 8 are guard and 37 must be free at any tick for the saved context.
The demo tasks used 49 bytes in total (measured with the `0xA5` fill pattern).

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
Everything runs in simavr only. Not yet tested: LEDs, USB-serial output, uploading with avrdude, behaviour after the Arduino bootloader, real
crystal timing, and real UART timing (simavr uses 11 bit-times per character, real 8N1 uses 10). The README lists the checks to do on the board.

**25. What would you do next?**
Add a blocking `sleep(ms)` so waiting tasks don't burn their slice, then semaphores/mutexes, then priorities. Profile and optimise the tick path (the stack check is the first suspect) and add a stack-usage report. These were deliberately left out of this project's scope.
