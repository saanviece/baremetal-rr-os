/*
 * The link to the RR-OS Scope dashboard (the web page in the visualizer folder).
 *
 * The dashboard sends the letter 'T'. From then on, at every task change, the kernel adds one
 * "switch frame" of 141 bytes to the serial output, between the text lines:
 *
 *   byte  0        0x80 + id of the task that gets the CPU now (text never has bit 7 set: this marks a frame)
 *   bytes 1..4     tick = kernel time in ms, lowest byte first
 *   bytes 5..6     number of switches so far, lowest byte first
 *   byte  7        id of the task that was just paused
 *   byte  8        PORTB at this moment (the three LED pins)
 *   bytes 9..10    the paused task's saved stack pointer (its tcb->sp)
 *   bytes 11..12   the address of its stack (tcb->stack_low)
 *   bytes 13..140  its whole stack, 128 bytes from stack_low upward: canary, unused bytes, its own data,
 *                  and the 35 bytes the tick interrupt just saved (r31..r2, r1, SREG, r0, PC)
 *
 * So the dashboard sees the real registers, flags, stack and LEDs of the board at every switch.
 * The Arduino IDE's Serial Monitor never sends 'T', so it only ever shows the text lines.
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "trace.h"
#include "uart.h"

#define HEAD_LEN 13

volatile uint8_t trace_on;                    /* 0 at reset: no frames until the dashboard asks */
static uint8_t head[HEAD_LEN];                /* static, not a local: it must not take room on the paused task's stack */

void trace_init(void)
{
    UCSR0B |= (1 << RXEN0) | (1 << RXCIE0);   /* receiver on, and an interrupt for every byte that arrives */
}

ISR(USART_RX_vect)                            /* a byte arrived from the PC */
{
    uint8_t c = UDR0;                         /* reading UDR0 also clears the interrupt flag */
    if (c == 'T')
        trace_on = 1;                         /* dashboard connected: start sending frames */
    else if (c == 't')
        trace_on = 0;                         /* dashboard leaving: stop */
}

/*
 * Called by kernel_tick() with interrupts off, right after it chose `to` and before the assembly loads its stack.
 * It never waits: if the send buffer cannot take the whole frame now, this frame is skipped.
 * The dashboard notices from the switch number that one is missing.
 */
void trace_switch(const tcb_t *from, const tcb_t *to, uint32_t tick, uint16_t nswitch)
{
    uint16_t sp = (uint16_t)from->sp, low = (uint16_t)from->stack_low;
    head[0]  = 0x80 | to->id;                 /* fill the head first: then few values must survive the calls below, */
    head[1]  = tick;        head[2] = tick >> 8;  head[3] = tick >> 16;  head[4] = tick >> 24;   /* so this */
    head[5]  = nswitch;     head[6] = nswitch >> 8;                       /* function pushes few registers */
    head[7]  = from->id;
    head[8]  = PORTB;
    head[9]  = sp;          head[10] = sp >> 8;
    head[11] = low;         head[12] = low >> 8;
    if (uart_room() < HEAD_LEN + STACK_SIZE)
        return;
    uart_queue(head, HEAD_LEN);
    uart_queue(from->stack_low, STACK_SIZE);  /* the paused task's stack, exactly as it is in RAM now */
}
