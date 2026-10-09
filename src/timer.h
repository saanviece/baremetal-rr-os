/* Timer1 as the 1 ms system tick. */
#ifndef TIMER_H
#define TIMER_H

#define TICK_HZ 1000UL                         /* ticks per second (1 ms per tick) */

void timer_start(void);   /* configure Timer1 CTC, enable its compare-A interrupt (does NOT call sei) */

#endif
