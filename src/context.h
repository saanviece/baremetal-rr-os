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

/* Freeze the running task: push r0, SREG, r1, r2..r31 (the hardware already pushed the PC). */
#define SAVE_CONTEXT \
    "push r0                    \n\t"  /* r0 is my scratch register: save it first      */ \
    "in   r0, __SREG__          \n\t"  /* copy the flags (SREG) into r0                 */ \
    "push r0                    \n\t"  /* save the flags                                */ \
    "push r1                    \n\t"  /* save r1 ...                                   */ \
    "clr  r1                    \n\t"  /* ... then make r1 = 0 again: C code needs that */ \
    "push r2  \n\t push r3  \n\t push r4  \n\t push r5  \n\t" \
    "push r6  \n\t push r7  \n\t push r8  \n\t push r9  \n\t" \
    "push r10 \n\t push r11 \n\t push r12 \n\t push r13 \n\t" \
    "push r14 \n\t push r15 \n\t push r16 \n\t push r17 \n\t" \
    "push r18 \n\t push r19 \n\t push r20 \n\t push r21 \n\t" \
    "push r22 \n\t push r23 \n\t push r24 \n\t push r25 \n\t" \
    "push r26 \n\t push r27 \n\t push r28 \n\t push r29 \n\t" \
    "push r30 \n\t push r31 \n\t"

/* Remember where this task's stack ended: tcb->sp := SP. Uses Z (already saved), r24, r25. */
#define SAVE_SP_TO_CURRENT \
    "lds  r30, rr_current       \n\t"  /* Z = address of the running task's TCB */ \
    "lds  r31, rr_current+1     \n\t" \
    "in   r24, __SP_L__         \n\t"  /* read the stack pointer                */ \
    "in   r25, __SP_H__         \n\t" \
    "st   Z, r24                \n\t"  /* tcb->sp low                           */ \
    "std  Z+1, r25              \n\t"  /* tcb->sp high                          */

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
