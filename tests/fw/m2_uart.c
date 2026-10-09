/* M2 test firmware: print one line over the UART, then stop. */
#include <avr/io.h>
#include "uart.h"

int main(void)
{
    uart_init();
    uart_puts("Hello, UART\r\n");
    for (;;) {}
}
