# Learning guide - how to read this project

Goal: be able to explain every line. Read in this order; each step is small and has its own note and test.

## Reading order

| Step | Read | Time | You should be able to say |
|------|------|------|---------------------------|
| 1 | `docs/M1_blink.md`, `tests/fw/m1_blink.c` | 10 min | what DDR/PORT/PIN do; why `PINB = x` toggles |
| 2 | `src/uart.c` (top half), `docs/M2_uart.md` | 15 min | how a byte leaves the chip; where 103 comes from |
| 3 | `src/timer.c`, `docs/M3_tick.md`, `tests/fw/m3_tick.c` | 20 min | what an interrupt does; why OCR1A = 249 |
| 4 | `src/kernel.h` (the TCB), `build_frame()` in `src/kernel.c`, `docs/M4_tcb_stack.md` | 30 min | what a task looks like when it is paused |
| 5 | `src/context.h`, the ISR in `src/kernel.c`, `docs/M5_context_switch.md` | 45 min | every push/pop and why in that order |
| 6 | `pick_next()`, `kernel_tick()`, `stack_ok()`, `docs/M6_scheduler.md` | 30 min | how Round-Robin chooses; idle; canary |
| 7 | `uart_print()` + its ISR, `src/main.c`, `docs/M7_demo.md` | 30 min | why printing from tasks needs a buffer |
| 8 | `docs/ARCHITECTURE.md` (timeline), `docs/M8_tests.md`, `tests/fw/m8_regs.c` | 45 min | how you know it works |
| 9 | `docs/VIVA_QA.md` | 30 min | answers in your own words |

Tip: open `build/main.lst` (`make disasm`) next to `__vector_11` and tick off each instruction against `context.h`.

## Concepts in simple words

**Register.** A tiny, very fast storage box *inside* the CPU. The AVR has 32 of them (r0-r31). All arithmetic happens in them.
Two other meanings of "register": the *I/O registers* (`PORTB`, `UDR0` ...) are memory addresses wired to hardware; writing them
operates the hardware.

**Program counter (PC).** The address of the next instruction. Saving the PC is saving "where I was".

**Stack.** A pile of bytes in SRAM used last-in-first-out. `push` puts a byte on it, `pop` takes the latest one off.
Function calls put their return address on it; so do interrupts. On AVR it grows towards *lower* addresses.

**Stack pointer (SP).** A 16-bit register holding the address of the next free stack byte. `push` = store at SP, then SP-1.
`pop` = SP+1, then read. Each task has its own stack, so a task switch is mostly "change SP".

**SREG (status register).** 8 flag bits set by instructions: Z (zero), C (carry), N (negative), V (overflow), S (sign), H (half-carry),
T (a user bit) and **I** (global interrupt enable). A comparison sets flags and a later branch reads them, so a switch between the two
must not lose them. That is why SREG is part of the context.

**Interrupt.** The hardware pauses the program, pushes the PC, clears the I-bit, and jumps to a fixed address (the *vector*). The code
there is the **ISR** (interrupt service routine). `reti` goes back and re-enables interrupts.

**Interrupt vector table.** The first words of flash: one `jmp` per interrupt source. `ISR(TIMER1_COMPA_vect)` makes the compiler
put a jump to my function in slot 11.

**Context.** Everything that defines "where a program is": the PC, the 32 registers, SREG and (through the stack) its local data.
Save it = freeze the program. Restore it = it continues as if nothing happened.

**Context switch.** Save the context of task A, load the context of task B. Because B's context was saved the same way earlier,
B continues exactly where it stopped.

**Preemptive vs cooperative.** Preemptive = the *timer* takes the CPU away from a task whether it likes it or not (this project).
Cooperative = tasks must volunteer to give it up.

**Time slice / tick.** The tick is the 1 ms timer interrupt. The slice is how many ticks a task may keep the CPU (10).

**Round-Robin.** Fixed turns in a circle, no priorities.

**TCB (task control block).** The small record the kernel keeps per task (saved SP, state, link to the next task, stack info).

**Why `r1 = 0`.** avr-gcc generates code that assumes register r1 always contains zero (a ready-made constant for
adding carries, clearing, 16-bit multiplies). The compiler enforces it by itself at normal function boundaries. My assembly
interrupt code must do it by hand: save the task's r1 (it may be non-zero inside inline assembly), `clr r1` before calling
any C function, and give the task its own r1 back at the end. A new task's fake frame starts with r1 = 0. In `kernel_tick`
the compiler really does use it: `adc r26, r1`.

**Naked ISR.** An ISR for which the compiler writes no entry/exit code, so the assembly has total control.

**Critical section.** A few instructions during which interrupts are off so a shared variable cannot be changed halfway.
Save SREG, `cli`, work, restore SREG.

**Race condition.** A bug that only appears when the interrupt happens at an unlucky instant, e.g. `PORTB ^= x` interrupted between its
read and write.

**Volatile.** Tells the compiler "this variable may change behind your back (interrupt, hardware)": do not cache it in a register.

**Canary.** Known bytes at the end of a stack. If they change, the stack overflowed into them.

**Harvard architecture.** Code in flash and data in SRAM are separate address spaces. That is why string literals are copied to SRAM
at start-up (the 66 bytes of `.data`) and why function pointers are *word* addresses in flash.

**Why avr-gcc function pointers are "word addresses".** AVR instructions are 16 bits, so flash is addressed per word. The
return address pushed on the stack is a word address, so `(uint16_t)fn` is exactly what has to be planted for `reti` to pop.

## Exercises (to be sure you own it)
1. Change `SLICE_TICKS` to 50 and predict the T1/T2/T3 print rhythm; then run `python tests/run_tests.py m6` and the M7 test.
2. In `context.h` swap two pops. Predict what breaks, then run `tests/run_tests.py m8`.
3. Make `build_frame` put `0x55` in r5's slot and write a task that reads r5 at its start.
4. Add a fourth LED task (you have `MAX_TASKS = 4`).
