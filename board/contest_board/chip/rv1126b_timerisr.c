/****************************************************************************
 * NuttX - RV1126B System Tick Timer Driver
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version
 * 2.0 (the "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/timers/arch_timer.h>

#include "riscv_internal.h"
#include "hardware/rv1126b_timer.h"
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MTIME register access macros */

#define CTIMER_MTIME_CTRL   (RV1126B_CTIMER_BASE + RV1126B_CTIMER_MTIME_CTRL)
#define CTIMER_MTIME_DIV    (RV1126B_CTIMER_BASE + RV1126B_CTIMER_MTIME_DIV)
#define CTIMER_MTIME        (RV1126B_CTIMER_BASE + RV1126B_CTIMER_MTIME)
#define CTIMER_MTIMEH       (RV1126B_CTIMER_BASE + RV1126B_CTIMER_MTIMEH)
#define CTIMER_MTIMECMP     (RV1126B_CTIMER_BASE + RV1126B_CTIMER_MTIMECMP)
#define CTIMER_MTIMECMPH    (RV1126B_CTIMER_BASE + RV1126B_CTIMER_MTIMECMPH)

/* Timer configuration:
 * HPMCU frequency = 396 MHz
 * Divider = 1000
 * Effective timer clock = 396 MHz / 1000 = 396 kHz
 * For 100 Hz tick: 396000 / 100 = 3960 counts per tick
 */

#define RV1126B_MTIME_DIV_VALUE   1000
#define RV1126B_MTIME_TICK_VALUE  (RV1126B_MTIME_FREQ / RV1126B_MTIME_DIV_VALUE / 100)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const uint64_t g_mtimecmp_step = RV1126B_MTIME_TICK_VALUE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_timer_stop
 *
 * Description:
 *   Stop the MTIME timer by clearing the enable bit.
 *
 ****************************************************************************/

static void rv1126b_timer_stop(void)
{
  uint32_t regval;

  regval = getreg32(CTIMER_MTIME_CTRL);
  regval &= ~RV1126B_MTIME_CTRL_EN;
  putreg32(regval, CTIMER_MTIME_CTRL);
}

/****************************************************************************
 * Name: rv1126b_timer_start
 *
 * Description:
 *   Start the MTIME timer by setting the enable bit.
 *
 ****************************************************************************/

static void rv1126b_timer_start(void)
{
  uint32_t regval;

  regval = getreg32(CTIMER_MTIME_CTRL);
  regval |= RV1126B_MTIME_CTRL_EN;
  putreg32(regval, CTIMER_MTIME_CTRL);
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_timer_interrupt
 *
 * Description:
 *   Timer interrupt handler registered via irq_attach().  Advances the
 *   MTIMECMP register by one tick period using the safe high-low-high
 *   sequence and processes the OS tick.
 *
 *   MTIME read uses high-low-high retry to handle rollover.
 *   MTIMECMP write uses the safe sequence (high=UINT32_MAX, low,
 *   final-high) under up_irq_save()/restore() to prevent spurious
 *   compares during the write.
 *
 ****************************************************************************/

static int rv1126b_timer_interrupt(int irq, void *context, void *arg)
{
  uint32_t mtime_hi;
  uint32_t mtime_lo;
  uint32_t mtime_hi2;
  uint64_t mtime;
  uint64_t mtimecmp;
  irqstate_t flags;

  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);

  /* Read MTIME with high-low-high retry to handle rollover */

  do
    {
      mtime_hi  = getreg32(CTIMER_MTIMEH);
      mtime_lo  = getreg32(CTIMER_MTIME);
      mtime_hi2 = getreg32(CTIMER_MTIMEH);
    }
  while (mtime_hi != mtime_hi2);

  mtime = ((uint64_t)mtime_hi << 32) | mtime_lo;
  mtimecmp = mtime + g_mtimecmp_step;

  /* Write MTIMECMP using the safe sequence under irq_save to prevent
   * a spurious compare during the two-step write.  Writing high first
   * to UINT32_MAX guarantees no match until the final high write.
   */

  flags = up_irq_save();
  putreg32(UINT32_MAX, CTIMER_MTIMECMPH);
  putreg32((uint32_t)(mtimecmp & 0xffffffff), CTIMER_MTIMECMP);
  putreg32((uint32_t)(mtimecmp >> 32), CTIMER_MTIMECMPH);
  up_irq_restore(flags);

  /* Process the OS tick */

  nxsched_process_timer();

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   Configure the MTIME timer to generate periodic system tick interrupts
 *   at 100 Hz (CONFIG_USEC_PER_TICK = 10000 usec).
 *
 *   This function is called during early OS initialization before the
 *   scheduler is started.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  /* Stop the timer first */

  rv1126b_timer_stop();

  /* Reset MTIME counter to 0 */

  putreg32(0, CTIMER_MTIME);
  putreg32(0, CTIMER_MTIMEH);

  /* Reset MTIMECMP to 0 */

  putreg32(0, CTIMER_MTIMECMP);
  putreg32(0, CTIMER_MTIMECMPH);

  /* Set the divider to divide the core clock by 1000 */

  putreg32(RV1126B_MTIME_DIV_VALUE, CTIMER_MTIME_DIV);

  /* Set MTIMECMP to the first tick value */

  putreg32(RV1126B_MTIME_TICK_VALUE, CTIMER_MTIMECMP);
  putreg32(0, CTIMER_MTIMECMPH);

  /* Register the timer ISR and enable the IRQ through the NuttX
   * IRQ framework (which handles MIE_MTIE via up_enable_irq).
   */

  if (irq_attach(RISCV_IRQ_MTIMER, rv1126b_timer_interrupt, NULL) < 0)
    {
      /* Cannot register timer ISR -- fatal at boot */
      return;
    }

  up_enable_irq(RISCV_IRQ_MTIMER);

  /* Start the timer */

  rv1126b_timer_start();
}
