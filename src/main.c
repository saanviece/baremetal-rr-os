/* M1: bare-metal LED blink on PB5 (Arduino D13) using direct register writes. */
#include <avr/io.h>

#define LED  PB5                      /* PB5 = Arduino pin 13 = on-board LED "L" */

int main(void)
{
    DDRB |= (1 << LED);               /* make PB5 an OUTPUT */
    for (;;) {
        PINB = (1 << LED);            /* writing 1 to PINB toggles PORTB bit: one atomic instruction */
        for (volatile uint32_t i = 0; i < 228000UL; i++) {
            /* empty busy-wait: ~35 cycles per pass x 228000 = ~0.5 s. M3 replaces this with the timer tick */
        }
    }
}
