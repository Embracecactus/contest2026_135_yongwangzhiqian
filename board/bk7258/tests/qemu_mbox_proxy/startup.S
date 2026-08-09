/*
 * contest2026_135_yongwangzhiqian/board/bk7258/tests/qemu_mbox_proxy/startup.S
 *
 * Minimal freestanding startup for QEMU mps2-an521 (dual Cortex-M33).
 *
 * This is a TEST/BOARD harness only.  It does NOT reuse or modify the BK7258
 * SDK or firmware: it re-implements the RPTUN MBOX0 notify state machine from
 * bk7258_rptun_mbox.c against a real dual-core MHU doorbell + shared RAM, so
 * the cross-core locking / coalescing / retry behaviour can be exercised under
 * actual inter-core interrupts.
 *
 * Both cores execute this same image from the shared reset vector.  Core 1 is
 * parked by QEMU until core 0 writes CPUWAIT=0; it then re-fetches the vector
 * with its own (per-core) stack, so the SP must be switched before any C runs.
 */

    .syntax unified
    .cpu cortex-m33
    .thumb

    .equ CPUID_ADDR, 0x4001F000

    .section .vector_table, "a"
    .globl g_vector_table
g_vector_table:
    .word _stack_core0        /* 0  initial MSP (core 0); core 1 switches ASAP */
    .word Reset_Handler       /* 1  Reset */
    .word NMI_Handler         /* 2  NMI */
    .word HardFault_Handler   /* 3  HardFault */
    .word MemManage_Handler   /* 4  MemManage */
    .word BusFault_Handler    /* 5  BusFault */
    .word UsageFault_Handler  /* 6  UsageFault */
    .word 0                   /* 7  reserved */
    .word 0                   /* 8  reserved */
    .word 0                   /* 9  reserved */
    .word 0                   /* 10 reserved */
    .word SVC_Handler         /* 11 SVC */
    .word DebugMon_Handler    /* 12 DebugMon */
    .word 0                   /* 13 reserved */
    .word PendSV_Handler      /* 14 PendSV */
    .word SysTick_Handler     /* 15 SysTick */
    .word Default_Handler     /* 16 IRQ0 */
    .word Default_Handler     /* 17 IRQ1 */
    .word Default_Handler     /* 18 IRQ2 */
    .word Default_Handler     /* 19 IRQ3 */
    .word Default_Handler     /* 20 IRQ4 */
    .word Default_Handler     /* 21 IRQ5 */
    .word MHU0_IRQHandler     /* 22 IRQ6  (doorbell CP->AP, lands on core 1) */
    .word MHU1_IRQHandler     /* 23 IRQ7  (doorbell AP->CP, lands on core 0) */

    .text
    .thumb_func
    .globl Reset_Handler
Reset_Handler:
    /* On QEMU mps2-an521 the boot SSRAM lives at 0x10000000 and 0x00000000
       is NOT aliased to it, so the hardware-reset VTOR (0) points at empty
       memory.  Point VTOR at our vector table before any exception can be
       taken (this also fixes CPU1, which re-fetches its reset vector on
       release). */
    ldr r0, =0xE000ED08     /* SCB VTOR */
    ldr r1, =0x10000000     /* our vector table base */
    str r1, [r0]

    /* Read core id (register-only; no stack needed yet). */
    ldr r3, =CPUID_ADDR
    ldr r3, [r3]
    cbz r3, .Lcore0

    /* ---- Core 1 : switch to its own stack, then run AP side ---- */
    ldr sp, =_stack_core1
    bl core1_main
    b .

.Lcore0:
    /* ---- Core 0 : zero .bss exactly once (core 1 must NOT re-zero it,
       or it would clobber state core 0 has already set up) ---- */
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
1:
    cmp r0, r1
    bge 2f
    str r2, [r0]
    adds r0, r0, #4
    b 1b
2:
    ldr sp, =_stack_core0
    bl core0_main
    b .

    .thumb_func
    .globl Default_Handler
    .weak Default_Handler
Default_Handler:
    b .

    .weak NMI_Handler, HardFault_Handler, MemManage_Handler, BusFault_Handler
    .weak UsageFault_Handler, SVC_Handler, DebugMon_Handler, PendSV_Handler
    .weak SysTick_Handler, MHU0_IRQHandler, MHU1_IRQHandler
    .set NMI_Handler, Default_Handler
    .set HardFault_Handler, Default_Handler
    .set MemManage_Handler, Default_Handler
    .set BusFault_Handler, Default_Handler
    .set UsageFault_Handler, Default_Handler
    .set SVC_Handler, Default_Handler
    .set DebugMon_Handler, Default_Handler
    .set PendSV_Handler, Default_Handler
    .set SysTick_Handler, Default_Handler
    .set MHU0_IRQHandler, Default_Handler
    .set MHU1_IRQHandler, Default_Handler
