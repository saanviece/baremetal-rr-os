# M4 - Task Control Block and the fake starting context

**Goal:** give every task (a) its own private stack and (b) a small record describing it (the TCB),
and prepare each stack so that the task can be *started* by the same code that *resumes* a task.

## The trick, in one sentence
A task that is paused looks like this: "all its registers are saved on its own stack and the stack
pointer is remembered". So to start a brand-new task I do not need special start-up code: I just
**build, by hand, the stack a paused task would have**, then "resume" it. The resume code pops the
(fake) registers and executes `reti`, which jumps to the address I planted - the first line of the task.

## Why every task needs its own stack
A stack holds a function's return addresses, its local variables and its saved registers. If two tasks
shared one stack they would overwrite each other's data the first time one was paused. So each task
gets a private array (`STACK_SIZE` = 128 bytes). SRAM budget: 2048 bytes in total.

## The Task Control Block (TCB)
One small struct per task (8 bytes on AVR):

| Field | Size | Meaning |
|-------|------|---------|
| `sp` | 2 | **Saved stack pointer** - where this task's stack top is while it is paused. **Must stay the first field** because the assembly code reads/writes it at offset 0. |
| `state` | 1 | `TASK_READY` (may run) or `TASK_DEAD` (its function returned; never run again). |
| `id` | 1 | 0 = idle task, 1..4 = user tasks. |
| `next` | 2 | Pointer to the next TCB. The last one points back to the first: a **circular list**, which is what makes round-robin "round". |
| `stack_low` | 2 | Address of the lowest byte of this task's stack. The **stack canary** (8 guard bytes) sits there. |

Note (a small change from the project brief): the brief lists "stack canary" inside the TCB. A canary has to
sit at the *end of the stack memory* to notice an overflow, so the canary bytes live at the bottom of each
stack and the TCB stores a pointer (`stack_low`) to them.

## Stack layout of a brand-new task (the fake saved context)
AVR facts used here (checked in the simavr source `_avr_push_addr`, the instruction behaviour that
`call`/interrupt use, and by the M4 test):
* the stack **grows downward** (push stores at SP, then SP := SP - 1);
* a 16-bit return address is pushed **low byte first**, so the low byte ends up at the *higher*
  address and the high byte just below it;
* `pop` does SP := SP + 1 and then reads.

```
 high address                                              (STACK_SIZE = 128, offsets from stack_low)
 +--------------------+ <- stack_low + 127
 | exit addr  LOW     |  } return address of "task_exit": if the task function ever
 | exit addr  HIGH    |  } does `ret`, it lands in task_exit() and is marked DEAD
 +--------------------+
 | entry addr LOW     |  } PC the interrupt "pushed": `reti` will jump here
 | entry addr HIGH    |  } = first instruction of the task function (word address)
 +--------------------+
 | r0   = 0           |  <- pushed first by the context-save code
 | SREG = 0           |  <- flags (I-bit is set later by `reti`)
 | r1   = 0           |  <- r1 MUST be 0: avr-gcc uses it as its "zero register"
 | r2   = 0           |
 |  ...               |     r2 .. r31 all 0 (r24/r25 could carry a task argument one day)
 | r31  = 0           |  <- pushed last
 +--------------------+ <- tcb.sp points HERE: the first free byte below the last push
 |  (unused, 0xA5)    |
 |  ...               |
 +--------------------+
 | canary 8 x 0xA5    |  <- stack_low .. stack_low + 7: if any of these changes, the stack overflowed
 +--------------------+ <- stack_low
 low address
```
Size of the fake frame: 2 (exit) + 2 (entry) + 1 (r0) + 1 (SREG) + 31 (r1..r31) = **37 bytes**.
The whole unused stack is pre-filled with `0xA5`. That has two uses: the canary check, and measuring the
"high-water mark" (how deep the stack ever got) by finding the first byte that is no longer `0xA5`.

## Why r1 = 0 matters
avr-gcc compiles C assuming **r1 always contains 0** (it uses it for multiply results and as a ready-made
zero). Hardware does not know this. After any context switch r1 must be 0 again when C code runs, so:
the fake frame has r1 = 0, and later the interrupt code will `clr r1` after saving the old value.

## Starting a task = "resuming" it
```
SP := tcb.sp ;  pop r31 ... pop r1 ;  pop r0 -> SREG ;  pop r0 ;  reti
```
`reti` pops the entry address into the PC and sets the I-bit, so the task starts with interrupts on,
its registers all zero, and below it on the stack the address of `task_exit`.

## Code added in M4
* `src/kernel.h`, `src/kernel.c`: `tcb_t`, `kernel_init`, `kernel_create` (builds the frame and links the circular list), `task_exit`.
* `src/context.h`: the `RESTORE_CONTEXT` assembly (M5 adds the matching save half).

## How M4 is tested
1. `m4_stack`: create two tasks, stop the simulator, dump SRAM and compare **every byte** of the frame with the layout above.
2. `m4_launch`: really start a task through the fake frame. The task checks that r1 == 0, writes a marker, then
   returns; we verify the marker, the r1 check, and that the task became `DEAD` via `task_exit`.
