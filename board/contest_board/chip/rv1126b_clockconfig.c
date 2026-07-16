/****************************************************************************
 * NuttX - RV1126B Clock Verification
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
#include <stdbool.h>

#include <nuttx/arch.h>

#include "riscv_internal.h"
#include "hardware/rv1126b_memorymap.h"
#include "hardware/rv1126b_cru.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* PLL configuration registers */

#define TOPCRU_BASE           RV1126B_TOPCRU_BASE

/* GPLL (General PLL) registers at TOPCRU
 *
 * SDK-confirmed layout (rv1126b.h TOPCRU_REG):
 *   AUPLL_CON[5] at offset 0x0000 (5 registers, 20 bytes)
 *   GPLL_CON[5]  at offset 0x0020
 *
 * GPLL_CON[2] at offset 0x0028:
 *   Bit 10: LOCK (read-only, 1 = PLL is locked)
 *
 * PLL output = (24 MHz / refdiv) * fbdiv / postdiv1 / postdiv2
 * For 1188 MHz: refdiv=1, fbdiv=99, postdiv1=2, postdiv2=1
 *   (24/1) * 99 / 2 = 1188 MHz
 */

/* HPMCU clock source selection
 * CLK_HPMCU is a gated clock from aclk_bus_root (no mux/divider).
 * SDK-verified: SCR1_CORE_FREQUECY = 396 MHz (GPLL 1188 MHz / 3).
 * The divider is configured by A-core/bootrom; HPMCU does not need
 * to write to CORECRU CLKSEL_CON00 (those bits belong to A53 core).
 */

/* SDK-verified: GPLL lock status is at GPLL_CON[2] bit 10 (TOPCRU + 0x28) */

#define GPLL_LOCK_ADDR        (TOPCRU_BASE + 0x0028)
#define GPLL_LOCK_SHIFT       10
#define GPLL_LOCK_MASK        (1 << GPLL_LOCK_SHIFT)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_wait_pll_lock
 *
 * Description:
 *   Wait for the PLL to achieve lock by polling the lock bit.
 *
 * Returned Value:
 *   true if PLL locked within the timeout, false otherwise.
 *
 ****************************************************************************/

static bool rv1126b_wait_pll_lock(uint32_t pll_lock_addr)
{
  volatile uint32_t timeout = 1000000;

  while (timeout > 0)
    {
      if (getreg32(pll_lock_addr) & GPLL_LOCK_MASK)
        {
          return true;  /* PLL locked */
        }

      timeout--;
    }

  return false;  /* Timeout -- PLL did not lock */
}

/****************************************************************************
 * Name: rv1126b_warn_gpll_unlocked
 *
 * Description:
 *   Emit a short GPLL-not-locked warning through riscv_lowputc().
 *   Uses character-by-character output to avoid depending on higher-level
 *   console subsystems that may not be available this early in boot.
 *
 ****************************************************************************/

static void rv1126b_warn_gpll_unlocked(void)
{
  const char *msg = "\r\nWARNING: GPLL not locked\r\n";
  int i;

  for (i = 0; msg[i] != '\0'; i++)
    {
      riscv_lowputc(msg[i]);
    }
}

/****************************************************************************
 * Name: rv1126b_ensure_gpll_locked
 *
 * Description:
 *   Verify that GPLL is already locked at 1188 MHz.  In an AMP
 *   environment the Linux A-core (or bootrom) will have configured
 *   GPLL before the HPMCU starts.  We do NOT reconfigure GPLL here
 *   to avoid interfering with other cores that depend on it.
 *
 *   If GPLL is not locked, emit a warning via riscv_lowputc() and let
 *   the caller return without changing clocks.
 *
 ****************************************************************************/

static bool rv1126b_ensure_gpll_locked(void)
{
  if (!rv1126b_wait_pll_lock(GPLL_LOCK_ADDR))
    {
      rv1126b_warn_gpll_unlocked();
      return false;
    }

  return true;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_clockconfig
 *
 * Description:
 *   Verify the preconfigured system clocks for the RV1126B HPMCU.
 *
 *   This function is called from rv1126b_start() very early in the boot
 *   sequence.  In an AMP environment, GPLL and the HPMCU clock are already
 *   configured by the Linux A-core or bootrom.  It verifies that GPLL is
 *   locked without reconfiguring either clock.  If GPLL is not locked, a
 *   warning is emitted via riscv_lowputc() and the function returns with
 *   clocks unchanged.
 *
 ****************************************************************************/

void rv1126b_clockconfig(void)
{
  /* Step 1: Verify GPLL is locked (already configured by A-core/bootrom).
   * Do NOT reconfigure GPLL to avoid disrupting Linux on the A-core.
   * If GPLL is not locked, leave clocks unchanged.
   */

  if (!rv1126b_ensure_gpll_locked())
    {
      return;
    }

  /* HPMCU clock configuration remains owned by the A-core or bootrom. */
}
