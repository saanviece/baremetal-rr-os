/* M4 test firmware: create two tasks, then stop at checkpoint() so the test can inspect SRAM. */
#include <avr/io.h>
#include "kernel.h"

static void task_a(void) { for (;;) {} }
static void task_b(void) { for (;;) {} }

__attribute__((noinline)) void checkpoint(void) { asm volatile("nop"); }

int main(void)
{
    kernel_init();
    kernel_create(task_a);
    kernel_create(task_b);
    checkpoint();                      /* simtest stops here (-b checkpoint) */
    for (;;) {}
}
