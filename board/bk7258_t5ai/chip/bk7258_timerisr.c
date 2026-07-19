/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_timerisr.c
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

#include "arm_internal.h"
#include "systick.h"
#include "nvic.h"
#include "bk7258_clockdiag.h"

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
