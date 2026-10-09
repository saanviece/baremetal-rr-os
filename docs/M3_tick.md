# M3 - Timer1 tick interrupt (a 1 ms heartbeat)

**Goal:** make the hardware call a function of mine exactly every 1 ms, and count the calls.
This "tick" is the heartbeat the scheduler will later use to decide when to switch tasks.

## Interrupt in plain words
An *interrupt* is the hardware tapping the CPU on the shoulder: "stop what you are doing, run this
special function, then carry on." When it happens the CPU automatically
1. finishes the current instruction,
2. pushes the address of the next instruction (the **PC**) on the stack,
3. clears the global interrupt flag (I-bit in **SREG**) so no second interrupt can barge in,
4. jumps to the **vector** - a fixed slot at the start of flash. Slot 11 is "Timer1 Compare Match A".
Each slot holds a `jmp` to my function (the compiler fills the table when I write `ISR(TIMER1_COMPA_vect)`).
`reti` at the end pops the PC again and sets the I-bit.

## Timer1 set-up (all numbers from the datasheet formulas)
* The timer is a counter `TCNT1` that goes up by 1 every *timer clock*.
* Prescaler 64: timer clock = 16 MHz / 64 = **250 kHz** -> 4 us per count. Bits `CS11 | CS10` in `TCCR1B`.
* **CTC mode** ("Clear Timer on Compare"): when `TCNT1` reaches `OCR1A` it resets to 0 and raises the
  interrupt. Bit `WGM12` in `TCCR1B`.
* Period = (OCR1A + 1) counts x 4 us. For 1 ms we need 250 counts, so **OCR1A = 249**
  (the "+1" is because counting 0..249 is 250 steps).
* `TIMSK1.OCIE1A = 1` lets the compare-match raise the interrupt; `sei()` sets the global I-bit.

So: 16 000 000 / 64 / 250 = 1000 ticks per second, one tick = exactly **16 000 CPU cycles**.

## Reading the counter safely
`ticks` is 32 bits but the CPU is 8-bit, so reading it takes 4 separate loads. If the interrupt fires
in the middle I could read a half-old, half-new number. `kernel_ticks()` therefore switches
interrupts off for those few cycles (`cli`), copies, and puts SREG back. This is the first example of a
**critical section**.

## Code layout
* `src/timer.c` - `timer_start()` only configures the hardware (stays forever).
* the ISR that counts ticks lives in the test firmware `tests/fw/m3_tick.c` for now; from M5 the kernel
  owns that vector.

## How M3 is tested
Run for 1 s in simavr. Check: the ISR starts every 16 000 cycles exactly, `ticks` ends at ~1000 and
PB5 (toggled by main every 500 ticks) changes every ~0.5 s.
