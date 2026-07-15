/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/rv1126b_irq_dispatch.c
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
 * RV1126B HPMCU IRQ dispatch for NuttX
 *
 * This file implements the RISC-V interrupt dispatch logic for the
 * RV1126B HPMCU.  The SCR1 core uses the IPIC (CSR 0xbf0..0xbf7) for
 * interrupt prioritization and EOI/SOI handshaking, and the INTMUX
 * for external interrupt routing.
 *
 * Dispatch flow:
 *   1. Trap entry (rv1126b_head.S g_traps) saves context and calls
 *      riscv_dispatch_irq(mcause, regs).
 *   2. riscv_dispatch_irq() identifies the interrupt type and:
 *      - For timer interrupts (mcause=7): calls up_timer_int()
 *        and returns regs.
 *      - For external interrupts (mcause=11): scans INTMUX status
 *        registers to find the active IRQs, then dispatches each
 *        via riscv_doirq(irq, regs) (which handles context switch
 *        bookkeeping), wrapped by IPIC SOI/EOI.
 *   3. The returned regs pointer is used by the assembly trap exit
 *      to restore the (possibly switched) context.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>

#include <stdint.h>

#include "riscv_internal.h"
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* INTMUX definitions from hardware/rv1126b_intmux.h */

#define INTMUX_NGROUPS          RV1126B_INTMUX_NGROUPS  /* 8 groups */
#define INTMUX_IRQS_PER_GROUP   32

/* RISC-V interrupt cause mask */

#define MCAUSE_INTERRUPT_BIT   ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_ipic_soi
 *
 * Description:
 *   Write to IPIC SOI (Start of Interrupt) CSR to acknowledge the
 *   start of interrupt processing.  Any value can be written.
 *
 ****************************************************************************/

static inline void rv1126b_ipic_soi(void)
{
  write_csr(0xbf5, 0);
}

/****************************************************************************
 * Name: rv1126b_ipic_eoi
 *
 * Description:
 *   Write to IPIC EOI (End of Interrupt) CSR to signal the end of
 *   interrupt processing.  Any value can be written.
 *
 ****************************************************************************/

static inline void rv1126b_ipic_eoi(void)
{
  write_csr(0xbf4, 0);
}

/****************************************************************************
 * Name: rv1126b_get_active_irq
 *
 * Description:
 *   Scan the INTMUX status registers to find the highest-priority
 *   active (pending and enabled) IRQ.
 *
 * Returned Value:
 *   IRQ number (0..255), or -1 if no active IRQ is found.
 *
 ****************************************************************************/

static int rv1126b_get_active_irq(void)
{
  int i;
  unsigned int status;

  for (i = 0; i < INTMUX_NGROUPS; i++)
    {
      status = getreg32(RV1126B_INTMUX_BASE +
                        RV1126B_INTMUX_STATUS_OFFSET +
                        (i * RV1126B_INTMUX_GROUP_STRIDE));

      /* Source 0 in group 0 is reserved (collides with RISCV_IRQ_MEXT
       * aggregate slot in M-mode).  Mask it out so it never appears
       * as an active IRQ.
       */

      if (i == 0)
        {
          status &= ~1u;
        }

      if (status != 0)
        {
          /* Find the lowest-numbered (highest priority) set bit */

          int bit = __builtin_ctz(status);
          return i * INTMUX_IRQS_PER_GROUP + bit;
        }
    }

  return -1;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_dispatch_irq
 *
 * Description:
 *   Dispatch an interrupt based on the mcause value.
 *
 *   This function is called from the NuttX RISC-V common interrupt
 *   handling path (riscv_doirq -> riscv_dispatch_irq) when the mcause
 *   indicates an interrupt (bit 31 set).
 *
 *   For the RV1126B HPMCU:
 *     - mcause == 7  (IRQ_M_TIMER): machine timer interrupt
 *     - mcause == 11 (IRQ_M_EXT):   machine external interrupt
 *       (routed through INTMUX)
 *
 * Input Parameters:
 *   mcause - The value of the mcause CSR
 *   regs   - Pointer to the saved register context
 *
 * Returned Value:
 *   Pointer to the (possibly new) register context after dispatch.
 *
 ****************************************************************************/

void *riscv_dispatch_irq(uintptr_t mcause, uintreg_t *regs)
{
  uintptr_t cause;

  if ((mcause & MCAUSE_INTERRUPT_BIT) == 0)
    {
      /* Synchronous exceptions must go through riscv_doirq() so NuttX can
      * advance mepc for ecall and handle context switching properly.
      */

      regs = riscv_doirq((int)mcause, regs);
      return regs;
    }

  /* Interrupt: strip the interrupt bit to get the cause code */

  cause = mcause & ~MCAUSE_INTERRUPT_BIT;

  if (cause == IRQ_M_TIMER)
    {
      /* Machine timer interrupt -- dispatch through NuttX IRQ framework */

      regs = riscv_doirq(RISCV_IRQ_MTIMER, regs);
    }
  else if (cause == IRQ_M_EXT)
    {
      /* Machine external interrupt -- scan INTMUX for active IRQs */

      int irq;

      /* Signal IPIC Start of Interrupt */

      rv1126b_ipic_soi();

      /* Scan for active IRQs -- handle all pending IRQs.
       * Use a bounded loop to prevent infinite spinning if a handler
       * fails to clear its interrupt source.
       */

      int max_irqs = INTMUX_NGROUPS * INTMUX_IRQS_PER_GROUP;

      while ((irq = rv1126b_get_active_irq()) >= 0 && --max_irqs > 0)
        {
          /* Dispatch through the NuttX IRQ dispatch table using the
           * INTMUX-source-to-NuttX-IRQ conversion.
           */

          regs = riscv_doirq(RV1126B_INTMUX_SOURCE_TO_IRQ(irq), regs);
        }

      /* Signal IPIC End of Interrupt */

      rv1126b_ipic_eoi();
    }
  else
    {
      /* Unexpected interrupt cause -- halt. */

      for (; ; )
        {
        }
    }

  return regs;
}
