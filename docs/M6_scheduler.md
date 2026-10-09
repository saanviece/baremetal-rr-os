# M6 - The Round-Robin scheduler

**Goal:** replace M5's "switch on every tick" by a proper Round-Robin policy:
each task runs for a **time slice** (default 10 ticks = 10 ms), then the next *ready* task in the ring gets the CPU.

## Round-Robin in plain words
Imagine people sitting in a circle passing one microphone. Everyone gets the microphone for the same fixed time,
then passes it to the neighbour on the left. Nobody is more important than anybody else (that would be
*priorities*, which this project deliberately does not have).

* the circle = the **circular linked list** of TCBs (`tcb->next`, the last one points to the first);
* the fixed time = the **time slice** (`SLICE_TICKS` ticks of 1 ms);
* "passing" = `kernel_tick()` changing `rr_current`, then the assembly code doing the context switch of M5.

## What `kernel_tick()` does now (runs every 1 ms, inside the ISR, interrupts off)
1. `ticks++` - the system clock.
2. **Stack check**: look at the running task's 8 canary bytes and at its saved SP; if something is wrong, `kernel_panic()`.
3. `--slice_left`: if the slice is not over and the task is still READY, return (no switch, the same task continues).
4. Otherwise `pick_next()`: walk around the ring starting after the task that last had its turn
   and take the first one that is `TASK_READY`. If none is, take the **idle task**.
5. If that is a different task, set `rr_current` to it and count a switch. (The assembly after `kernel_tick`
   then loads that task's SP and restores its registers.)

## Idle-safe design
* **Idle task** (`tcbs[0]`, id 0). It is *not* in the ring, so it never takes a normal turn. It runs only when
  no user task is READY (for example all of them returned, or the user created none). Its body is the
  `sleep` instruction in a loop: the CPU halts until the next interrupt, which saves power. The tick wakes it up.
  This means the scheduler can never end up with "nobody to run".
* **A task that returns** (falls off the end of its function) lands in `task_exit()`, which marks it `TASK_DEAD`.
  The scheduler skips dead tasks. So returning is safe, not a crash.
* **No `sleep()`/blocking API**: out of scope for this project, so there is no "waiting" state; a task that wants to
  wait busy-loops on `kernel_ticks()` and simply uses up its own slice.

## Stack overflow detection (canary)
Every stack starts with 8 guard bytes `0xA5` at its lowest addresses (see M4). Stacks grow *downwards*, so a
task that uses too much stack tramples these bytes first. Every tick the kernel checks the **running** task
(only the running task can be the one growing its stack):
* any of the 8 bytes `!= 0xA5`  -> overflow, or
* saved SP below `stack_low + 8` -> the stack pointer itself crossed the guard.

On failure `kernel_panic()` stops everything: interrupts off, prints `!STACK OVERFLOW Tn` over the UART,
and blinks the on-board LED fast forever.
**Limits (be honest in the viva):** it detects an overflow *after* it happened, at the next tick (<= 1 ms later),
it does not prevent it, and a write that skips over the guard bytes or writes exactly `0xA5` is not noticed.
The AVR has no memory protection unit, so this is the cheapest practical check.

## Code
* `kernel_init()` also creates the idle task.
* `kernel_tick()`, `pick_next()`, `kernel_panic()` in `src/kernel.c`.
* `kernel_start()` begins with the first user task, or with idle when there are none.

## How M6 is tested
* **Rotation and slice length**: sampling `rr_current` at every tick must show runs of exactly 10 ticks in the order 1,2,3,1,2,3...
* **Dead tasks are skipped**: a task that returns gets no further slices; the others keep alternating.
* **Idle**: when all tasks have returned (and when none were created) `rr_current` becomes the idle task and the ticks keep coming.
