/*
 * M8 test firmware: a DELIBERATE stack overflow.
 * Task 2 calls itself recursively, each level using ~30 bytes of its 128-byte stack, until it has gone
 * past the bottom of its stack. The kernel's canary check should notice at the next tick, print
 * "!STACK OVERFLOW T2" and halt. Tasks 1 and 3 are well-behaved counters.
 */
#include <avr/io.h>
#include "kernel.h"
#include "uart.h"

#ifndef LEVELS
#define LEVELS 5                  /* 6 calls x ~30 bytes = ~180 bytes > 128: overflows well past the guard bytes */
#endif

volatile uint16_t count[3];

static void task1(void) { for (;;) count[0]++; }
static void task3(void) { for (;;) count[2]++; }

__attribute__((noinline)) static void deep(uint8_t n)
{
    volatile uint8_t pad[24];                    /* the stack space this level uses */
    for (uint8_t i = 0; i < sizeof pad; i++)
        pad[i] = n;                              /* really write the whole frame, like a big local array */
    if (n)
        deep(n - 1);
    for (;;)
        count[1]++;                              /* stay at the deepest point, like a runaway task */
}

static void task2(void) { deep(LEVELS); }

int main(void)
{
    uart_init();
    kernel_init();
    kernel_create(task1);
    kernel_create(task2);
    kernel_create(task3);
    kernel_start();
}
