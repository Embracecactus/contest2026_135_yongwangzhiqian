/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/common/bk7258_timerisr.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, Cortex-M33) SysTick system-timer init for NuttX.
 *
 * Modelled on nuttx/arch/arm/src/mps/mps_timer.c.  up_timer_initialize()
 * loads the SysTick reload value for the configured tick rate and registers
 * the common armv8-m SysTick lower half (arm_systick.c) as the system clock
 * source.
 *
 * Clock assumptions (see docs/bk7258-t5ai + armino sdkconfig.h):
 *   - The Tier-1 bootloader does NOT enable the DPLL; the app core runs at
 *     the BootROM default = the 26 MHz XTAL.  Hence CONFIG_CPU_FREQ_HZ =
 *     26000000.
 *   - SysTick CLKSOURCE = processor clock (no /8 divisor), so the SysTick
 *     clock equals CONFIG_CPU_FREQ_HZ.
 *
 *   Reload = cpu_hz / CLK_TCK - 1, where cpu_hz is decoded at runtime from
 *            the live clock registers by bk7258_clockdiag_current_cpu_hz():
 *
 *              baseline (M1=0, dplle=0)         -> 26000000 Hz
 *              loader --reboot 1 (M1=0x423)     -> 80000000 Hz
 *              unknown                           -> 26000000 Hz (fallback)
 *
 *            No DPLL/mux/voltage/UART-divisor write is performed; only the
 *            SysTick reload bookkeeping is recomputed.
 *
 *   BOARD_CPU_FREQ_HZ in board.h remains the build-time baseline / fallback
 *   and matches the runtime baseline-case return; it is no longer used
 *   directly in the reload path.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/clock.h>
#include <nuttx/timers/arch_timer.h>
#include <arch/board/board.h>
#include <arch/irq.h>

#include "arm_internal.h"
#include "systick.h"
#include "nvic.h"
#include "bk7258_clockdiag.h"
#include "bk7258_dvfs.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SysTick reload for one OS tick is computed at runtime in
 * up_timer_initialize() from bk7258_clockdiag_current_cpu_hz() and CLK_TCK
 * (= 1000000 / CONFIG_USEC_PER_TICK).  BOARD_CPU_FREQ_HZ (board.h) remains
 * the documented build-time baseline and equals the baseline-case runtime
 * return, but is no longer used directly in the reload path.
 */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   Called during start-up (up_initialize) to initialise the timer
 *   hardware that drives the NuttX system clock.  Registers the common
 *   Cortex-M SysTick lower half, clocked at the runtime-detected cpu_hz.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  uint32_t cpu_hz;
  uint32_t reload;

  /* Decode the runtime core-clock frequency from the live M1/ANA_REG5
   * state (read-only) and derive the SysTick reload for one OS tick.  No
   * DPLL, mux, voltage or UART-divisor write is performed -- only the
   * SysTick reload bookkeeping is recomputed.
   *
   *   Baseline (M1=0, dplle=0):      cpu_hz = 26000000
   *   Loader --reboot 1 (M1=0x423):  cpu_hz = 80000000
   *
   * systick_initialize() will then arm CLKSOURCE | TICKINT (core clock,
   * interrupt on wrap) and attach the SysTick ISR; setting the reload first
   * guarantees the first tick has the correct period (some QEMU/model cores
   * ignore CTRL writes when RELOAD is zero, so follow the mps_timer.c
   * ordering).
   */

  cpu_hz = bk7258_clockdiag_current_cpu_hz();
  reload = (cpu_hz / CLK_TCK) - 1;

  putreg32(reload, NVIC_SYSTICK_RELOAD);

  /* coreclk=true  -> SysTick clocked at the processor clock (cpu_hz).
   * minor=-1      -> do not register a /dev/timerN node; this timer is the
   *                  dedicated system clock only.
   */

  up_timer_set_lowerhalf(systick_initialize(true, cpu_hz, -1));
}

/****************************************************************************
 * Name: bk7258_systick_recalc
 *
 * Description:
 *   Recompute and atomically rewrite the SysTick one-tick reload for the
 *   live core clock, after a runtime DVFS frequency switch.  SysTick is
 *   clocked at the CPU0 processor clock (CLKSOURCE = processor clock, no
 *   /8 divisor), so every CPU frequency change must update RELOAD here or
 *   the OS tick period drifts (sleep N becomes N * (old_hz / new_hz)).
 *
 *   Reads M1/ANA_REG5 via bk7258_clockdiag_current_cpu_hz() (read-only) and
 *   writes only RELOAD + CVR.  Clearing CVR after touching RELOAD is the
 *   ARMv8-M architecturally mandated sequence to apply a new reload on the
 *   next wrap (CVR is write-to-clear).
 *
 *   This keeps the NuttX common arm_systick.c upstream untouched (the
 *   systick_lowerhalf_s.freq field is NOT updated here -- it is only used
 *   for usec/count conversions in optional timer APIs, not for the tick
 *   cadence, which is governed by RELOAD).  If a future caller needs
 *   accurate usec-scale timeouts after a switch, that is a separate task.
 *
 *   Caller (bk7258_dvfs_set_freq) holds interrupts disabled across the
 *   whole mux+reload sequence, so no extra irqsave is taken here.
 *
 ****************************************************************************/

#ifdef CONFIG_BK7258_DVFS
void bk7258_systick_recalc(void)
{
  uint32_t cpu_hz;
  uint32_t reload;

  cpu_hz = bk7258_clockdiag_current_cpu_hz();
  reload = (cpu_hz / CLK_TCK) - 1;

  /* Use the BK7258_CDIAG_SYSTICK_* address macros (always present in
   * bk7258_clockdiag.h) rather than risking a backend-specific
   * NVIC_SYSTICK_CURRENT definition.  RVR is RELOAD, CVR is the
   * write-to-clear current-counter register. */
  putreg32(reload, BK7258_CDIAG_SYSTICK_RVR);

  /* Write any value to CVR to clear it; the SysTick then counts down from
   * RELOAD so the first tick is one full tick long at the new period. */
  putreg32(0, BK7258_CDIAG_SYSTICK_CVR);
}
#endif /* CONFIG_BK7258_DVFS */
