/* Minimal preemptive Round-Robin kernel for the ATmega328P. */
#include <avr/io.h>
#include <avr/interrupt.h>
#include "kernel.h"
#include "timer.h"
#include "context.h"
#include "uart.h"

tcb_t *volatile rr_current;                     /* task that owns the CPU right now */

static tcb_t   tcbs[MAX_TASKS + 1];             /* [0] = idle task, [1..MAX_TASKS] = user tasks */
static uint8_t stacks[MAX_TASKS + 1][STACK_SIZE];
static uint8_t ntasks;                          /* user tasks created so far */
static volatile uint32_t ticks;                 /* 1 ms ticks since start */
static volatile uint16_t switches;              /* how many times the CPU changed task */
static uint8_t slice_left = SLICE_TICKS;        /* ticks left in the running task's slice */
static tcb_t  *rr_last;                         /* the user task that had the last turn (ring position) */
volatile uint8_t kernel_fault;                  /* 0 = fine, else (id of the task that overflowed) + 1 */

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

/* Runs only when no user task is READY. `sleep` halts the CPU until the next interrupt (idle mode). */
static void idle_task(void)
{
    SMCR = (1 << SE);                           /* allow the sleep instruction (mode bits 0 = idle) */
    for (;;)
        asm volatile("sleep");
}

void kernel_init(void)
{
    ntasks = 0;
    rr_last = 0;
    tcbs[0].id        = 0;                      /* the idle task is not in the ring */
    tcbs[0].state     = TASK_READY;
    tcbs[0].stack_low = stacks[0];
    tcbs[0].sp        = build_frame(stacks[0], idle_task);
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

/* Stack overflow: stop everything, say so on the UART and blink the LED fast. Never returns. */
__attribute__((noreturn)) static void kernel_panic(const tcb_t *t)
{
    cli();
    kernel_fault = t->id + 1;                   /* +1 so that task 0 (idle) is also distinguishable from "no fault" */
    uart_puts("\r\n!STACK OVERFLOW T");
    uart_putc('0' + t->id);
    uart_puts("\r\n");
    DDRB |= (1 << PB5);
    for (;;) {
        PINB = (1 << PB5);
        for (volatile uint16_t i = 0; i < 20000; i++) {}
    }
}

/* Is the stack of task t still intact? (guard bytes untouched and SP not inside the guard) */
static uint8_t stack_ok(const tcb_t *t)
{
    for (uint8_t i = 0; i < CANARY_LEN; i++)
        if (t->stack_low[i] != STACK_FILL)
            return 0;
    return t->sp >= t->stack_low + CANARY_LEN;
}

/* Round-Robin: go once around the ring, starting after the last user task that ran. */
static tcb_t *pick_next(void)
{
    tcb_t *t = rr_last;
    if (t) {
        do {
            t = t->next;
            if (t->state == TASK_READY) {
                rr_last = t;
                return t;
            }
        } while (t != rr_last);                 /* stop after checking rr_last itself */
    }
    return &tcbs[0];                            /* nobody is ready: run the idle task */
}

/* Called from the timer ISR every 1 ms, with interrupts OFF and the context already saved. */
__attribute__((used)) void kernel_tick(void)
{
    ticks++;
    if (!stack_ok(rr_current))
        kernel_panic(rr_current);
    if (--slice_left && rr_current->state == TASK_READY)
        return;                                 /* slice not over: same task keeps the CPU */
    slice_left = SLICE_TICKS;
    tcb_t *next = pick_next();
    if (next != rr_current) {
        rr_current = next;                      /* the assembly after this call performs the switch */
        switches++;
    }
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
    rr_last = ntasks ? &tcbs[1] : 0;            /* first user task goes first ... */
    rr_current = ntasks ? &tcbs[1] : &tcbs[0];  /* ... or idle if there is none */
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

uint16_t kernel_switches(void)
{
    uint8_t sreg = SREG;
    cli();
    uint16_t n = switches;
    SREG = sreg;
    return n;
}
