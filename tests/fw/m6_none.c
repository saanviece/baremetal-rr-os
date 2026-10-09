/* M6 test firmware: no user task at all - the kernel must still run safely (idle only). */
#include <avr/io.h>
#include "kernel.h"

int main(void)
{
    kernel_init();
    kernel_start();
}
