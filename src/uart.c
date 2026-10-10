/* UART0 transmit, written directly on the ATmega328P registers. */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "uart.h"

#define BAUD     9600UL
#define UBRR_VAL (F_CPU / 16 / BAUD - 1)      /* datasheet formula, normal speed mode = 103 */

void uart_init(void)
{
    UCSR0A = 0;                               /* normal speed (U2X0 = 0), as the UBRR formula assumes */
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

/* ---- task-safe printing: ring buffer + "data register empty" interrupt ---- */

#define TXBUF_SIZE 64                             /* power of two so that "& MASK" wraps the index */
#define TXBUF_MASK (TXBUF_SIZE - 1)

static volatile uint8_t txbuf[TXBUF_SIZE];
static volatile uint8_t tx_head;                  /* next free slot: moved only by uart_print (interrupts off) */
static volatile uint8_t tx_tail;                  /* next byte to send: moved only by the UDRE interrupt */

void uart_print(const char *s)
{
    uint8_t n = 0;
    while (s[n])                                  /* length of the line */
        n++;
    if (n > TXBUF_MASK)                           /* the buffer keeps one slot empty, so 63 is the maximum */
        n = TXBUF_MASK;

    for (;;) {
        uint8_t sreg = SREG;                      /* remember whether interrupts were on */
        cli();                                    /* critical section: no task switch while we copy */
        uint8_t used = (tx_head - tx_tail) & TXBUF_MASK;
        if (TXBUF_MASK - used >= n) {             /* the whole line fits */
            for (uint8_t i = 0; i < n; i++) {
                txbuf[tx_head] = s[i];
                tx_head = (tx_head + 1) & TXBUF_MASK;
            }
            UCSR0B |= (1 << UDRIE0);              /* let the interrupt start draining the buffer */
            SREG = sreg;
            return;
        }
        SREG = sreg;                              /* full: allow the UDRE interrupt to run, then retry */
    }
}

ISR(USART_UDRE_vect)                              /* runs whenever UDR0 is empty and UDRIE0 is set */
{
    if (tx_tail != tx_head) {
        UDR0 = txbuf[tx_tail];                    /* hand one byte to the hardware */
        tx_tail = (tx_tail + 1) & TXBUF_MASK;
    } else {
        UCSR0B &= ~(1 << UDRIE0);                 /* nothing left: stop these interrupts */
    }
}
