/* M6 test firmware: task 2 returns after a few loops (becomes DEAD); tasks 1 and 3 run forever. */
#include <avr/io.h>
#include "kernel.h"

volatile uint16_t count[3];

static void task1(void) { for (;;) count[0]++; }
static void task2(void) { for (uint8_t i = 0; i < 100; i++) count[1]++; }   /* then returns */
static void task3(void) { for (;;) count[2]++; }

int main(void)
{
    kernel_init();
    kernel_create(task1);
    kernel_create(task2);
    kernel_create(task3);
    kernel_start();
}
