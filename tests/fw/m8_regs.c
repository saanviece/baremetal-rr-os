/*
 * M8 test firmware: do registers (and flags) survive context switches?
 *
 * Three tasks are written entirely in assembly, so the compiler cannot hide anything. Each task keeps
 * 32 values in registers r0..r31 as 16 MIRROR PAIRS: r<k> and r<k+16> always hold the same number.
 * Every loop pass it
 *   1. adds 1 to all 32 registers (so an old, stale value can never look right),
 *   2. sets the C and T flags and waits (a short counting loop that does not touch C/T),
 *      then checks both flags are still set; then does the same with both cleared,
 *   3. compares all 16 pairs. Any difference = a register was damaged by a switch.
 * On failure the task latches its bit in GPIOR0 and hangs. On success it toggles its own pin (heartbeat).
 * The tasks use different seeds, so a register leaking from one task into another would also show up.
 * r1 is used as ordinary data here (allowed: these tasks never run compiler code).
 */
#include <avr/io.h>
#include "kernel.h"

/* helper strings: compare mirror pair (a, b) and jump to the failure code if different */
#define CMP_PAIR(a, b) \
    "cp   r" #a ", r" #b "  \n\t" \
    "breq 1f                \n\t" \
    "rjmp fail%=            \n\t" \
    "1:                     \n\t"

/* wait with C=1,T=1 then check, wait with C=0,T=0 then check */
#define FLAG_WINDOW(SET_C, SET_T, CHECK_C, CHECK_T, W) \
    SET_C "                 \n\t" \
    SET_T "                 \n\t" \
    W "%=: dec r31          \n\t" /* dec changes Z,N,V,S but NOT C or T */ \
    "dec  r15               \n\t" \
    "brne " W "%=           \n\t" \
    CHECK_C "               \n\t" \
    "rjmp fail%=            \n\t" \
    "1:                     \n\t" \
    CHECK_T "               \n\t" \
    "rjmp fail%=            \n\t" \
    "1:                     \n\t"

#define REG_TASK(NAME, SEED, PINBIT, FAILBIT)                                     \
__attribute__((naked, used)) static void NAME(void)                               \
{                                                                                 \
    asm volatile(                                                                 \
        /* load r16..r31 with seed-dependent values, copy them into r0..r15 */    \
        "ldi r16, (%0 + 13*0)  & 255 \n\t" "ldi r17, (%0 + 13*1)  & 255 \n\t"     \
        "ldi r18, (%0 + 13*2)  & 255 \n\t" "ldi r19, (%0 + 13*3)  & 255 \n\t"     \
        "ldi r20, (%0 + 13*4)  & 255 \n\t" "ldi r21, (%0 + 13*5)  & 255 \n\t"     \
        "ldi r22, (%0 + 13*6)  & 255 \n\t" "ldi r23, (%0 + 13*7)  & 255 \n\t"     \
        "ldi r24, (%0 + 13*8)  & 255 \n\t" "ldi r25, (%0 + 13*9)  & 255 \n\t"     \
        "ldi r26, (%0 + 13*10) & 255 \n\t" "ldi r27, (%0 + 13*11) & 255 \n\t"     \
        "ldi r28, (%0 + 13*12) & 255 \n\t" "ldi r29, (%0 + 13*13) & 255 \n\t"     \
        "ldi r30, (%0 + 13*14) & 255 \n\t" "ldi r31, (%0 + 13*15) & 255 \n\t"     \
        "mov r0, r16  \n\t mov r1, r17  \n\t mov r2, r18  \n\t mov r3, r19  \n\t" \
        "mov r4, r20  \n\t mov r5, r21  \n\t mov r6, r22  \n\t mov r7, r23  \n\t" \
        "mov r8, r24  \n\t mov r9, r25  \n\t mov r10, r26 \n\t mov r11, r27 \n\t" \
        "mov r12, r28 \n\t mov r13, r29 \n\t mov r14, r30 \n\t mov r15, r31 \n\t" \
        "rjmp loop%=            \n\t"                                             \
        "fail%=:                \n\t"                                             \
        "sbi  0x1e, %2          \n\t" /* GPIOR0 bit: this task saw damage        */ \
        "hang%=: rjmp hang%=    \n\t"                                             \
        "loop%=:                \n\t"                                             \
        /* 1. add one to every register */                                        \
        "inc r0  \n\t inc r1  \n\t inc r2  \n\t inc r3  \n\t"                     \
        "inc r4  \n\t inc r5  \n\t inc r6  \n\t inc r7  \n\t"                     \
        "inc r8  \n\t inc r9  \n\t inc r10 \n\t inc r11 \n\t"                     \
        "inc r12 \n\t inc r13 \n\t inc r14 \n\t inc r15 \n\t"                     \
        "inc r16 \n\t inc r17 \n\t inc r18 \n\t inc r19 \n\t"                     \
        "inc r20 \n\t inc r21 \n\t inc r22 \n\t inc r23 \n\t"                     \
        "inc r24 \n\t inc r25 \n\t inc r26 \n\t inc r27 \n\t"                     \
        "inc r28 \n\t inc r29 \n\t inc r30 \n\t inc r31 \n\t"                     \
        /* 2. flags must survive a wait (r31/r15 are the pair used as the counter) */ \
        FLAG_WINDOW("sec", "set", "brcs 1f", "brts 1f", "wa")                     \
        FLAG_WINDOW("clc", "clt", "brcc 1f", "brtc 1f", "wb")                     \
        "ldi r31, (%0 + 13*15) & 255 \n\t"  /* counter pair is 0 now: reload both */ \
        "mov r15, r31           \n\t"                                             \
        /* 3. all 16 mirror pairs must still match */                             \
        CMP_PAIR(0, 16)  CMP_PAIR(1, 17)  CMP_PAIR(2, 18)  CMP_PAIR(3, 19)       \
        CMP_PAIR(4, 20)  CMP_PAIR(5, 21)  CMP_PAIR(6, 22)  CMP_PAIR(7, 23)       \
        CMP_PAIR(8, 24)  CMP_PAIR(9, 25)  CMP_PAIR(10, 26) CMP_PAIR(11, 27)      \
        CMP_PAIR(12, 28) CMP_PAIR(13, 29) CMP_PAIR(14, 30) CMP_PAIR(15, 31)      \
        "sbi  0x03, %1          \n\t" /* heartbeat: toggle my pin (PINB bit)     */ \
        "rjmp loop%=            \n\t"                                             \
        :: "i"(SEED), "i"(PINBIT), "i"(FAILBIT));                                 \
}

REG_TASK(reg_task_a, 0x11, PB5, 0)
REG_TASK(reg_task_b, 0x57, PB0, 1)
REG_TASK(reg_task_c, 0xA3, PB1, 2)

int main(void)
{
    DDRB |= (1 << PB5) | (1 << PB0) | (1 << PB1);
    GPIOR0 = 0;                                  /* no failure latched yet */
    kernel_init();
    kernel_create(reg_task_a);
    kernel_create(reg_task_b);
    kernel_create(reg_task_c);
    kernel_start();
}
