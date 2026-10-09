/*
 * Assembly snippets that move a task's CPU state between the CPU and its stack.
 * They are plain strings so they can be pasted inside asm volatile(...) blocks.
 *
 * Stack content of a paused task (high address first, i.e. in the order it was pushed):
 *     PC low, PC high   <- pushed by the hardware when the interrupt fired
 *     r0
 *     SREG              <- saved through r0
 *     r1
 *     r2 ... r31
 *   saved SP points just below r31.
 */
#ifndef CONTEXT_H
#define CONTEXT_H

/* Load SP from the TCB that rr_current points to (TCB offset 0 = saved sp). Uses Z, r24, r25. */
#define LOAD_SP_FROM_CURRENT \
    "lds  r30, rr_current       \n\t"  /* Z = rr_current (low byte)          */ \
    "lds  r31, rr_current+1     \n\t"  /* Z = rr_current (high byte)         */ \
    "ld   r24, Z                \n\t"  /* r24 = tcb->sp low                  */ \
    "ldd  r25, Z+1              \n\t"  /* r25 = tcb->sp high                 */ \
    "out  __SP_H__, r25         \n\t"  /* SP := tcb->sp                      */ \
    "out  __SP_L__, r24         \n\t"

/* Undo the pushes, in exactly the opposite order. After this only the 2-byte PC is left on the stack. */
#define RESTORE_CONTEXT \
    "pop  r31 \n\t pop  r30 \n\t pop  r29 \n\t pop  r28 \n\t" \
    "pop  r27 \n\t pop  r26 \n\t pop  r25 \n\t pop  r24 \n\t" \
    "pop  r23 \n\t pop  r22 \n\t pop  r21 \n\t pop  r20 \n\t" \
    "pop  r19 \n\t pop  r18 \n\t pop  r17 \n\t pop  r16 \n\t" \
    "pop  r15 \n\t pop  r14 \n\t pop  r13 \n\t pop  r12 \n\t" \
    "pop  r11 \n\t pop  r10 \n\t pop  r9  \n\t pop  r8  \n\t" \
    "pop  r7  \n\t pop  r6  \n\t pop  r5  \n\t pop  r4  \n\t" \
    "pop  r3  \n\t pop  r2  \n\t pop  r1  \n\t"                \
    "pop  r0                    \n\t"  /* this is the saved SREG value        */ \
    "out  __SREG__, r0          \n\t"  /* put the flags back                  */ \
    "pop  r0                    \n\t"  /* the real r0                         */

#endif
