/* UART0 transmit, written directly on the ATmega328P registers. */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "uart.h"

#define BAUD     250000UL                     /* fast enough for the dashboard's switch frames (trace.c) */
#define UBRR_VAL (F_CPU / 16 / BAUD - 1)      /* datasheet formula, normal speed mode: 16 MHz / 16 / 250000 - 1 = 3, exact */

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

void uart_puts_flash(const __flash char *s)
{
    while (*s)                                /* reading through a __flash pointer uses the LPM instruction */
        uart_putc(*s++);
}

/* ---- task-safe printing: ring buffer + "data register empty" interrupt ---- */

#define TXBUF_SIZE 256                            /* power of two so that "& MASK" wraps the index; */
#define TXBUF_MASK (TXBUF_SIZE - 1)               /* holds a switch frame (141 bytes) plus a report line */

static volatile uint8_t txbuf[TXBUF_SIZE];
static volatile uint8_t tx_head;                  /* next free slot: moved only with interrupts off */
static volatile uint8_t tx_tail;                  /* next byte to send: moved only by the UDRE interrupt */

/* Free bytes in the buffer (it keeps one slot empty, so at most 255). Call with interrupts off. */
uint8_t uart_room(void)
{
    return TXBUF_MASK - ((tx_head - tx_tail) & TXBUF_MASK);
}

/* Copy n bytes into the buffer, but only if all of them fit. Never waits. Call with interrupts off.
   Returns 1 if the bytes were queued, 0 if there was no room. */
uint8_t uart_queue(const void *data, uint8_t n)
{
    const uint8_t *p = data;
    if (uart_room() < n)
        return 0;
    uint8_t h = tx_head;                          /* work on a copy: with interrupts off nobody else moves it, */
    while (n--) {                                 /* and the loop does not re-read a volatile on every byte    */
        txbuf[h] = *p++;
        h = (h + 1) & TXBUF_MASK;
    }
    tx_head = h;
    UCSR0B |= (1 << UDRIE0);                      /* let the interrupt start draining the buffer */
    return 1;
}

void uart_print(const char *s)
{
    uint8_t n = 0;
    while (s[n] && n < TXBUF_MASK)                /* length of the line; 255 is the maximum (longer lines are cut) */
        n++;

    for (;;) {
        uint8_t sreg = SREG;                      /* remember whether interrupts were on */
        cli();                                    /* critical section: no task switch while we copy */
        uint8_t done = uart_queue(s, n);          /* the whole line, or nothing */
        SREG = sreg;
        if (done)
            return;
        /* full: interrupts were back on for a moment, so the UDRE interrupt could send; try again */
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
