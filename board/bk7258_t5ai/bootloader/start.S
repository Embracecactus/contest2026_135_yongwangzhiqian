/*
 * start.S - BK7258 Tier-1 bootloader : asm trampoline + hardened jump epilogue.
 *
 * Layout in the 0x10000-byte logical bootloader slot (FLASH @ 0x02000000):
 *   0x000 .. 0x0FF  initial vector table (64 entries)
 *   0x100 .. 0x107  bootloader magic  "BK7236\x10\x00"
 *   0x108 .. 0x1FF  vector table continuation (62 entries)
 *   0x200 ..        Reset_Handler: cache-safe reset entry, verified board
 *                   init, C main, then the official-style jump epilogue.
 *
 * The board-specific WDT/UART register values remain reproduced verbatim from
 * the known-good minimal bootloader.  The cache/MPU operations are recovered
 * from the official BK7258 normal bootloader v3.1.1.9; see boot_runtime.c.
 * App-header validation, FAL parsing and UART logging live in boot_main.c.
 *
 * Reference: docs/bk7258-t5ai/bootloader/bk-official-bootloader-reverse.md S2.7.
 */

    .syntax unified
    .cpu cortex-m33
    .thumb

/* ------------------------------------------------------------------------ */
/* 0x000: initial vector table (64 words = 0x100 bytes)                      */
/* ------------------------------------------------------------------------ */
    .section .text.startup,"ax"
    .org 0x000
    .word 0x2809F700              /* [0] initial MSP (verified SRAM top)    */
    .word Reset_Handler + 1       /* [1] Reset (Thumb)                       */
    .word NMI_Handler + 1         /* [2] NMI                                 */
    .word default_handler + 1     /* [3] HardFault                           */
    .rept 60                      /* [4..63] MemMgmt..IRQ63 -> default       */
    .word default_handler + 1
    .endr

/* ------------------------------------------------------------------------ */
/* 0x100: bootloader magic consumed by the flash toolchain / packer.        */
/* ------------------------------------------------------------------------ */
    .org 0x100
    .ascii "BK7236"
    .byte 0x10, 0x00

/* ------------------------------------------------------------------------ */
/* 0x108: vector table continuation (62 words = 0xF8 bytes -> ends 0x200).   */
/* ------------------------------------------------------------------------ */
    .org 0x108
    .rept 62
    .word Reset_Handler + 1
    .endr

/* ------------------------------------------------------------------------ */
/* 0x200: Reset_Handler.                                                     */
/* ------------------------------------------------------------------------ */
    .org 0x200
    .global Reset_Handler
    .thumb_func
Reset_Handler:
    cpsid i

    /*
     * Always invalidate I-cache before executing any mutable boot image.
     *
     * This must be in the first reset cache line rather than in C.  BK7258
     * downloader/SYSRESETREQ resets can leave CPU0's private I-cache alive;
     * without this sequence the core can execute a previously flashed
     * bootloader even though XIP reads already show the new bytes.
     */
    ldr  r0, =0xE000EF50     /* SCB_ICIALLU */
    movs r1, #0
    dsb  sy
    str  r1, [r0]
    dsb  sy
    isb  sy

    /*
     * Official v3.1.1.9 reset/runtime normalization:
     *   - early flash/SYS register repair;
     *   - VTOR + ITCM/DTCM setup;
     *   - I-cache enable and D-cache invalidate;
     *   - hold CPU1/CPU2 reset and power them down.
     */
    bl   boot_reset_prepare

    /* Enable SWD/debug. (verbatim) */
    ldr  r0, =0x440100E0
    movs r1, #1
    mvns r1, r1
    str  r1, [r0]
    str  r1, [r0, #4]

    /* Disable/feed AON WDT and APB WDT with the required key sequence. (verbatim) */
    ldr  r0, =0x44000600     /* AON_WDT_CTRL */
    ldr  r1, =0x005A0000     /* key1, period=0 */
    str  r1, [r0]
    ldr  r1, =0x00A50000     /* key2, period=0 */
    str  r1, [r0]

    ldr  r0, =0x44800008     /* WDT global_ctrl */
    movs r1, #1
    str  r1, [r0]
    ldr  r0, =0x44800010     /* WDT ctrl */
    ldr  r1, =0x005A0000     /* key1, period=0 */
    str  r1, [r0]
    ldr  r1, =0x00A50000     /* key2, period=0 */
    str  r1, [r0]

    /* Configure GPIO0/1 like the known-good UART1 probe state. (verbatim) */
    ldr  r0, =0x440100C0     /* GPIO0..7 peripheral mode */
    movs r1, #0
    str  r1, [r0]
    ldr  r0, =0x44000400     /* GPIO0: UART_ID_1 TXD */
    ldr  r1, =0x00000078
    str  r1, [r0]
    ldr  r0, =0x44000404     /* GPIO1: UART_ID_1 RXD */
    ldr  r1, =0x00150078
    str  r1, [r0]

    /* Also preserve the known-good GPIO10/11 boot UART state. (verbatim) */
    ldr  r0, =0x440100C4     /* GPIO8..15 peripheral mode */
    movs r1, #0
    str  r1, [r0]
    ldr  r0, =0x44000428     /* GPIO10 */
    ldr  r1, =0x0015007C
    str  r1, [r0]
    ldr  r0, =0x4400042C     /* GPIO11 */
    ldr  r1, =0x00000078
    str  r1, [r0]

    /* Enable UART1 clock. (verbatim) */
    ldr  r0, =0x44010030
    ldr  r1, [r0]
    movs r2, #0x400
    orrs r1, r1, r2
    str  r1, [r0]

    /* Configure UART1 like the known-good probe state. (verbatim) */
    ldr  r0, =0x45830008     /* UART1 global_ctrl */
    movs r1, #1
    str  r1, [r0]
    ldr  r0, =0x45830010     /* clk_div=0x37, 8-bit data, TX enable */
    ldr  r1, =0x00003719
    str  r1, [r0]

    /*
     * Hand off to C: parse the FAL partition table, validate the app header
     * and emit UART logs. Returns app logical vector address in r0. On
     * validation failure c_main prints BAD + reason and loops forever.
     */
    bl   c_main
    mov  r4, r0

    /*
     * Match the official jump_to_app() cache/MPU contract before changing
     * VTOR/MSP.  This disables D-cache, cleans+invalidates every set/way and
     * disables/clears all MPU regions.  It also invalidates I-cache as a
     * BK7258 reset-residue hardening step.
     */
    bl   boot_prepare_app_handoff
    mov  r0, r4

    /*
     * Hardened jump epilogue (BK official S2.7):
     *   r1 = app MSP   = vec[0]
     *   r2 = app Reset = vec[1] (Thumb bit set)
     *   SCB_VTOR <- app_vec ; dsb ; isb
     *   MSP      <- app MSP ; dsb ; isb
     *   clear r0..r12 (r2 preserved, holds app Reset)
     *   dsb ; isb ; bx r2
     */
    ldr  r1, [r0]            /* r1 = app MSP                          */
    ldr  r2, [r0, #4]        /* r2 = app Reset_Handler (Thumb)        */

    ldr  r3, =0xE000ED08     /* SCB->VTOR                             */
    str  r0, [r3]            /* VTOR = app_vec                        */
    dsb  sy
    isb  sy

    msr  msp, r1             /* MSP = app SP                          */
    dsb  sy
    isb  sy

    /* Clear the general register file; preserve r2 (app Reset_Handler). */
    movs r0, #0
    mov  r1, r0
    mov  r3, r0
    mov  r4, r0
    mov  r5, r0
    mov  r6, r0
    mov  r7, r0
    mov  r8, r0
    mov  r9, r0
    mov  r10, r0
    mov  r11, r0
    mov  r12, r0

    dsb  sy
    isb  sy
    bx   r2                  /* jump to app Reset_Handler             */

hang:
    b    hang

/* ------------------------------------------------------------------------ */
/* Exception handlers.                                                       */
/* ------------------------------------------------------------------------ */
    .global NMI_Handler
    .thumb_func
NMI_Handler:
    b    NMI_Handler

    .global default_handler
    .thumb_func
default_handler:
    b    default_handler

/* Compatibility aliases matching the verified minimal-bl symbol names. */
    .global HardFault_Handler
    .thumb_set HardFault_Handler, default_handler

    .end
