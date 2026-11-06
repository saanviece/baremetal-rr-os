/* The link to the RR-OS Scope dashboard: one binary "switch frame" per task change, only after the dashboard asks. */
#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include "kernel.h"

extern volatile uint8_t trace_on;     /* 1 after the dashboard sent 'T', 0 after 't' (and after every reset) */

void trace_init(void);                /* turn on the UART receiver, so the dashboard's command can arrive */
void trace_switch(const tcb_t *from, const tcb_t *to, uint32_t tick, uint16_t nswitch);  /* called by kernel_tick */

#endif
