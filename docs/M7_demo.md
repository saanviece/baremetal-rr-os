# M7 - Demo: three tasks, three LEDs, UART lines

**Goal:** a visible, explainable proof that multitasking works on a real UNO.

| Task | LED | Arduino pin | Toggles every | UART line |
|------|-----|-------------|---------------|-----------|
| T1 | PB5 | D13 (on-board LED "L") | 250 ms | `T1` |
| T2 | PB0 | D8  (external LED) | 500 ms | `T2` |
| T3 | PB1 | D9  (external LED) | 1000 ms | `T3` |

## How a task waits without `sleep()`
There is no blocking API (out of scope). A task reads the system tick and compares it with the time
it last toggled: `if (kernel_ticks() - t0 >= period) { t0 += period; toggle; }`. It loops all the time, so it uses its whole
slice. That is wasteful but simple, and honest for a Round-Robin demo.
Because a task only runs 10 ms out of every 30 ms, a toggle can be up to ~20 ms late. I add `period`
to `t0` (not "now") so the lateness never accumulates: the average rate is exact.

## How the UART lines show the rotation
Each task counts how many times it has been given the CPU: the kernel keeps a switch counter, and if
that counter has changed since the task last looked, it has had a new turn. Every 10th turn a task prints its name.
T1 gets its 10th turn first, T2 10 ms later, T3 10 ms after that, so the terminal shows

```
T1
T2
T3
T1
...
```
in a strict rotation (one line each 100 ms).

## Why a task-safe print is needed (the new part of `uart.c`)
The polled `uart_puts` of M2 sends one letter at a time and waits in between. If the tick fires in the middle
of "T1\r\n", T2 could start printing, and the terminal would show a mix like `T1T2\r\n\r\n`.
Fix = `uart_print(line)`:
1. Under a short critical section (`cli` ... restore SREG) copy the **whole line** into a 64-byte ring buffer,
   or, if there is not enough free room, open the interrupts for an instant and try again.
   Interrupts are off for only ~100 cycles, so no tick is lost.
2. Set `UDRIE0` ("USART Data Register Empty Interrupt Enable").
3. The `USART_UDRE_vect` interrupt takes one byte at a time out of the buffer into `UDR0`, whenever the
   hardware is ready. When the buffer is empty it clears `UDRIE0` again.

So each line goes into the buffer in one piece, and the hardware sends the bytes later by itself. Lines can
never be mixed. The producers are tasks, the consumer is the UDRE interrupt; `head` is only moved by producers
(inside the critical section), `tail` only by the interrupt, so they never fight.

Limits: a line longer than 63 characters is cut. If the buffer is full a task waits (busy) until space appears.
The old polled `uart_puts` is still used before the kernel starts (the banner) and by the crash handler.

## Stack budget (128 bytes per task)
fake/saved context 35 bytes + `kernel_tick` call ~ 4 + UART interrupt ~ 12 + the task's own calls. M8 measures the real
high-water mark with the `0xA5` fill pattern.

## How M7 is tested
Run `main.elf` for 5 s in simavr and check: the three pins toggle at 250/500/1000 ms (average and worst lateness),
the UART shows the banner followed by T1, T2, T3 in strict rotation with no corrupted line, no stack fault, and how
much stack each task used.
