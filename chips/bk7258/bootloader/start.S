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

#include "bk7258_debug_route.h"

#define BK7258_BL1_UART_DIV \
    ((26000000 / BK7258_BL1_CONSOLE_BAUD) - 1)
#if BK7258_BL1_CONSOLE_PARITY == 1
#  define BK7258_BL1_UART_PARITY_BITS 0x60
#elif BK7258_BL1_CONSOLE_PARITY == 2
#  define BK7258_BL1_UART_PARITY_BITS 0x20
#else
#  define BK7258_BL1_UART_PARITY_BITS 0
#endif
#if BK7258_BL1_CONSOLE_STOP_BITS == 2
#  define BK7258_BL1_UART_STOP_BITS 0x80
#else
#  define BK7258_BL1_UART_STOP_BITS 0
#endif
#define BK7258_BL1_UART_CONFIG \
    (0x3 | ((BK7258_BL1_CONSOLE_DATA_BITS - 5) << 3) | \
     BK7258_BL1_UART_PARITY_BITS | BK7258_BL1_UART_STOP_BITS | \
     (BK7258_BL1_UART_DIV << 8))

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

    /* Both public v3.1.1.9 BK7258 bootloaders program this exact lower limit
     * before entering C.  BL2 installs its own temporary SRAM-window limit
     * immediately after the BL1-to-BL2 handoff, then restores this CP limit
     * before jumping to the selected NuttX image. */
    ldr  r0, =0x2802F800     /* public CP handoff MSPLIM */
    msr  msplim, r0
    dsb  sy
    isb  sy

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

#if BK7258_BL1_SWD_ENABLE
    /* Keep SWD reachable even if the CP image faults before board bring-up.
     * The RTT profile exposes CPU0 throughout boot and runtime; CPU1 remains
     * independently controlled by CP and is never a prerequisite for attach.
     */
    ldr  r0, =BK7258_SYS_CPU_ROUTE_REG
    ldr  r1, [r0]
    ldr  r2, =BK7258_SYS_JTAG_CORE_MASK
    bics r1, r1, r2
    ldr  r2, =(BK7258_BL1_SWD_TARGET << BK7258_SYS_JTAG_CORE_SHIFT)
    orrs r1, r1, r2
    str  r1, [r0]
#endif

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

    /* Official reset path next restores the Flash controller's two-line
     * continuous-read state and selects a divider from the JEDEC ID.  The
     * later clock helper switches the already-divided Flash clock to DPLL. */
    bl   boot_flash_reset_prepare

#if BK7258_BL1_SWD_ENABLE
    bl   boot_swd_route_apply
#endif

#if BK7258_BL1_CONSOLE_UART < 3
    /* Configure only the selected UART pins.  SWD and UART are independent
     * owners unless Kconfig/build validation rejects an overlapping route.
     */
#  if BK7258_BL1_CONSOLE_UART == 0
    ldr  r0, =0x440100C4
    ldr  r1, [r0]
    bic  r1, r1, #0x0000ff00
    str  r1, [r0]
    ldr  r0, =0x44000428     /* P10 UART0 RX */
    ldr  r1, =0x0015007c
    str  r1, [r0]
    ldr  r0, =0x4400042c     /* P11 UART0 TX */
    movs r1, #0x78
    str  r1, [r0]
#    define BK7258_BL1_UART_BASE 0x44820000
#    define BK7258_BL1_UART_CLK_ENABLE (1 << 2)
#    define BK7258_BL1_UART_CLK_SELECT (1 << 10)
#  elif BK7258_BL1_CONSOLE_UART == 1
    ldr  r0, =0x440100C0
    ldr  r1, [r0]
    bic  r1, r1, #0xff
    str  r1, [r0]
    ldr  r0, =0x44000400     /* P0 UART1 TX */
    movs r1, #0x78
    str  r1, [r0]
    ldr  r0, =0x44000404     /* P1 UART1 RX */
    ldr  r1, =0x00150078
    str  r1, [r0]
#    define BK7258_BL1_UART_BASE 0x45830000
#    define BK7258_BL1_UART_CLK_ENABLE (1 << 10)
#    define BK7258_BL1_UART_CLK_SELECT (1 << 13)
#  else
#    if BK7258_BL1_UART2_PIN_GROUP == 1
    ldr  r0, =0x440100D4
    ldr  r1, [r0]
    bic  r1, r1, #0xff
    str  r1, [r0]
    ldr  r0, =0x440004A0     /* P40 UART2 RX */
    ldr  r1, =0x00150078
    str  r1, [r0]
    ldr  r0, =0x440004A4     /* P41 UART2 TX */
    movs r1, #0x78
    str  r1, [r0]
#    else
    ldr  r0, =0x440100CC
    ldr  r1, [r0]
    ldr  r2, =0xff000000
    bics r1, r1, r2
    ldr  r2, =0x11000000
    orrs r1, r1, r2
    str  r1, [r0]
    ldr  r0, =0x44000478     /* P30 UART2 RX */
    ldr  r1, =0x00150078
    str  r1, [r0]
    ldr  r0, =0x4400047C     /* P31 UART2 TX */
    movs r1, #0x78
    str  r1, [r0]
#    endif
#    define BK7258_BL1_UART_BASE 0x45840000
#    define BK7258_BL1_UART_CLK_ENABLE (1 << 11)
#    define BK7258_BL1_UART_CLK_SELECT (1 << 16)
#  endif

    ldr  r0, =0x44010030
    ldr  r1, [r0]
    ldr  r2, =BK7258_BL1_UART_CLK_ENABLE
    orrs r1, r1, r2
    str  r1, [r0]
    ldr  r0, =0x44010020
    ldr  r1, [r0]
    ldr  r2, =BK7258_BL1_UART_CLK_SELECT
    bics r1, r1, r2
    str  r1, [r0]
    ldr  r0, =(BK7258_BL1_UART_BASE + 0x08)
    movs r1, #1
    str  r1, [r0]
    ldr  r0, =(BK7258_BL1_UART_BASE + 0x10)
    ldr  r1, =BK7258_BL1_UART_CONFIG
    str  r1, [r0]
#endif

    /*
     * Hand off to C: parse the FAL partition table, validate the app header
     * and emit UART logs. Returns app logical vector address in r0. On
     * validation failure c_main prints BAD + reason and loops forever.
     */
    bl   c_main
    mov  r4, r0

#if !BK7258_BL1_USE_BL2
    /* The public SDK bootloader uses 0x2802f800 as its own stack limit, but
     * the board-owned direct NuttX CP image starts its RAM at 0x28010000 and
     * enters with MSP around 0x28015xxx.  Leaving the boot limit installed
     * makes the first push in bk7258_reset_entry raise STKOF before CP can
     * clear MSPLIM itself.  Widen only the direct-CP handoff here; the SRAM
     * BL2 entry installs and later restores its separately contracted limit.
     */
    ldr  r0, =0x28010000
    msr  msplim, r0
    dsb  sy
    isb  sy
#endif

    /*
     * Match the official jump_to_app() cache/MPU contract before changing
     * VTOR/MSP.  This disables D-cache, cleans+invalidates every set/way and
     * disables/clears all MPU regions.  It also invalidates I-cache as a
     * BK7258 reset-residue hardening step.
     */
    bl   boot_prepare_app_handoff

#if BK7258_BL1_SWD_ENABLE
    /* This is the direct-boot debug handshake boundary requested by the
     * board contract: security/cache/MPU cleanup is complete, while the CP
     * Reset_Handler has not executed.  Re-assert the configured route in
     * case an earlier boot stage touched the GPIO or CPU-selection fields.
     */
    bl   boot_swd_route_apply
#  if BK7258_BL1_SWD_BOOT_HOLD
    /* The debugger may halt the core indefinitely.  Disable both boot-stage
     * watchdogs before waiting; a loop-time feed would still expire while
     * halted.  Keep them disabled after release: period zero does not clear
     * the elapsed hardware count, so re-enabling after an unbounded hold can
     * reset before the next instruction.  CP takes watchdog ownership in its
     * reset entry.  Non-hold production builds retain the normal BL1 period.
     */
    ldr  r0, =0x44800010     /* APB_WDT_CTRL */
    ldr  r1, =0x005A0000
    str  r1, [r0]
    ldr  r1, =0x00A50000
    str  r1, [r0]
    ldr  r0, =0x44000600     /* AON_WDT_CTRL */
    ldr  r1, =0x005A0000
    str  r1, [r0]
    ldr  r1, =0x00A50000
    str  r1, [r0]

    ldr  r2, =BK7258_SWD_BOOT_RELEASE_ADDRESS
    movs r3, #0
    str  r3, [r2]
    dsb  sy
1:
    ldr  r0, =BK7258_DHCSR_REG
    ldr  r1, [r0]
    tst  r1, #BK7258_DHCSR_C_DEBUGEN
    beq  1b

    /* C_DEBUGEN precedes the probe's first halt request, so it is not a safe
     * automatic release condition.  Remain at this final handoff boundary
     * until the attached debugger explicitly writes "JLNK" to the release
     * word.  This makes command-line and IDE attach deterministic.
     */

    ldr  r3, =BK7258_SWD_BOOT_RELEASE_MAGIC
2:
    ldr  r1, [r2]
    cmp  r1, r3
    bne  2b
    movs r1, #0
    str  r1, [r2]
    dsb  sy
    isb  sy
#  endif
#endif
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

#if BK7258_BL1_SWD_ENABLE
    .thumb_func
boot_swd_route_apply:
    ldr  r0, =BK7258_DAUTHCTRL_REG
    movs r1, #0x0f
    str  r1, [r0]

    ldr  r0, =BK7258_SYS_CPU_ROUTE_REG
    ldr  r1, [r0]
    ldr  r2, =BK7258_SYS_JTAG_CORE_MASK
    bics r1, r1, r2
    ldr  r2, =(BK7258_BL1_SWD_TARGET << BK7258_SYS_JTAG_CORE_SHIFT)
    orrs r1, r1, r2
    str  r1, [r0]

#  if BK7258_BL1_SWD_PIN_GROUP == BK7258_SWD_PIN_GROUP_0
    ldr  r0, =BK7258_SYS_GPIO16_23_FUNC_REG
    ldr  r1, [r0]
    ldr  r2, =BK7258_SWD_GROUP0_FUNC_MASK
    bics r1, r1, r2
    ldr  r2, =BK7258_SWD_GROUP0_FUNC_VALUE
    orrs r1, r1, r2
    str  r1, [r0]
    ldr  r0, =BK7258_GPIO20_CTRL_REG
#  else
    ldr  r0, =BK7258_SYS_GPIO0_7_FUNC_REG
    ldr  r1, [r0]
    ldr  r2, =BK7258_SWD_GROUP1_FUNC_MASK
    bics r1, r1, r2
    ldr  r2, =BK7258_SWD_GROUP1_FUNC_VALUE
    orrs r1, r1, r2
    str  r1, [r0]
    ldr  r0, =BK7258_GPIO0_CTRL_REG
#  endif
    ldr  r1, [r0]
    ldr  r2, =BK7258_GPIO_FUNC_CTRL_MASK
    bics r1, r1, r2
    orr  r1, r1, #BK7258_GPIO_JTAG_CTRL
    str  r1, [r0]
#  if BK7258_BL1_SWD_PIN_GROUP == BK7258_SWD_PIN_GROUP_0
    ldr  r0, =BK7258_GPIO21_CTRL_REG
#  else
    ldr  r0, =BK7258_GPIO1_CTRL_REG
#  endif
    ldr  r1, [r0]
    ldr  r2, =BK7258_GPIO_FUNC_CTRL_MASK
    bics r1, r1, r2
    orr  r1, r1, #BK7258_GPIO_JTAG_CTRL
    str  r1, [r0]
    dsb  sy
    isb  sy
    bx   lr
#endif

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
