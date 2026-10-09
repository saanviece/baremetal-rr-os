/* M5 test firmware: three tasks that only count. The switch happens on every tick. */
#include <avr/io.h>
#include "kernel.h"

volatile uint16_t count[3];

static void task1(void) { for (;;) count[0]++; }
static void task2(void) { for (;;) count[1]++; }
static void task3(void) { for (;;) count[2]++; }

int main(void)
{
    kernel_init();
    kernel_create(task1);
    kernel_create(task2);
    kernel_create(task3);
    kernel_start();
}
