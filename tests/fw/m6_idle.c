/* M6 test firmware: both tasks return at once, so only the idle task is left. */
#include <avr/io.h>
#include "kernel.h"

static void quick(void) { }

int main(void)
{
    kernel_init();
    kernel_create(quick);
    kernel_create(quick);
    kernel_start();
}
