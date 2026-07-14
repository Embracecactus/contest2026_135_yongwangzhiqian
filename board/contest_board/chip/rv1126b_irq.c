/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/rv1126b_irq.c
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
 * RV1126B HPMCU interrupt controller (INTMUX) for NuttX
 *
 * The RV1126B HPMCU uses a RISC-V SCR1 core with a local interrupt
 * controller (IPIC) and an external interrupt multiplexer (INTMUX).
 * External interrupts from peripherals are routed through the INTMUX
 * before reaching the machine external interrupt (MEIP) line.
 *
 * INTMUX register layout:
 *   Base: 0x20b40000
 *   Enable registers: offset 0x00..0x1C (8 groups x 32 bits = 256 IRQs)
 *   Status registers: offset 0x80..0x9C (8 groups x 32 bits)
 *
 * The IPIC (CSR 0xbf0..0xbf7) is used for EOI/SOI handshaking with the
 * SCR1 interrupt controller.
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
#define INTMUX_NR_IRQS          (INTMUX_NGROUPS * INTMUX_IRQS_PER_GROUP)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_intmux_enable
 *
 * Description:
 *   Enable a specific IRQ in the INTMUX enable registers.
 *   Uses the RV1126B_INTMUX_ENABLE(n) macro from rv1126b_intmux.h.
 *
 * Input Parameters:
 *   irq - IRQ number (0..255)
 *
 ****************************************************************************/

static void rv1126b_intmux_enable(int irq)
{
  uintptr_t reg = RV1126B_INTMUX_ENABLE(irq);
  unsigned int bit = irq % INTMUX_IRQS_PER_GROUP;

  putreg32(getreg32(reg) | (1u << bit), reg);
}

/****************************************************************************
 * Name: rv1126b_intmux_disable
 *
 * Description:
 *   Disable a specific IRQ in the INTMUX enable registers.
 *   Uses the RV1126B_INTMUX_ENABLE(n) macro from rv1126b_intmux.h.
 *
 * Input Parameters:
 *   irq - IRQ number (0..255)
 *
 ****************************************************************************/

static void rv1126b_intmux_disable(int irq)
{
  uintptr_t reg = RV1126B_INTMUX_ENABLE(irq);
  unsigned int bit = irq % INTMUX_IRQS_PER_GROUP;

  putreg32(getreg32(reg) & ~(1u << bit), reg);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 *
 * Description:
 *   Initialize the interrupt subsystem for the RV1126B HPMCU.
 *   - Disable all INTMUX enables
 *   - Set mtvec to the trap vector
 *   - Enable machine external interrupts in MIE
 *   - Enable global interrupts
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  int i;

  /* Disable all INTMUX interrupt enables */

  for (i = 0; i < INTMUX_NGROUPS; i++)
    {
      putreg32(0, RV1126B_INTMUX_BASE + RV1126B_INTMUX_ENABLE_OFFSET +
                (i * RV1126B_INTMUX_GROUP_STRIDE));
    }

  /* Enable all IPIC vectors before enabling MEIP.  write_csr() stringifies
   * its CSR argument, so these CSR numbers must remain literals.
   */

  for (i = 0; i < 16; i++)
    {
      write_csr(0xbf6, i);
      write_csr(0xbf7, 1u << 1);
    }

  /* Note: mtvec is already set to the NuttX trap vector
   * (g_traps in rv1126b_head.S) before NuttX code runs.
   */

  /* Enable machine external interrupts in the mie CSR */

  set_csr(mie, MIE_MEIE);

  /* Enable global interrupts */

  up_irq_enable();
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the specified IRQ in the INTMUX.
 *
 * Input Parameters:
 *   irq - IRQ number to enable
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  rv1126b_intmux_enable(irq);
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the specified IRQ in the INTMUX.
 *
 * Input Parameters:
 *   irq - IRQ number to disable
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  rv1126b_intmux_disable(irq);
}

/****************************************************************************
 * Name: up_irq_enable
 *
 * Description:
 *   Enable interrupts and return the previous interrupt state.
 *
 ****************************************************************************/

irqstate_t up_irq_enable(void)
{
  irqstate_t flags;

  __asm__ __volatile__
    (
      "csrrs %0, mstatus, %1\n"
      : "=r" (flags)
      : "r"(MSTATUS_MIE)
      : "memory"
    );

  return flags;
}
