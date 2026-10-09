/* UART0 transmit, written directly on the ATmega328P registers. */
#ifndef UART_H
#define UART_H

void uart_init(void);          /* 9600 baud, 8N1, transmitter only */
void uart_putc(char c);        /* polled: waits for the data register to be free, then sends */
void uart_puts(const char *s); /* polled string. NOT task-safe: use only before the kernel starts / in the crash handler */
void uart_print(const char *s);/* task-safe: the whole string goes into a ring buffer in one piece (max 63 chars) */

#endif
