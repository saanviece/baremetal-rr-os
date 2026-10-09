# M2 - UART transmit written in registers

**Goal:** be able to print text to the PC (debug output) without any serial library.

## What a UART is
A UART sends bytes one bit at a time on one wire (TX = pin D1 / PD1 on the UNO, which the UNO's USB chip
forwards to the PC). Both ends must agree on the speed (**baud**) and the frame shape. I use the
common **9600 baud, 8N1**: 8 data bits, No parity, 1 stop bit. One byte = 10 bits on the wire
(1 start + 8 data + 1 stop), so one character takes 10 / 9600 = 1.04 ms.

## The registers I touch (ATmega328P, USART0)
| Register | What I write | Why |
|----------|--------------|-----|
| `UBRR0`  | 103 | Baud-rate divider. Formula (normal mode): `UBRR = F_CPU / (16 x baud) - 1` = 16 000 000 / 153 600 - 1 = 103.17 -> 103. Real baud = 16 MHz / (16 x 104) = 9615, an error of +0.16 %, which is fine. |
| `UCSR0B` | `TXEN0` | Switch the transmitter on (this also takes over pin PD1). |
| `UCSR0C` | `UCSZ01 | UCSZ00` | 8 data bits (the reset default is already 8N1, but I write it so the code documents itself). |
| `UCSR0A` bit `UDRE0` | read | "Data Register Empty": 1 means the hardware is ready to accept the next byte. |
| `UDR0`   | the byte | Writing here starts sending it. |

## How `uart_putc` works
```
wait until UDRE0 == 1     <- the previous byte has left the data register
UDR0 = c                  <- hardware now shifts it out by itself
```
This is *polled* (the CPU loops waiting). It is simple and fine for M2, but **not safe to call from several
tasks at once** (two tasks could interleave letters). M7 adds a task-safe version on top; this polled
one stays for start-up messages and for the crash handler.

## How M2 is tested
Run in simavr, capture each byte that leaves the UART with its cycle number, and check
(a) the text is exactly what I sent, (b) the bit time is (UBRR + 1) x 16 = 1664 CPU cycles.
Honest detail: simavr's UART model always counts a parity slot, so it spaces characters 11 bits apart
(18 304 cycles) instead of the 10 bits (16 640 cycles) real 8N1 hardware uses. So the test checks the
*bit time*, which my UBRR value controls, and not the per-character time.
