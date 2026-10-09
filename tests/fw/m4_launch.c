/* M4 test firmware: START a task by "resuming" its fake context, then let it return into task_exit. */
#include <avr/io.h>
#include "kernel.h"
#include "context.h"

static void task_one(void)
{
    uint8_t z;
    asm volatile("mov %0, r1" : "=r"(z));        /* what does r1 contain at task start? */
    GPIOR1 = (uint8_t)(z + 1);                   /* marker: becomes 1 only if r1 was 0 */
    GPIOR0 = 0x5A;                               /* marker: the task body ran */
    PINB = (1 << PB5);
}                                                /* returning lands in task_exit() */

int main(void)
{
    DDRB |= (1 << PB5);
    kernel_init();
    rr_current = kernel_create(task_one);
    asm volatile(
        "cli                \n\t"
        LOAD_SP_FROM_CURRENT
        RESTORE_CONTEXT
        "reti               \n\t"
        ::: "memory");
    __builtin_unreachable();
}
