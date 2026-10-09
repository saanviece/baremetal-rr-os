# M5 - The context switch (naked ISR in inline assembly)

**Goal:** when the 1 ms timer interrupt fires, freeze the running task exactly as it is, and later let
a different task continue exactly where *it* was frozen.

## What is a "context"?
Everything the CPU needs to carry on a program as if nothing happened:

| Part | Where it lives | Why it matters |
|------|----------------|----------------|
| **PC** (program counter) | pushed automatically by the interrupt hardware | where the task was in its code |
| **r0 - r31** (32 general registers) | I push them | local variables and half-finished calculations |
| **SREG** (status register: flags Z, C, N, V, S, H, T, I) | I save it through r0 | a compare can be done in the task and the matching `breq` happen after the switch; the flags must survive |
| **SP** (stack pointer) | stored in the task's TCB | it *is* the address of everything above |

Memory (SRAM) needs no saving: each task's variables stay where they are, and its stack is private.

## Why the ISR is "naked"
A normal `ISR()` makes the compiler add its own save/restore code (only the registers it uses) and a `reti`.
For a context switch I need to control **exactly** what is pushed and popped, because the same bytes are
read back later (and read back by a *different* ISR run for a *different* task). `ISR_NAKED` tells avr-gcc:
"add nothing, I write the whole function in assembly".

## The ISR, line by line (see `src/kernel.c`, `src/context.h`)

```
-- hardware, before my first instruction: push PC (low byte, then high byte), clear I-bit, jump to vector --

SAVE_CONTEXT
  push r0            r0 is my scratch register, so save it first
  in   r0, SREG      copy the flags into r0 (the `in` instruction itself does not change flags)
  push r0            save the flags (on the stack, right above the old r0)
  push r1            save r1 ...
  clr  r1            ... and set r1 = 0, because the C function I call next assumes r1 == 0
  push r2 ... push r31   save the other 30 registers (r2 first, r31 last)
```
After this the stack holds, from high to low address: PC(2) r0 SREG r1 r2 ... r31 = 35 bytes. This is exactly the
shape that M4 built by hand for a new task.

```
SAVE_SP_TO_CURRENT
  lds r30, rr_current / lds r31, rr_current+1    Z := address of the running task's TCB
  in  r24, SPL / in r25, SPH                     read the stack pointer
  st  Z, r24 / std Z+1, r25                      tcb->sp := SP     (sp is the first field -> offset 0)
```
Now the old task is completely frozen: its whole state is "the bytes above tcb->sp".

```
call kernel_tick        ordinary C code: counts the tick and updates rr_current to the next task
```
`kernel_tick` may use r18-r27, r30, r31 and SREG freely - everything is already saved. It must find r1 = 0 (done).

```
LOAD_SP_FROM_CURRENT
  Z := rr_current (the NEW task's TCB) ; SP := tcb->sp
```
This single step is the actual switch: from here on, every `pop` reads the *new* task's stack.

```
RESTORE_CONTEXT          the mirror image, in reverse order
  pop r31 ... pop r2     (30 pops)
  pop r1
  pop r0                 this is the saved SREG value ...
  out SREG, r0           ... put the flags back
  pop r0                 the real r0
reti                     pop PC (high byte first, then low byte) and set the I-bit -> the new task continues
```

## Rules that make it correct
* **Push order = reverse of pop order**, and the fake frame of M4 uses the same order.
* r1 is saved *before* it is cleared; the task gets its own r1 back at the end.
* Interrupts are off during the whole ISR (the hardware clears the I-bit), so the ISR cannot be interrupted halfway.
* The saved SREG has the I-bit = 0 (the hardware cleared it on entry). That is fine: `reti` sets the I-bit again.
  Tasks only ever get interrupted while I = 1, so "I = 1" is the correct state for every resumed task.
* Writing SP takes two instructions (`out SPH`, `out SPL`); an interrupt between them would be fatal, but
  interrupts are off here.

## Starting the very first task
`kernel_start()` does only the second half of the ISR: `SP := first task's sp`, `RESTORE_CONTEXT`, `reti`.
The old `main()` stack is simply abandoned.

## What M5's tick function does (temporary)
`kernel_tick()` counts the tick and moves to the *next task in the circular list on every tick*. M6 replaces this
with the real scheduler (time slice of 10 ticks, dead tasks skipped, idle task, stack canary).

## How M5 is tested
Three tasks each just count in a variable. In simavr: every interrupt must move `rr_current` to the next TCB
in the ring (1 -> 2 -> 3 -> 1 ...), all three counters must keep growing, and I measure the cycles the
interrupt costs.
