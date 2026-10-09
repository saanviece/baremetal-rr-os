# M1 - Makefile and LED blink (direct register writes)

**Goal:** prove the whole chain works: C file -> compiler -> `.hex` -> (simulated) chip -> pin changes.
No Arduino core, no `digitalWrite`. I talk to the chip by writing its registers.

## Registers in plain words
A *register* here is a special memory address that is wired to hardware. Writing to it changes
the real chip. For a pin on PORTB there are three:

| Register | Meaning |
|----------|---------|
| `DDRB`  | Data Direction. Bit = 1 means "this pin is an OUTPUT". |
| `PORTB` | Output value. Bit = 1 drives the pin HIGH (5 V), 0 drives it LOW. |
| `PINB`  | Reads the real pin level. **Writing a 1 here toggles the matching PORTB bit.** |

The Arduino UNO LED "L" is on **PB5** (Arduino pin D13).

## Why I toggle with `PINB = ...` and not `PORTB ^= ...`
`PORTB ^= mask` is three steps: read PORTB, flip, write back. If the timer interrupt hits
between the read and the write and another task changes PORTB, my write-back erases its change
(a *race condition*). Writing to `PINB` is ONE instruction, so it cannot be cut in half. Later three tasks each own a
different PORTB pin, so this matters. (The AVR "toggle by writing PIN" feature is described in the
AVR I/O-port chapter, "Toggling the Pin" - see REFERENCES.md.)

## The delay in M1
`_delay_ms` is not allowed and there is no timer yet, so M1 uses a counted empty loop with a
`volatile` counter (volatile stops the compiler deleting the "useless" loop). It is only for
bring-up; M3 replaces it with the timer tick.

## The Makefile
`make` -> compiles each `.c` with `avr-gcc -mmcu=atmega328p -Os`, links to `build/main.elf`,
converts to `build/main.hex` with `avr-objcopy`, prints the size with `avr-size`.
Other targets: `make flash PORT=COM3`, `make disasm`, `make test`, `make clean`.
Flags worth knowing: `-DF_CPU=16000000UL` (tells the code the clock speed),
`-ffunction-sections` + `-Wl,--gc-sections` (throw away unused functions).

## How M1 is tested
Run the ELF in simavr, record every change of PB5 with its CPU-cycle number, and check
(a) the pin toggles, (b) the interval between toggles is constant.
