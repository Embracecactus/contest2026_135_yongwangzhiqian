/* SPDX-License-Identifier: Apache-2.0 */
/* First instruction reached by BL1 after it has copied the valid BL2 payload. */
    .syntax unified
    .cpu cortex-m33
    .thumb
    .section .vectors, "a", %progbits
    .global _vectors
_vectors:
    /* The reserved BL2 execution/copy window is 128 KiB.  The current image
     * is smaller, but using the full-window top keeps the ABI valid when a
     * signed BL2 grows past the 64 KiB bank boundary. */
    .word 0x28040000
    .word bk7258_bl2_reset + 1
    .rept 62
    .word bk7258_bl2_panic + 1
    .endr

    .section .text.start, "ax", %progbits
    .thumb_func
    .global bk7258_bl2_reset
bk7258_bl2_reset:
    cpsid i

    /* BL1 intentionally enters BL2 with the public CP handoff limit
     * (0x2802f800).  That leaves only 2 KiB below this BL2 vector's MSP at
     * 0x28030000, which is unsuitable for MCUboot's bounded verification
     * stack.  This board-owned SRAM BL2 owns 0x28020000..0x2802ffff while it
     * runs, so widen MSPLIM before clearing .bss or entering C.  The final
     * CP handoff restores the public lower limit. */
    ldr r0, =0x28020000
    msr msplim, r0
    dsb sy
    isb sy

    ldr r0, =__bss_start__
    ldr r1, =__bss_end__
    movs r2, #0
1:
    cmp r0, r1
    bcs 2f
    str r2, [r0], #4
    b 1b
2:
    bl bk7258_bl2_main
3:
    b 3b
