/* SPDX-License-Identifier: Apache-2.0 */
.syntax unified
.cpu cortex-m33
.thumb
.section .vectors,"a"
.word 0x28001000
.word start + 1
.word 0
.word hardfault + 1
.section .fault,"ax"
hardfault:
    /* CRC instruction failure must reach a real exception handler. */
    sub sp, #8
    movw r3, #0x26
    movt r3, #2
    str r3, [sp]
    movs r2, #2
    str r2, [sp, #4]
    mov r1, sp
    movs r0, #0x20
    bkpt #0xab
    b .
.section .text,"ax"
.global start
start:
    movw r0, #0x1234
#ifdef BAD_RESULT
    movw r1, #0x1235
#else
    movw r1, #0x1234
#endif
    cmp r0, r1
    bne fail
    movs r2, #0
    b finish
fail:
    movs r2, #1
finish:
    sub sp, #8
    movw r3, #0x26
    movt r3, #2
    str r3, [sp]
    str r2, [sp, #4]
    mov r1, sp
    movs r0, #0x20
    bkpt #0xab
loop:
    b loop
