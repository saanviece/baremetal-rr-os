/* Minimal preemptive Round-Robin kernel for the ATmega328P. */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "kernel.h"

tcb_t *volatile rr_current;                     /* task that owns the CPU right now */

static tcb_t   tcbs[MAX_TASKS + 1];             /* [0] = idle task, [1..MAX_TASKS] = user tasks */
static uint8_t stacks[MAX_TASKS + 1][STACK_SIZE];
static uint8_t ntasks;                          /* user tasks created so far */

/* Runs if a task function ever returns: mark it dead and wait to be preempted for good. */
__attribute__((used, noinline)) static void task_exit(void)
{
    rr_current->state = TASK_DEAD;
    for (;;) {}
}

/*
 * Fill a stack and build the fake "paused task" frame on top of it (see docs/M4_tcb_stack.md).
 * Bytes are written in the order the CPU would have pushed them. Returns the stack pointer value
 * the task has to be resumed with.
 */
static uint8_t *build_frame(uint8_t *stack, void (*fn)(void))
{
    for (uint16_t i = 0; i < STACK_SIZE; i++)
        stack[i] = STACK_FILL;                  /* canary + high-water-mark pattern */

    uint8_t *sp = stack + STACK_SIZE - 1;       /* top of the stack = highest address */
    uint16_t exit_pc  = (uint16_t)task_exit;    /* function pointers on AVR are WORD addresses */
    uint16_t entry_pc = (uint16_t)fn;

    *sp-- = exit_pc & 0xFF;                     /* return address of the task: low byte first, */
    *sp-- = exit_pc >> 8;                       /* then high byte (lower address)              */
    *sp-- = entry_pc & 0xFF;                    /* the "pushed PC" that reti will pop          */
    *sp-- = entry_pc >> 8;
    for (uint8_t i = 0; i < 33; i++)            /* r0, SREG, r1 .. r31 = 2 + 31 bytes, all 0   */
        *sp-- = 0;                              /* (r1 = 0 is required by avr-gcc)             */
    return sp;                                  /* first free byte below r31 */
}

void kernel_init(void)
{
    ntasks = 0;
}

tcb_t *kernel_create(void (*fn)(void))
{
    if (ntasks >= MAX_TASKS)
        return 0;
    uint8_t id = ++ntasks;                      /* user tasks are 1..MAX_TASKS */
    tcb_t *t = &tcbs[id];

    t->id        = id;
    t->state     = TASK_READY;
    t->stack_low = stacks[id];
    t->sp        = build_frame(stacks[id], fn);

    t->next = &tcbs[1];                         /* close the circle: last task points to the first */
    if (id > 1)
        tcbs[id - 1].next = t;                  /* previous last task now points to the new one */
    return t;
}
