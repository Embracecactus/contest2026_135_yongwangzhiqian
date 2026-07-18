/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_timerisr.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Beken BK7258 (T5-AI, Cortex-M33) SysTick system-timer init for NuttX N2.
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

/* UART1 MMIO for the boot-trace marker pushed at the top of
 * up_timer_initialize().  Freestanding polled putc (polls fifo_status.bit20,
 * writes fifo_port); identical to start.c::bk7258_early_putc and
 * vectors.c::bk7258_fault_putc.  Local to this translation unit so it
 * introduces no new linkage dependency.
 */

#define BK7258_TMR_UART1_FSTAT  (*(volatile uint32_t *)0x45830018u)
#define BK7258_TMR_UART1_FPORT  (*(volatile uint32_t *)0x4583001Cu)
#define BK7258_TMR_UART1_READY  (1u << 20)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Bare MMIO single-byte marker.  Emits 'T' at function entry of
 * up_timer_initialize() so board-side observation can confirm the SysTick
 * bring-up was reached during the nx_start() walk.
 */

static void bk7258_timer_diag_putc(unsigned char c)
{
  while ((BK7258_TMR_UART1_FSTAT & BK7258_TMR_UART1_READY) == 0)
    {
    }

  BK7258_TMR_UART1_FPORT = (uint32_t)(c & 0xffu);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   Called during start-up (up_initialize) to initialise the timer
 *   hardware that drives the NuttX system clock.  Registers the common
 *   Cortex-M SysTick lower half, clocked at CONFIG_CPU_FREQ_HZ.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  uint32_t cpu_hz;
  uint32_t reload;

  /* Boot-trace marker: reached up_timer_initialize() inside nx_start(). */

  bk7258_timer_diag_putc('T');

  /* Decode the runtime core-clock frequency from the live M1/ANA_REG5
   * state (read-only) and derive the SysTick reload for one OS tick.  No
   * DPLL, mux, voltage or UART-divisor write is performed -- only the
   * SysTick reload bookkeeping is recomputed.
   *
   *   Baseline (M1=0, dplle=0):      cpu_hz = 26000000  reload = 0x0027ac3f
   *   Loader --reboot 1 (M1=0x423):  cpu_hz = 80000000  reload = 0x007a11ff
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

  /* N4-D0 read-only SysTick baseline.  All SysTick writes inside
   * up_timer_initialize() (the RELOAD write above + the CTRL write issued by
   * systick_initialize()) are now committed, so read back CSR/RVR/CVR and
   * echo the runtime reload and cpu_hz for comparison.  No new SysTick
   * write is introduced here; the existing reload write is the only one
   * and is not a clock-control write.
   */

  bk7258_clockdiag_systick_dump(reload, cpu_hz);

  /* Boot-trace marker: up_timer_initialize() is about to return normally.
   * Lower-case 't' is distinct from the entry marker 'T' above, so board-side
   * observation can tell a function-body hang (only 'T' seen) from a clean
   * return that hands control back to clock_initialize() (Tt seen, hang is
   * then somewhere between here and arm_serialinit()'s 'C').
   */

  bk7258_timer_diag_putc('t');
}
