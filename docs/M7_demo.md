# M7 - Demo: three tasks, three LEDs, report lines in plain words

**Goal:** a visible, explainable proof that multitasking works on a real UNO.

| Task | LED | Arduino pin | Toggles every |
|------|-----|-------------|---------------|
| T1 | PB5 | D13 (on-board LED "L") | 250 ms |
| T2 | PB0 | D8  (external LED) | 500 ms |
| T3 | PB1 | D9  (external LED) | 1000 ms |

## How a task waits without `sleep()`
There is no blocking API (out of scope). A task reads the system tick and compares it with the time
it last toggled: `if (kernel_ticks() - t0 >= period) { t0 += period; toggle; }`. It loops all the time, so it uses its whole
slice. That is wasteful but simple, and honest for a Round-Robin demo.
Because a task only runs 10 ms out of every 30 ms, a toggle can be up to ~20 ms late. I add `period`
to `t0` (not "now") so the lateness never accumulates: the average rate is exact.

## What the serial monitor shows
First a short explanation (the start-up text), then one line from a task every 10th time it gets the CPU:
```
    0.270 s | T1 has the CPU | turn     10 | D13 LED on
    0.280 s | T2 has the CPU | turn     10 | D8 LED off
    0.290 s | T3 has the CPU | turn     10 | D9 LED off
    0.570 s | T1 has the CPU | turn     20 | D13 LED off
```
* **time**: `kernel_ticks()` at that moment, i.e. milliseconds since the kernel started, printed as seconds.
* **turn**: each task counts its own turns. Its first run is turn 1. Whenever the kernel's switch counter has changed since the task
  last looked, the task was switched out and back in, so that is a new turn. Every 10th turn it prints.
* **LED**: the level of the task's own pin in `PORTB` at that moment. A task can be paused anywhere in its loop, so after it
  notices a new turn it first does the toggle check and only then prints: the line always shows the LED as it really is.
  (The first version printed before the toggle check; then a task paused between the two printed its LED one toggle too
  early. The emulator in the visualiser showed it, because a different compiler paused the task at a different instruction.)

Why exactly these numbers: T1 runs first (tick 0), then T2 (tick 10), T3 (tick 20), T1 again (tick 30) ... so each task gets a turn
every 30 ms, and turn k starts at 30 x (k - 1) ms, plus 10 ms for T2 and 20 ms for T3. Turn 10 therefore starts at
0.270 / 0.280 / 0.290 s, turn 20 at 0.570 / 0.580 / 0.590 s, and so on. Anyone can read the Round-Robin rotation off the screen:
the three lines of a group are one slice (10 ms) apart, and all tasks always show the same turn number (equal turns, fixed order).

How a line is built (`report()` in `src/main.c`): two small helpers append text (`put_str`) and a number in decimal (`put_dec`,
which takes the digits off with `% 10` and `/ 10`, lowest digit first, and writes them back in reverse). No `printf`: the project
only uses `avr/io.h` and `avr/interrupt.h`.

Each task builds its line in **its own buffer** (`line[72]` inside its `demo_task_t`). A task can be paused halfway through writing
its line; if the tasks shared one buffer, the next task would write over it.

## The start-up text and `__flash`
On the AVR, code and data live in separate memories (Harvard architecture). A normal string constant is copied from flash into
the 2 KB of RAM at reset and stays there. The 303-byte start-up text would use 15 % of the RAM just to be printed once, so it is
declared `const __flash char banner[]`: it stays in flash only, and `uart_puts_flash()` reads it with the `LPM` instruction.
`__flash` is a GCC extension for the AVR (GCC manual, "Named Address Spaces"); it needs no extra header.

## Why a task-safe print is needed (the new part of `uart.c`)
The polled `uart_puts` of M2 sends one letter at a time and waits in between. If the tick fires in the middle of a line,
another task could start printing, and the terminal would show a mix of two lines.
Fix = `uart_print(line)`:
1. Under a short critical section (`cli` ... restore SREG) copy the **whole line** into a 256-byte ring buffer,
   or, if there is not enough free room, open the interrupts for an instant and try again.
2. Set `UDRIE0` ("USART Data Register Empty Interrupt Enable").
3. The `USART_UDRE_vect` interrupt takes one byte at a time out of the buffer into `UDR0`, whenever the
   hardware is ready. When the buffer is empty it clears `UDRIE0` again.

So each line goes into the buffer in one piece, and the hardware sends the bytes later by itself. Lines can
never be mixed. The producers are tasks, the consumer is the UDRE interrupt; `head` is only moved by producers
(inside the critical section), `tail` only by the interrupt, so they never fight.

Why 256 bytes: the three tasks print their lines 10 ms apart (about 57 characters each). At 9600 baud, where M7 started, only
about 10 characters leave in 10 ms, so the buffer had to hold about 150 characters at once. Since M9 the line runs at 250000 baud
and the buffer must hold a dashboard frame (141 bytes) plus a report line; 256 still fits both. A power of two lets
`& TXBUF_MASK` do the wrap-around. (Since M9 the copying itself is in `uart_queue()`, which the frames use too.)

How long are interrupts off? The copy loop takes about 18 cycles per character plus about 45 (counted from the instruction
listing of the `make` build), so a 57-character line takes about 1 070 cycles = 67 us. That is far less than the 1 ms tick: a tick
that arrives during the copy is only a little late, never lost. In this demo the copy even happens right after the task got the
CPU, well before the next tick. In a 6 s simulator run single ticks were at most 24 cycles (1.5 us) late (the UART interrupt).

Limits: a line longer than 255 characters is cut. If the buffer is full a task waits (busy) until space appears.
The polled `uart_puts` / `uart_puts_flash` are still used before the kernel starts (the start-up text) and by the crash handler.

## Stack budget (128 bytes per task)
saved context 35 bytes + `call kernel_tick` 2 + `kernel_tick`'s own 6 + the task's own calls (`report()` and `put_dec()` with
its 10-byte digit array). M8 measures the real high-water mark with the `0xA5` fill pattern: 61 bytes per task
(76 while the dashboard's frames are on, see M9).

## How M7 is tested
Run `main.elf` for 5 s in simavr and check:
* the start-up text comes first, then only report lines, each in the exact format (no garbled line);
* the lines come in strict rotation T1, T2, T3, T1 ...;
* every line's numbers are exactly the ones predicted above (turn 10 at 0.270 / 0.280 / 0.290 s, ...);
* every printed "LED on/off" equals the real pin level in that slice (sampled 5 ms after the tick that started the slice);
* the three pins toggle at 250/500/1000 ms (average and worst lateness), there is no stack fault, and how much stack each task used.
