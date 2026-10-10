# M8 - Tests and measurements (all run in simavr)

Run everything with `python tests/run_tests.py` (or `make test`). The full real output of the last run is saved in
[`TEST_RESULTS.txt`](TEST_RESULTS.txt). Nothing below is claimed unless that run printed it.

## What each test proves

| # | Question | How it is tested | Result (last run) |
|---|----------|------------------|-------------------|
| 1 | Do tasks run in Round-Robin order? | Sample `rr_current` at every tick (M5/M6 tests) | runs of exactly 10 ticks, order 1,2,3,1,2,3 (39 slices checked) |
| 2 | Do registers and flags survive a switch? | `tests/fw/m8_regs.c`: 3 assembly tasks, each keeps 32 registers as 16 mirror pairs (`r<k>` == `r<k+16>`), adds 1 to all of them every loop, and checks flags C and T across a wait | 1999 switches (1 per tick) and 299 switches (10-tick slices): **0 failures**; tens of thousands of checksum passes per task |
| 2b | Can that test fail at all? | Mutation test: rebuild the kernel with a deliberate bug (`-DFAULT_R7` corrupts r7, `-DFAULT_CARRY` clears C after every restore) | both bugs detected (`GPIOR0` fail flags set) |
| 3 | Is the tick period right? | Cycle number of every tick interrupt | average 16000.001 cycles = 1.00000 ms (the 1-4 cycle jitter per tick is the CPU finishing its current instruction) |
| 4 | What does the kernel cost? | Cycles from the vector's first instruction to the end of `reti` (+4 for hardware entry) | tick without task change 331 cycles (20.7 us); tick with task change 372 cycles (23.2 us); about 2.1 % of the CPU |
| 5 | Does the canary catch an overflow? | Task 2 recurses until its stack is exceeded (`m8_overflow.c`) | `!STACK OVERFLOW T2` printed, `kernel_fault` = 3, detected 0.99 ms (one tick) after the task got the CPU, kernel halted |
| 5b | False alarms? | Same program with 2 calls deep; and the 5 s demo | none |
| 6 | How much stack is really used? | `0xA5` fill pattern high-water mark after the 5 s demo | tasks 49 of 128 bytes, idle 37 of 128 |

## Things the measurements taught me (good viva material)
* The first version of my overflow control "failed": a task needs room for its **37-byte saved context at any tick**, so
  the usable stack is about 128 - 8 (guard) - 37 = **83 bytes**, not 128.
* The first overflow test only wrote 2 bytes of each 24-byte frame, so the guard bytes stayed `0xA5`, yet the overflow was
  still caught because the kernel also checks that the saved **SP** is not inside the guard zone. Two checks are better than one.
* simavr's UART model counts 11 bits per character (it always includes a parity slot) while 8N1 hardware sends 10, so the
  serial text is about 10 % slower in the simulator than on a real board. The bit time (1664 cycles) matches the formula.
* Adding the stack check made a tick cost 331 cycles instead of the 202 cycles measured in M5 (switch only).
  I kept the readable loop; a faster check is an easy later optimisation.

## What these tests do NOT cover
No real hardware (see README); timing is *cycle-exact in the model* but the model is not the silicon. Interrupt-latency
worst cases with every instruction type, SRAM exhaustion by other means, and a task writing a stack overflow that jumps
over the guard bytes are not tested.
