# M9 - A link to the dashboard: one frame per task switch

**Goal:** let a web page (RR-OS Scope, in the `visualizer` folder next to the Arduino sketch) show what the *real* board is
doing at every task switch, not a simulation of it. The page opens only when the board is connected and in sync.

## Why the text lines were not enough
The report lines come once every 100 ms per task, and at 9600 baud the line was already busy. A switch happens every 10 ms.
To show every switch, the board has to say something at every switch, and that needs a faster serial line.

## What changed
| File | Change |
|------|--------|
| `src/trace.c/.h` (new, 63 lines) | the receiver (to hear the dashboard's `T`) and `trace_switch()`, which queues one frame |
| `src/kernel.c` | `kernel_tick()` calls `trace_switch(prev, next, ticks, switches)` after a switch, only if `trace_on` |
| `src/uart.c/.h` | 250000 baud instead of 9600; the copy into the ring buffer is now `uart_queue()` (used by `uart_print` and by the frames) and `uart_room()` |
| `src/main.c` | calls `trace_init()` |

## 250000 baud
`UBRR = F_CPU / (16 x baud) - 1 = 16 000 000 / 4 000 000 - 1 = 3`, and `16 MHz / (16 x 4) = 250000` **exactly** (0 % error;
9600 baud had +0.16 %). One character is 10 bits = 40 us. The UNO's USB chip (an ATmega16U2) also runs from 16 MHz; in the
3-minute test on the real board not a single byte arrived wrong. The Arduino IDE's Serial Monitor offers 250000 in its baud
list (checked with `arduino-cli monitor --describe`).

## The command and the frame
The dashboard sends one letter: `T` = start sending frames, `t` = stop. After every reset `trace_on` is 0, so a board on its own,
or with the Arduino IDE's Serial Monitor, sends only the text. The receiver interrupt (`USART_RX_vect`) reads `UDR0` and sets
or clears `trace_on`.

At every task switch, while `trace_on` is 1, `kernel_tick()` (inside the tick interrupt, interrupts off) queues 141 bytes:

| Bytes | Meaning |
|-------|---------|
| 0 | `0x80 + id` of the task that gets the CPU. Text never has bit 7 set, so this byte marks the start of a frame |
| 1-4 | tick (kernel time in ms), lowest byte first |
| 5-6 | number of switches so far |
| 7 | id of the task that was just paused |
| 8 | `PORTB` at this moment (the three LED pins) |
| 9-10 | the paused task's saved stack pointer (`tcb->sp`) |
| 11-12 | the address of its stack (`tcb->stack_low`) |
| 13-140 | its whole stack, 128 bytes from `stack_low` upward |

The last 35 bytes above the saved SP are exactly what the interrupt saved a moment ago: r31..r2, r1, SREG, r0 and the PC
(the address where the task will continue). So the dashboard shows the paused task's real registers and flags, and, with the
build's symbol table, the function and source line where it stopped. The top two bytes of every stack are the `task_exit`
address planted by `build_frame()`, and the bottom 8 are the canary.

## Never wait inside the interrupt
`trace_switch()` first checks with `uart_room()` that the whole frame fits into the 256-byte send buffer. If not, it skips
this frame (a switch number is then missing, and the dashboard counts it). It never waits, because it runs inside the tick
interrupt. In every test so far no frame was skipped.
The 13-byte head is a `static` array, not a local variable, so it does not take room on the paused task's stack.

## What it costs (measured in simavr, `make` build)
* A switch with a frame takes **3128 cycles (196 us)** instead of 380, with interrupts off. That is still far less than one
  tick (16000 cycles), so no tick is lost; switches happen every 10 ms, so it adds about 1.7 % CPU time.
* While frames are on, the tick interrupt goes deeper into the paused task's stack (it calls `trace_switch`, which calls
  `uart_queue`): the demo tasks then reach **76 bytes** of their 128 (61 without frames). On the real board (Arduino IDE build) 65.
* The frames take about 58 % of what 250000 baud can carry (14.5 kB/s on the real board).
* Single ticks were at most 21 cycles (1.3 us) late with frames on (24 without), in a 6 s simulator run.

## How it is tested
* **Simulator (M9 in `tests/run_tests.py`):** the harness can now type a byte into the UART receiver (`simtest -r <ms> <byte>`).
  `T` at 100 ms, `t` at 2000 ms. Checked: no frame byte before `T`; report lines still all correct between the frames; 190 frames
  with consecutive switch numbers 10 ticks apart; **each frame's tick, switch number, paused task, next task and PORTB equal what
  the simulator itself recorded at that switch**; the stack address, the canary and the planted `task_exit` are right in every
  frame; no frames after `t`; the cost and the stack depth above.
* **Real UNO (29 Oct 2026, 3 minutes):** 17837 frames, none missing, every slice exactly 10 ticks, strict rotation, addresses
  match the build, canaries intact, all 53511 LED bits follow the blink rule, all 1782 report lines exactly as predicted.
* **The dashboard page** was run against the real board through a test bridge (see README, "Tested on the real UNO").

## The dashboard in short
1. It opens the port at 250000 baud and pulses DTR, which resets the UNO (the bootloader runs, then the start-up text comes).
   Anything before the start-up text is left over in the USB chip from before the reset and is ignored.
2. After the start-up text it sends `T`. Frames start arriving.
3. It is "in sync" after three switches in a row, 10 ticks apart, nothing missing. Only then does the dashboard open.
4. A frame head that cannot be real (task id above 4, stack pointer outside the stack ...) is treated as noise: the page drops
   one byte and reads on, so it finds the next real frame.
5. If no frame comes for 0.8 s the page says it is not live; if the start-up text appears again, the board was reset and the
   page starts over. On Disconnect it sends `t`.
6. It checks every frame and line as it arrives (slices, rotation, canaries, LED pins, report lines, missing frames, the build)
   and measures the board's clock against the PC from the least-delayed frame of each second.
