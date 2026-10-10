# M0 - Toolchain check

**What this step is:** before writing any code, make sure every tool I need exists and works.

## What each tool does (plain words)

| Tool | Job |
|------|-----|
| `avr-gcc` | Compiler: turns my C into machine code for the AVR chip (not for the PC). |
| `avr-libc` | The small C library for AVR. I use **only** its headers `avr/io.h` (register names like `PORTB`) and `avr/interrupt.h` (the `ISR()` macro). |
| `make` | Runs the build recipe in the `Makefile`, so I type `make` instead of long commands. |
| `avrdude` | Copies the finished `.hex` file into the Arduino UNO's flash over USB. |
| `simavr` | A software copy of the ATmega328P. It lets me run and test my program on the PC without a board. |

## What was found / installed (this PC, Windows 11)

| Tool | Status at start | Action | Version now |
|------|-----------------|--------|-------------|
| avr-gcc | missing | `winget install ZakKemble.avr-gcc` | GCC 16.1.0 (binutils 2.46.1) |
| avr-libc | missing | comes inside the package above | 2.3.2 |
| make | missing | comes inside the package above | GNU Make 4.2.1 |
| avrdude | missing | comes inside the package above | 8.2 |
| simavr | missing, **no Windows package exists** | built from source with MSYS2 (see `tools/build_simavr.sh`). Gotcha found on the way: without `pkgconf` installed simavr builds *silently without ELF support* ("ELF format is not supported by this build") | git commit `d6aed53` |
| python 3 | present | used only to run my test scripts | 3.13 |

simavr **was** installed (built), so simulation tests are possible.
Important honesty note: simavr is a *model*. It matches the datasheet closely but it is not the real chip.
Anything that depends on real electrical behaviour (LED brightness, USB-serial driver, real
crystal accuracy, bootloader) cannot be tested in it. The README lists these.

## The test harness (`tools/simtest.c`)

The stock `run_avr` program only runs firmware. I wrote a small C program that links against
simavr's library and prints events (UART bytes, pin changes, how many CPU cycles an interrupt
took, memory dumps) with exact cycle numbers. Python scripts in `tests/` read that output and check it.
Build it with `sh tools/build_simtest.sh` inside an MSYS2 UCRT64 shell.

## Facts I checked in the avr-libc header `iom328p.h` that ships with the compiler

* `TIMER1_COMPA_vect_num` = 11, `USART_UDRE_vect_num` = 19. Each vector slot is 2 words (a `jmp`),
  so vector N lives at byte address `N*4` (vector 11 -> 0x2C).
* `RAMEND` = 0x8FF, so SRAM is 0x0100-0x08FF = 2048 bytes.
* `OCR1A`, `TIMSK1`, `UBRR0`, `UDR0`, `SMCR`, `GPIOR0` exist at the addresses the datasheet gives.
