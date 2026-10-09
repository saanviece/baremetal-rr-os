/* Minimal preemptive Round-Robin kernel for the ATmega328P. */
#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

#define MAX_TASKS    4      /* user tasks; the idle task is extra */
#define STACK_SIZE   128    /* bytes of stack per task */
#define CANARY_LEN   8      /* guard bytes at the bottom of every stack */
#define STACK_FILL   0xA5   /* pattern the unused stack is filled with (also the canary value) */
#define SLICE_TICKS  10     /* time slice = this many 1 ms ticks */

#define TASK_READY   1      /* may run */
#define TASK_DEAD    2      /* its function returned; never scheduled again */

/* Task Control Block. `sp` MUST stay first: the assembly code uses offset 0. */
typedef struct tcb {
    uint8_t    *sp;         /* saved stack pointer while the task is not running */
    uint8_t     state;      /* TASK_READY or TASK_DEAD */
    uint8_t     id;         /* 0 = idle, 1.. = user tasks */
    struct tcb *next;       /* circular list of user tasks */
    uint8_t    *stack_low;  /* lowest byte of this task's stack (canary lives here) */
} tcb_t;

extern tcb_t *volatile rr_current;   /* the task that owns the CPU now */

void     kernel_init(void);                   /* call once, before creating tasks */
tcb_t   *kernel_create(void (*fn)(void));     /* add a task; returns 0 if the table is full */

#endif
