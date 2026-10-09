/*
 * M7 demo: three tasks share one ATmega328P.
 *   T1 blinks PB5 (D13) every 250 ms, T2 blinks PB0 (D8) every 500 ms, T3 blinks PB1 (D9) every 1000 ms.
 * Each task also prints its name on the UART every 10th time it is given the CPU,
 * so the terminal shows T1 T2 T3 T1 T2 T3 ... (the Round-Robin rotation).
 */
#include <avr/io.h>
#include "kernel.h"
#include "uart.h"

#define PRINT_EVERY 10                          /* print once per this many turns of the task */

/* Common body of the three tasks. Never returns. */
static void blink_and_report(const char *line, uint8_t led_mask, uint16_t half_period_ms)
{
    uint32_t t0 = kernel_ticks();               /* time of the last toggle */
    uint16_t seen = kernel_switches();          /* switch counter at my last look */
    uint8_t turns = 0;                          /* how many turns I have had since the last print */

    for (;;) {
        uint16_t now = kernel_switches();
        if (now != seen) {                      /* the counter moved: I was switched out and back in */
            seen = now;
            if (++turns >= PRINT_EVERY) {
                turns = 0;
                uart_print(line);
            }
        }
        if (kernel_ticks() - t0 >= half_period_ms) {
            t0 += half_period_ms;               /* += (not "now") so lateness never accumulates */
            PINB = led_mask;                    /* writing 1 to PINB toggles the pin atomically */
        }
    }
}

static void task1(void) { blink_and_report("T1\r\n", (1 << PB5),  250); }
static void task2(void) { blink_and_report("T2\r\n", (1 << PB0),  500); }
static void task3(void) { blink_and_report("T3\r\n", (1 << PB1), 1000); }

int main(void)
{
    DDRB |= (1 << PB5) | (1 << PB0) | (1 << PB1);   /* the three LED pins are outputs */
    uart_init();
    uart_puts("RR-OS: 3 tasks, 10 ms slice\r\n");     /* polled print is fine: the kernel is not running yet */

    kernel_init();
    kernel_create(task1);
    kernel_create(task2);
    kernel_create(task3);
    kernel_start();                                 /* never returns */
}
