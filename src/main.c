/*
 * M7 demo: three tasks share one ATmega328P.
 *   T1 blinks PB5 (D13) every 250 ms, T2 blinks PB0 (D8) every 500 ms, T3 blinks PB1 (D9) every 1000 ms.
 * Every 10th time a task is given the CPU it prints one line in plain words, e.g.
 *       0.270 s | T1 has the CPU | turn     10 | D13 LED on
 * so the serial monitor shows the Round-Robin rotation (T1, T2, T3, 10 ms apart) and what each task does.
 */
#include <avr/io.h>
#include "kernel.h"
#include "uart.h"
#include "trace.h"

#define PRINT_EVERY 10                          /* print once per this many turns of the task */

/*
 * Everything one demo task needs. Each task has its own line buffer: a task can be paused
 * halfway through building its line, and the next task must not write over it.
 */
typedef struct {
    const char *name;                           /* "T1" */
    const char *led;                            /* Arduino pin of its LED, e.g. "D13" */
    uint8_t     mask;                           /* that pin's bit in PORTB */
    uint16_t    half_period_ms;                 /* the LED toggles this often */
    char        line[72];                       /* the longest possible line is 64 characters + the '\0' */
} demo_task_t;

static demo_task_t demo[3] = {
    { .name = "T1", .led = "D13", .mask = (1 << PB5), .half_period_ms =  250 },
    { .name = "T2", .led = "D8",  .mask = (1 << PB0), .half_period_ms =  500 },
    { .name = "T3", .led = "D9",  .mask = (1 << PB1), .half_period_ms = 1000 },
};

/* The start-up text. `__flash` keeps it in flash memory: plain strings are copied into the 2 KB of RAM at reset. */
static const __flash char banner[] =
    "\r\nRR-OS: a Round-Robin OS running 3 tasks on one CPU\r\n"
    "Every 1 ms a timer interrupt runs the kernel, and every\r\n"
    "10 ms the kernel pauses the running task and starts\r\n"
    "the next one: T1 -> T2 -> T3 -> T1 -> ...\r\n"
    "T1 blinks D13 (250 ms), T2 D8 (500 ms), T3 D9 (1 s)\r\n"
    "Every 10th turn, a task prints a line:\r\n\r\n";

/* Append string s at p. Returns the new end. */
static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

/* Append v in decimal, right-aligned in at least `width` characters (filled on the left with `pad`). */
static char *put_dec(char *p, uint32_t v, uint8_t width, char pad)
{
    char digits[10];                            /* a uint32_t has at most 10 decimal digits */
    uint8_t n = 0;
    do {
        digits[n++] = '0' + v % 10;             /* lowest digit first ... */
        v /= 10;
    } while (v);
    for (; width > n; width--)
        *p++ = pad;
    while (n)
        *p++ = digits[--n];                     /* ... so copy them out in reverse */
    return p;
}

/* Build one line like "   12.340 s | T2 has the CPU | turn    410 | D8 LED on" and send it. */
static void report(demo_task_t *t, uint32_t turn)
{
    uint32_t ms = kernel_ticks();               /* time since the kernel started */
    char *p = t->line;
    p = put_dec(p, ms / 1000, 5, ' ');          /* whole seconds */
    *p++ = '.';
    p = put_dec(p, ms % 1000, 3, '0');          /* milliseconds, always 3 digits */
    p = put_str(p, " s | ");
    p = put_str(p, t->name);
    p = put_str(p, " has the CPU | turn ");
    p = put_dec(p, turn, 6, ' ');
    p = put_str(p, " | ");
    p = put_str(p, t->led);
    p = put_str(p, (PORTB & t->mask) ? " LED on\r\n" : " LED off\r\n");
    *p = '\0';
    uart_print(t->line);                        /* task-safe: the whole line goes out in one piece */
}

/* Common body of the three tasks. Never returns. */
static void blink_and_report(demo_task_t *t)
{
    uint32_t t0 = kernel_ticks();               /* time of the last toggle */
    uint16_t seen = kernel_switches();          /* switch counter at my last look */
    uint32_t turn = 1;                          /* I am running, so this is my first turn */

    for (;;) {
        uint16_t now = kernel_switches();
        uint8_t new_turn = (now != seen);       /* the counter moved: I was switched out and back in */
        if (new_turn) {
            seen = now;
            turn++;
        }
        if (kernel_ticks() - t0 >= t->half_period_ms) {
            t0 += t->half_period_ms;            /* += (not "now") so lateness never accumulates */
            PINB = t->mask;                     /* writing 1 to PINB toggles the pin atomically */
        }
        /* Report only after the toggle check of this same loop pass: the task may have been paused
           anywhere in the loop, and the line must show the LED as it is at this moment. */
        if (new_turn && turn % PRINT_EVERY == 0)
            report(t, turn);
    }
}

static void task1(void) { blink_and_report(&demo[0]); }
static void task2(void) { blink_and_report(&demo[1]); }
static void task3(void) { blink_and_report(&demo[2]); }

int main(void)
{
    DDRB |= (1 << PB5) | (1 << PB0) | (1 << PB1);   /* the three LED pins are outputs */
    uart_init();
    trace_init();                                   /* listen for the dashboard's 'T' (trace.c) */
    uart_puts_flash(banner);                        /* polled print is fine: the kernel is not running yet */

    kernel_init();
    kernel_create(task1);
    kernel_create(task2);
    kernel_create(task3);
    kernel_start();                                 /* never returns */
}
