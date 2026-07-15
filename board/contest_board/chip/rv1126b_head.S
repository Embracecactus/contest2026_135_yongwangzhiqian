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
 * Register layout comes from nuttx/arch/risc-v/include/irq.h
 * (RV32, no CLIC, no shadow stack):
 *
 *   Slot  0        (offset   0) : REG_EPC     -- mepc
 *   Slot  1        (offset   4) : REG_X1      -- x1 (ra)
 *   Slot  2        (offset   8) : REG_X2      -- x2 (sp)
 *   ...
 *   Slot 31        (offset 124) : REG_X31     -- x31 (t6)
 *   Slot 32        (offset 128) : REG_INT_CTX -- mstatus
 *   Register bytes remain canonical; stack allocation is rounded up to
 *   STACKFRAME_ALIGN before calling C from trap context.
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/irq.h>

/* Preserve the canonical register layout while keeping trap-call stack
 * alignment required by the RISC-V psABI.
 */

#define RV1126B_TRAPFRAME_SIZE \
  ((XCPTCONTEXT_SIZE + STACKFRAME_ALIGN - 1) & ~(STACKFRAME_ALIGN - 1))

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
    andi    sp, sp, -STACKFRAME_ALIGN

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
    /* Allocate aligned stack space for the full register context. */

    addi    sp, sp, -RV1126B_TRAPFRAME_SIZE

    /* Save all general-purpose registers x1-x31 into NuttX slots 1-31.
     * Slot layout matches nuttx/arch/risc-v/include/irq.h.
     */

    sw      x1,  REG_X1(sp)        /* ra */
    sw      x6,  REG_X6(sp)        /* t1 */
    addi    t1, sp, RV1126B_TRAPFRAME_SIZE
    sw      t1,  REG_X2(sp)        /* sp (pre-trap value) */
    sw      x3,  REG_X3(sp)        /* gp */
    sw      x4,  REG_X4(sp)        /* tp */
    sw      x5,  REG_X5(sp)        /* t0 */
    sw      x7,  REG_X7(sp)        /* t2 */
    sw      x8,  REG_X8(sp)        /* s0/fp */
    sw      x9,  REG_X9(sp)        /* s1 */
    sw      x10, REG_X10(sp)       /* a0 */
    sw      x11, REG_X11(sp)       /* a1 */
    sw      x12, REG_X12(sp)       /* a2 */
    sw      x13, REG_X13(sp)       /* a3 */
    sw      x14, REG_X14(sp)       /* a4 */
    sw      x15, REG_X15(sp)       /* a5 */
    sw      x16, REG_X16(sp)       /* a6 */
    sw      x17, REG_X17(sp)       /* a7 */
    sw      x18, REG_X18(sp)       /* s2 */
    sw      x19, REG_X19(sp)       /* s3 */
    sw      x20, REG_X20(sp)       /* s4 */
    sw      x21, REG_X21(sp)       /* s5 */
    sw      x22, REG_X22(sp)       /* s6 */
    sw      x23, REG_X23(sp)       /* s7 */
    sw      x24, REG_X24(sp)       /* s8 */
    sw      x25, REG_X25(sp)       /* s9 */
    sw      x26, REG_X26(sp)       /* s10 */
    sw      x27, REG_X27(sp)       /* s11 */
    sw      x28, REG_X28(sp)       /* t3 */
    sw      x29, REG_X29(sp)       /* t4 */
    sw      x30, REG_X30(sp)       /* t5 */
    sw      x31, REG_X31(sp)       /* t6 */

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

    /* Restore all general-purpose registers except x2/sp first.
     * Keep sp pointing at the context frame until the final load.
     *
     * mstatus is restored LAST, immediately before mret, to avoid
     * re-enabling interrupts (MIE) during the GPR restore sequence.
     */

    lw      x1,  REG_X1(sp)        /* ra */
    lw      x3,  REG_X3(sp)        /* gp */
    lw      x4,  REG_X4(sp)        /* tp */
    lw      x5,  REG_X5(sp)        /* t0 */
    lw      x6,  REG_X6(sp)        /* t1 */
    lw      x7,  REG_X7(sp)        /* t2 */
    lw      x8,  REG_X8(sp)        /* s0/fp */
    lw      x9,  REG_X9(sp)        /* s1 */
    lw      x10, REG_X10(sp)       /* a0 */
    lw      x11, REG_X11(sp)       /* a1 */
    lw      x12, REG_X12(sp)       /* a2 */
    lw      x13, REG_X13(sp)       /* a3 */
    lw      x14, REG_X14(sp)       /* a4 */
    lw      x15, REG_X15(sp)       /* a5 */
    lw      x16, REG_X16(sp)       /* a6 */
    lw      x17, REG_X17(sp)       /* a7 */
    lw      x18, REG_X18(sp)       /* s2 */
    lw      x19, REG_X19(sp)       /* s3 */
    lw      x20, REG_X20(sp)       /* s4 */
    lw      x21, REG_X21(sp)       /* s5 */
    lw      x22, REG_X22(sp)       /* s6 */
    lw      x23, REG_X23(sp)       /* s7 */
    lw      x24, REG_X24(sp)       /* s8 */
    lw      x25, REG_X25(sp)       /* s9 */
    lw      x26, REG_X26(sp)       /* s10 */
    lw      x27, REG_X27(sp)       /* s11 */
    lw      x28, REG_X28(sp)       /* t3 */
    lw      x29, REG_X29(sp)       /* t4 */
    lw      x30, REG_X30(sp)       /* t5 */
    lw      x31, REG_X31(sp)       /* t6 */

    /* Restore mstatus last -- re-enables MIE only after all GPRs
     * are restored, preventing spurious nested traps during exit.
     */

    lw      t0,  REG_INT_CTX(sp)
    csrw    mstatus, t0

    lw      x2,  REG_X2(sp)        /* sp -- restore last */

    /* Return from trap -- mepc and mstatus already restored above */

    mret

    .size   g_traps, . - g_traps
