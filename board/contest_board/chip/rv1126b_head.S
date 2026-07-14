/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/rv1126b_head.S
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * RV1126B HPMCU (RISC-V SCR1) startup assembly for NuttX
 *
 * Provides:
 *   - __start: primary entry point
 *   - g_traps: trap vector for interrupts and exceptions
 *
 * Based on the Rockchip RV1126B SDK CMSIS startup template and
 * NuttX RISC-V conventions.
 *
 * Register layout must match nuttx/arch/risc-v/include/irq.h
 * (RV32, no CLIC, no shadow stack):
 *
 *   Slot  0        (offset   0) : REG_EPC     -- mepc
 *   Slot  1        (offset   4) : REG_X1      -- x1 (ra)
 *   Slot  2        (offset   8) : REG_X2      -- x2 (sp)
 *   ...
 *   Slot 31        (offset 124) : REG_X31     -- x31 (t6)
 *   Slot 32        (offset 128) : REG_INT_CTX -- mstatus
 *   Total: XCPTCONTEXT_REGS = 33, XCPTCONTEXT_SIZE = 132
 ****************************************************************************/

#include <nuttx/config.h>

/* Register save/restore width for RV32 */

#define REGBYTES    4
#define RV32_REGBYTES   REGBYTES

/* NuttX RISC-V trap frame layout (no CLIC, no shadow stack).
 * These MUST match nuttx/arch/risc-v/include/irq.h.
 * Include <arch/irq.h> instead of redefining when the build tree
 * supports it; local defines kept for build-tree independence.
 */

#define REG_EPC_NDX         0
#define REG_X1_NDX          1
#define REG_X2_NDX          2
#define REG_INT_CTX_NDX    32
#define XCPTCONTEXT_REGS    33
#define XCPTCONTEXT_SIZE    (XCPTCONTEXT_REGS * REGBYTES)

/* Assembly byte-offset aliases (INT_REG_SIZE = 4 on RV32) */

#define REG_EPC         (REGBYTES * REG_EPC_NDX)
#define REG_X1          (REGBYTES * REG_X1_NDX)
#define REG_X2          (REGBYTES * REG_X2_NDX)
#define REG_INT_CTX     (REGBYTES * REG_INT_CTX_NDX)

/* MSTATUS bits */

#define MSTATUS_MIE     0x00000008

/****************************************************************************
 * .text.start section -- primary entry point
 ****************************************************************************/

    .section .text.start, "ax"
    .align  6
    .global __start
    .type   __start, %function

__start:
    /* Set the global pointer from the _gp linker symbol.
     * Convention: _gp = .sdata + 0x800
     */

    .option push
    .option norelax
    la      gp, _gp
    .option pop

    /* Set the stack pointer to the top of the idle thread stack.
     * _ebss is the end of BSS; IDLETHREAD_STACKSIZE is configured by NuttX.
     */

    la      sp, _ebss
    li      t0, CONFIG_IDLETHREAD_STACKSIZE
    add     sp, sp, t0

    /* Clear MIE bit in mstatus to disable all interrupts during init */

    li      t0, MSTATUS_MIE
    csrrc   zero, mstatus, t0

    /* Set mtvec to the trap vector entry point */

    la      t0, g_traps
    csrw    mtvec, t0

    /* Jump to C startup code */

    call    rv1126b_start

    /* Should never return, but if it does, spin */

1:
    wfi
    j       1b

    .size   __start, . - __start

/****************************************************************************
 * .text.trap section -- trap/interrupt vector and context save/restore
 ****************************************************************************/

    .section .text.trap, "ax"
    .align  6
    .global g_traps
    .type   g_traps, %function

g_traps:
    /* Allocate space on the stack for the full register context */

    addi    sp, sp, -XCPTCONTEXT_SIZE

    /* Save all general-purpose registers x1-x31 into NuttX slots 1-31.
     * Slot layout matches nuttx/arch/risc-v/include/irq.h:
     *   REG_X1_NDX=1 ... REG_X31_NDX=31
     */

    sw      x1,  1 * REGBYTES(sp)   /* ra */
    addi    t1, sp, XCPTCONTEXT_SIZE
    sw      t1,  2 * REGBYTES(sp)   /* sp (pre-trap value) */
    sw      x3,  3 * REGBYTES(sp)   /* gp */
    sw      x4,  4 * REGBYTES(sp)   /* tp */
    sw      x5,  5 * REGBYTES(sp)   /* t0 */
    sw      x6,  6 * REGBYTES(sp)   /* t1 */
    sw      x7,  7 * REGBYTES(sp)   /* t2 */
    sw      x8,  8 * REGBYTES(sp)   /* s0/fp */
    sw      x9,  9 * REGBYTES(sp)   /* s1 */
    sw      x10, 10 * REGBYTES(sp)  /* a0 */
    sw      x11, 11 * REGBYTES(sp)  /* a1 */
    sw      x12, 12 * REGBYTES(sp)  /* a2 */
    sw      x13, 13 * REGBYTES(sp)  /* a3 */
    sw      x14, 14 * REGBYTES(sp)  /* a4 */
    sw      x15, 15 * REGBYTES(sp)  /* a5 */
    sw      x16, 16 * REGBYTES(sp)  /* a6 */
    sw      x17, 17 * REGBYTES(sp)  /* a7 */
    sw      x18, 18 * REGBYTES(sp)  /* s2 */
    sw      x19, 19 * REGBYTES(sp)  /* s3 */
    sw      x20, 20 * REGBYTES(sp)  /* s4 */
    sw      x21, 21 * REGBYTES(sp)  /* s5 */
    sw      x22, 22 * REGBYTES(sp)  /* s6 */
    sw      x23, 23 * REGBYTES(sp)  /* s7 */
    sw      x24, 24 * REGBYTES(sp)  /* s8 */
    sw      x25, 25 * REGBYTES(sp)  /* s9 */
    sw      x26, 26 * REGBYTES(sp)  /* s10 */
    sw      x27, 27 * REGBYTES(sp)  /* s11 */
    sw      x28, 28 * REGBYTES(sp)  /* t3 */
    sw      x29, 29 * REGBYTES(sp)  /* t4 */
    sw      x30, 30 * REGBYTES(sp)  /* t5 */
    sw      x31, 31 * REGBYTES(sp)  /* t6 */

    /* Save mepc into slot 0 (REG_EPC).
     * NuttX riscv_doirq_top() reads regs[REG_EPC] to advance PC on ecall;
     * on context switch, the scheduler returns a different task's regs and
     * this value is written back to mepc below.
     */

    csrr    t0, mepc
    sw      t0, REG_EPC(sp)

    /* Save mstatus into slot 32 (REG_INT_CTX).
     * Contains MIE/MPIE/MPP bits needed across context switches.
     * Matches NuttX convention for non-CLIC, non-shadow-stack configs.
     */

    csrr    t0, mstatus
    sw      t0, REG_INT_CTX(sp)

    /* Pass context pointer (sp) as second argument (a1) and
     * mcause as first argument (a0) to the C dispatch function.
     *
     * Prototype: void *riscv_dispatch_irq(uintptr_t mcause,
     *                                     uintreg_t *regs);
     * Returns: pointer to the (possibly new) register context.
     */

    mv      a1, sp
    csrr    a0, mcause
    call    riscv_dispatch_irq

    /* riscv_dispatch_irq / riscv_doirq may switch context:
     * a0 now points to the register frame of the task to resume.
     */

    mv      sp, a0

    /* Restore mepc from slot 0 (REG_EPC) */

    lw      t0, REG_EPC(sp)
    csrw    mepc, t0

    /* Restore mstatus from slot 32 (REG_INT_CTX) */

    lw      t0, REG_INT_CTX(sp)
    csrw    mstatus, t0

    /* Restore all general-purpose registers except x2/sp first.
     * Keep sp pointing at the context frame until the final load.
     */

    lw      x1,  1 * REGBYTES(sp)   /* ra */
    lw      x3,  3 * REGBYTES(sp)   /* gp */
    lw      x4,  4 * REGBYTES(sp)   /* tp */
    lw      x5,  5 * REGBYTES(sp)   /* t0 */
    lw      x6,  6 * REGBYTES(sp)   /* t1 */
    lw      x7,  7 * REGBYTES(sp)   /* t2 */
    lw      x8,  8 * REGBYTES(sp)   /* s0/fp */
    lw      x9,  9 * REGBYTES(sp)   /* s1 */
    lw      x10, 10 * REGBYTES(sp)  /* a0 */
    lw      x11, 11 * REGBYTES(sp)  /* a1 */
    lw      x12, 12 * REGBYTES(sp)  /* a2 */
    lw      x13, 13 * REGBYTES(sp)  /* a3 */
    lw      x14, 14 * REGBYTES(sp)  /* a4 */
    lw      x15, 15 * REGBYTES(sp)  /* a5 */
    lw      x16, 16 * REGBYTES(sp)  /* a6 */
    lw      x17, 17 * REGBYTES(sp)  /* a7 */
    lw      x18, 18 * REGBYTES(sp)  /* s2 */
    lw      x19, 19 * REGBYTES(sp)  /* s3 */
    lw      x20, 20 * REGBYTES(sp)  /* s4 */
    lw      x21, 21 * REGBYTES(sp)  /* s5 */
    lw      x22, 22 * REGBYTES(sp)  /* s6 */
    lw      x23, 23 * REGBYTES(sp)  /* s7 */
    lw      x24, 24 * REGBYTES(sp)  /* s8 */
    lw      x25, 25 * REGBYTES(sp)  /* s9 */
    lw      x26, 26 * REGBYTES(sp)  /* s10 */
    lw      x27, 27 * REGBYTES(sp)  /* s11 */
    lw      x28, 28 * REGBYTES(sp)  /* t3 */
    lw      x29, 29 * REGBYTES(sp)  /* t4 */
    lw      x30, 30 * REGBYTES(sp)  /* t5 */
    lw      x31, 31 * REGBYTES(sp)  /* t6 */
    lw      x2,  2 * REGBYTES(sp)   /* sp -- restore last */

    /* Return from trap -- mepc and mstatus already restored above */

    mret

    .size   g_traps, . - g_traps
