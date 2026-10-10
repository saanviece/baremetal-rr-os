/* Timer1 as the 1 ms system tick. */
#include <avr/io.h>
#include "timer.h"

#define PRESCALER 64UL
/* Timer counts 0..OCR1A then resets, so one period = OCR1A + 1 counts. */
#define OCR_VALUE (F_CPU / PRESCALER / TICK_HZ - 1)   /* 16e6 / 64 / 1000 - 1 = 249 */

void timer_start(void)
{
    TCCR1A = 0;                                /* no pin output; waveform mode bits WGM11:10 = 0 */
    TCNT1  = 0;                                /* start counting from 0 */
    TIFR1  = (1 << OCF1A);                     /* clear a stale compare flag (writing 1 clears it) */
    OCR1A  = OCR_VALUE;                        /* TOP value: 249 -> 250 counts -> 1 ms */
    TIMSK1 = (1 << OCIE1A);                    /* interrupt when TCNT1 == OCR1A */
    /* WGM12 = CTC mode (clear on compare A). CS11|CS10 = clock / 64. Writing this starts the timer. */
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);
}
