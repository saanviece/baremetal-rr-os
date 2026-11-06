# References and resources used

The code in `src/` and `tests/` was written by me for this project. Nothing was copied from the sources below; they were used for
facts and ideas only. Where I could not read a primary source I say so.

## Specifications / manuals
1. **ATmega328P datasheet** (Microchip DS40002061) - register names, Timer1 CTC mode and prescaler bits, USART registers,
   interrupt behaviour. *The 700-page PDF was too large for my fetch tool, so I did not read it directly. I confirmed the individual
   facts below through Microchip's online AVR documentation and other secondary sources (items 2-4) and through the avr-libc header (item 5).
   Please double-check the register tables in the PDF yourself before the viva.*
2. Microchip online documentation, I/O ports "Toggling the Pin": writing a 1 to PINxn toggles PORTxn; also usable with SBI.
   https://onlinedocs.microchip.com/oxy/GUID-173AD72D-41FE-4760-A93C-7078A02BD908-en-US-7.1.1/GUID-0D75C2FC-DA70-4975-90D6-CB15803FEA46.html
   (same feature appears as 16.2.2 / 19.2.2 in the other AVR families' manuals; the ATmega328P chapter number is "I/O-Ports")
3. Microchip Developer Help, "Understanding the USART Baud Rate Macro": UBRR = F_CPU/(16*baud) - 1, 9600 baud at 16 MHz gives 103.
   (Since M9 the program uses 250000 baud; the same formula gives exactly 3.)
   https://developerhelp.microchip.com/xwiki/bin/view/products/mcu-mpu/8-bit-avr/getting-started/8-bit-avr-usart-baud-rate-macro/
4. Arduino forum thread on UART at 16 MHz (error of about 0.16 % for UBRR = 103). Secondary source only.
5. **avr-libc 2.3.2 headers** shipped with the compiler: `avr/iom328p.h` (register addresses, vector numbers 11 = TIMER1_COMPA,
   19 = USART_UDRE, RAMEND = 0x8FF), `avr/interrupt.h` (`ISR`, `ISR_NAKED`).
6. AVR instruction-set behaviour (PUSH/POP/CALL/RET/RETI cycle counts and stack byte order): I checked the **simavr source**
   (`sim_core.c`: `_avr_push_addr`, `_avr_pop_addr`, PUSH/POP) rather than the instruction-set manual PDF, and verified the order by
   running my own code (test M4). The cycle counts in the timeline of `ARCHITECTURE.md` are from my memory of the manual and are
   labelled as calculated there; the measured totals come from the simulator.
7. **GCC manual, "Named Address Spaces"**, section "AVR Named Address Spaces": `__flash` places data in the `.progmem.data`
   section of flash and the compiler reads it with the `LPM` instruction (used for the start-up text in `main.c`).
   https://gcc.gnu.org/onlinedocs/gcc/Named-Address-Spaces.html

## Tools
8. avr-gcc 16.1.0 / binutils 2.46.1 / avr-libc 2.3.2 / GNU Make 4.2.1 / AVRDUDE 8.2 - Windows build by Zak Kemble (winget `ZakKemble.avr-gcc`).
9. **simavr** (Michel Pollet) https://github.com/buserror/simavr , commit d6aed53, built from source with MSYS2/MinGW. I read its
   source for the UART timing model (`avr_uart.c`: 11 bit-times per character) and the stack push/pop code. My harness `tools/simtest.c`
   only *uses* its public library API; its own example programs were not copied.
10. MSYS2 (compiler + libelf + pkgconf for building simavr).

## Tutorials (ideas only)
11. The project brief suggested Kevin Cuzner, "Writing a preemptive task scheduler for AVR". **I did not read it** (not needed:
    the design comes from the datasheet facts above and the project brief). Listed here only because it was named as allowed reading.

## Project slides
12. "Minor Project intro PPT" (S. Chauhan, mentor Dr. Divya Prakash) - scope and requirements. Its own references:
    De Sio et al. 2023 (Microelectronics Reliability); Cernat et al. 2023 (IEEE SIITME); Lee & Kim 2003 (IEEE WSTFES).

## The dashboard (RR-OS Scope, in the `visualizer` folder next to the Arduino sketch; not part of this repository)
13. **Web Serial API** (W3C Web Platform Incubator Community Group draft, built into Microsoft Edge and Google Chrome): how the
    page opens the UNO's port, reads and writes bytes and pulses DTR. https://wicg.github.io/serial/
14. **arduino-cli** 1.5.1 (bundled with the Arduino IDE 2): `build_firmware.py` compiles and uploads the sketch with it, so the
    page and the board have the same build. `arduino-cli monitor --describe` lists the Serial Monitor's baud rates (250000 included).
15. **avr8js** 0.21.1 (Uri Shaked and contributors, Wokwi, MIT licence): the emulator in the separate simulator page.
    https://github.com/wokwi/avr8js
16. For testing the pages only (not used by them): Node.js, puppeteer-core (drives a headless Microsoft Edge) and the npm package
    `serialport` (a bridge from the test browser to the real COM port).
