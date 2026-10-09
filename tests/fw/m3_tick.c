/* M3 test firmware: a tick counter driven by Timer1, plus a 0.5 s blink on PB5 done in main. */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "timer.h"

volatile uint32_t ticks;                       /* incremented once per millisecond */

ISR(TIMER1_COMPA_vect)                         /* runs every 1 ms */
{
    ticks++;
}

static uint32_t read_ticks(void)               /* 4-byte read must not be split by the ISR */
{
    uint8_t sreg = SREG;
    cli();
    uint32_t t = ticks;
    SREG = sreg;
    return t;
}

int main(void)
{
    DDRB |= (1 << PB5);
    timer_start();
    sei();                                     /* global interrupt enable */
    uint32_t last = 0;
    for (;;) {
        if (read_ticks() - last >= 500) {
            last += 500;
            PINB = (1 << PB5);
        }
    }
}
