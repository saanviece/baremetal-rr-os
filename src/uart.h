/* UART0 transmit, written directly on the ATmega328P registers. */
#ifndef UART_H
#define UART_H

#include <stdint.h>

void uart_init(void);          /* 250000 baud, 8N1, transmitter only */
void uart_putc(char c);        /* polled: waits for the data register to be free, then sends */
void uart_puts(const char *s); /* polled string. NOT task-safe: use only before the kernel starts / in the crash handler */
void uart_puts_flash(const __flash char *s); /* the same, for text kept in flash memory instead of RAM */
void uart_print(const char *s);/* task-safe: the whole string goes into a ring buffer in one piece (max 255 chars) */
uint8_t uart_room(void);       /* free bytes in the ring buffer (interrupts off) */
uint8_t uart_queue(const void *data, uint8_t n); /* add n raw bytes only if all fit; never waits (interrupts off) */

#endif
