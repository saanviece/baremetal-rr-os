/* UART0 transmit, written directly on the ATmega328P registers. */
#include <avr/io.h>
#include "uart.h"

#define BAUD     9600UL
#define UBRR_VAL (F_CPU / 16 / BAUD - 1)      /* datasheet formula, normal speed mode = 103 */

void uart_init(void)
{
    UBRR0  = UBRR_VAL;                        /* baud-rate divider */
    UCSR0B = (1 << TXEN0);                    /* turn the transmitter on */
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);   /* 8 data bits, no parity, 1 stop bit */
}

void uart_putc(char c)
{
    while (!(UCSR0A & (1 << UDRE0)))          /* wait until the previous byte has left UDR0 */
        ;
    UDR0 = c;                                 /* writing UDR0 starts the transmission */
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}
