# Architecture

A minimal preemptive Round-Robin kernel for the ATmega328P (Arduino UNO, 16 MHz). Source: `src/` (342 lines of C
in the four `.c` files incl. comments, plus 4 small headers). Built size of the demo: 1648 bytes flash + 66 bytes
initialised data (of 32 256 bytes flash), 824 bytes SRAM used (of 2048).

## 1. Files

| File | Role |
|------|------|
| `src/main.c` | The demo: 3 tasks, 3 LEDs, UART lines |
| `src/kernel.c/.h` | TCBs, stacks, task creation, scheduler, the naked tick ISR, idle task, stack check |
| `src/context.h` | The assembly strings: save/restore of r0-r31, SREG and SP |
| `src/timer.c/.h` | Timer1 CTC configuration (1 ms tick) |
| `src/uart.c/.h` | UART0 transmit: polled (`uart_puts`) and task-safe buffered (`uart_print`) |
| `tools/simtest.c`, `tests/` | Simulator harness and tests (PC side, not firmware) |

## 2. Memory map

### Flash (32 KB, word addressed by the CPU)
```
0x0000  interrupt vector table, 26 slots x 2 words (each slot is a `jmp`)
          slot 11 (0x002C) -> TIMER1_COMPA : the tick / context switch
          slot 19 (0x004C) -> USART_UDRE   : feeds the UART from the ring buffer
        start-up code (avr-libc crt): sets r1 = 0, SREG = 0, SP = RAMEND, copies .data, clears .bss, calls main
        program code + string constants
```
### SRAM (2048 bytes, 0x0100 - 0x08FF) of the demo build, from `avr-nm`
```
0x0000-0x001F  CPU registers r0-r31 (mapped into the data space)
0x0020-0x00FF  I/O registers (PORTB, UDR0, TCCR1B, SPL/SPH at 0x5D/0x5E, SREG at 0x5F ...)
0x0100-0x0141  .data  (66 B) slice_left + the string literals ("T1\r\n" ...) copied from flash at start-up
0x0142-0x014B  .bss   kernel_fault, rr_last, switches, ticks, ntasks
0x014C-0x03CB  stacks[5][128]   (640 B)   0x014C idle | 0x01CC T1 | 0x024C T2 | 0x02CC T3 | 0x034C unused slot (MAX_TASKS = 4)
0x03CC-0x03F3  tcbs[5]          (40 B)    8 bytes each: tcbs[0] = idle, tcbs[1..4] = user tasks
0x03F4-0x03F5  rr_current                 pointer to the running task's TCB
0x03F6-0x0437  tx_tail, tx_head, txbuf[64]  UART ring buffer
0x0438-0x08FF  free (1224 B). main()'s own stack lived at the top (RAMEND = 0x08FF) but is abandoned by kernel_start().
```
(Addresses can shift slightly if the code changes; regenerate with `avr-nm -n build/main.elf`.)

## 3. The Task Control Block (8 bytes)
```
offset  field       meaning
  0     sp (2)      saved stack pointer          <- the assembly code uses offset 0, keep it first
  2     state (1)   TASK_READY = 1, TASK_DEAD = 2
  3     id (1)      0 = idle, 1.. = user tasks
  4     next (2)    next TCB in the circular list (last -> first)
  6     stack_low   lowest byte of this task's stack (guard/canary bytes start here)
```
Circular list of user tasks (idle is outside the ring):
```
 tcbs[1] -> tcbs[2] -> tcbs[3] --+
    ^                            |
    +----------------------------+          tcbs[0] (idle): only used when nobody else is READY
```

## 4. Stack layout of one task (128 bytes)
```
 high  stack_low+127  +--------------------------+
                      | return address -> task_exit (2 bytes, lo then hi)  |  planted at creation
                      | task function address      (2 bytes, lo then hi)   |  = PC that `reti` pops at first start
                      | r0, SREG, r1, r2 ... r31   (33 bytes)              |  saved context (zeros for a new task)
  saved sp ---------> +--------------------------+  <- tcb->sp while the task is not running
                      |   free space (filled 0xA5; the deepest byte that is no longer 0xA5 = high-water mark)
                      +--------------------------+
 low   stack_low+0    | 8 guard bytes 0xA5 (canary) |
```
While the task runs, the layout above the saved sp is its normal call stack (return addresses, locals). At a tick the
37 bytes of context are pushed *on top of whatever the task has used*, so the task's own usable depth is about
`128 - 8 (guard) - 37 = 83` bytes. (Measured use in the demo: 49 bytes including the context.)

## 5. Timeline of ONE context switch (the tick at which task A hands over to task B)

Times are CPU cycles after the timer compare-match is recognised. The totals (331 / 372 cycles) are **measured**
in simavr; the per-phase split is **calculated** from the instruction timings in the instruction-set manual
(push/pop/lds/ld/st = 2, in/out/clr = 1, call = 4, reti = 4), so it is an explanation, not a separate measurement.

```
 t = 0     Timer1 reaches OCR1A (249): flag OCF1A is set. The CPU finishes the instruction it is executing.
 +4        Hardware: push PC (2 bytes) onto A's stack, clear the I-bit, jump to vector 11
 +3        vector slot: `jmp __vector_11`
 +68       SAVE_CONTEXT : push r0; in r0,SREG; push r0; push r1; clr r1; push r2..r31  (A's state is now on A's stack)
 +10       SAVE_SP_TO_CURRENT : tcb_A->sp := SP
 +4        call kernel_tick
 ..        kernel_tick: ticks++ ; stack_ok(A)? ; --slice_left == 0 -> pick_next() -> rr_current := B ; switches++
           (by subtraction about 165 cycles of the measured 331 / 372; not profiled in detail, the 8-byte canary loop is the likely biggest part)
 +10       LOAD_SP_FROM_CURRENT : SP := tcb_B->sp          <== THE SWITCH: from here the CPU is on B's stack
 +67       RESTORE_CONTEXT : pop r31..r2, pop r1, pop r0 -> SREG, pop r0
 +4        reti : pop B's PC, set the I-bit -> B continues exactly where it was interrupted
```
Ticks where the slice is not over (9 of every 10) take the same path, but `rr_current` does not change, so the "switch"
restores the very same task (331 cycles); a real switch costs 372 cycles. Overall cost: about 2.1 % of the CPU.

## 6. Design decisions and why
* **Context saved by the ISR, scheduler in C.** The assembly is only the part that must be assembly (push/pop/SP). The policy
  is ordinary C, easy to read and to change.
* **Full context on every tick**, even when no switch happens: simpler to explain than "save only if switching".
* **No blocking calls / no `sleep()`**, as scoped. Tasks wait by watching `kernel_ticks()`.
* **Task-safe printing without disabling the tick for long**: copy the line into a ring buffer inside a short critical section.
* **Idle task outside the ring** so that Round-Robin stays purely "equal turns", but the scheduler always has someone to run.
* **Canary + SP check at every tick** for the running task: cheap, no MPU needed, detects after the fact.
* **Atomic access** to multi-byte shared data (`kernel_ticks`, `kernel_switches`) by briefly clearing the I-bit and restoring SREG
  (not a blind `sei`, so it is also correct if called with interrupts already off).
* **`PINB = mask` for LED toggling**: one instruction, cannot be torn by a task switch (unlike `PORTB ^= mask`).

## 7. Known limits
* Detection of a stack overflow is after the fact (<= 1 ms) and can be fooled by writes that skip the guard bytes.
* A busy-waiting task burns its whole slice; with 3 tasks each gets 1/3 of the CPU regardless of how little it needs.
* LED toggle lateness up to ~20 ms because a task only runs 10 ms in every 30 ms (the average rate is exact).
* `uart_print` busy-waits if the 64-byte buffer is full; lines longer than 63 characters are truncated.
* `ticks` is 32-bit (wraps after 49.7 days); `switches` is 16-bit and wraps after 65 536 switches, tasks only compare it for "changed".
* The `kernel_panic` crash handler uses the polled UART, so it blocks for ~1 ms per character.
