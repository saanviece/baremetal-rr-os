/* Minimal preemptive Round-Robin kernel for the ATmega328P. */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "kernel.h"
#include "timer.h"
#include "context.h"

tcb_t *volatile rr_current;                     /* task that owns the CPU right now */

static tcb_t   tcbs[MAX_TASKS + 1];             /* [0] = idle task, [1..MAX_TASKS] = user tasks */
static uint8_t stacks[MAX_TASKS + 1][STACK_SIZE];
static uint8_t ntasks;                          /* user tasks created so far */
static volatile uint32_t ticks;                 /* 1 ms ticks since start */

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

/* ---- the tick: called from the timer ISR with interrupts OFF, after the context is saved ---- */

__attribute__((used)) void kernel_tick(void)
{
    ticks++;
    rr_current = rr_current->next;              /* M5: switch to the next task on EVERY tick */
}

/*
 * Timer1 compare-A interrupt = the 1 ms tick = the context switch.
 * ISR_NAKED: the compiler adds nothing; every instruction here is mine (see docs/M5_context_switch.md).
 */
ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
    asm volatile(
        SAVE_CONTEXT                            /* freeze the running task                */
        SAVE_SP_TO_CURRENT                      /* tcb->sp := SP                          */
        "call kernel_tick       \n\t"           /* C code decides who runs next           */
        LOAD_SP_FROM_CURRENT                    /* SP := new task's tcb->sp  (the switch) */
        RESTORE_CONTEXT                         /* thaw the new task                      */
        "reti                   \n\t"           /* pop PC, set I-bit: new task continues  */
    );
}

void kernel_start(void)
{
    rr_current = &tcbs[1];                      /* first user task goes first */
    timer_start();
    asm volatile(
        "cli                    \n\t"
        LOAD_SP_FROM_CURRENT                    /* only the thaw half: nothing to save yet */
        RESTORE_CONTEXT
        "reti                   \n\t"
        ::: "memory");
    __builtin_unreachable();
}

uint32_t kernel_ticks(void)
{
    uint8_t sreg = SREG;                        /* 4 bytes can't be read in one step: block the ISR */
    cli();
    uint32_t t = ticks;
    SREG = sreg;                                /* restore the previous I-bit (not a blind sei) */
    return t;
}
