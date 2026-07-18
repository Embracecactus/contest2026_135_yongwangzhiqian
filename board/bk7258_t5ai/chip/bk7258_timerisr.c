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
 *   Reload = CONFIG_CPU_FREQ_HZ / CLK_TCK - 1
 *          = 26000000 / (1000000 / CONFIG_USEC_PER_TICK) - 1.
 *   With CONFIG_USEC_PER_TICK=1000 (1 ms, 1000 Hz) this is 25999, well
 *   inside the 24-bit SysTick range.
 *
 *   Board-side calibration TODO: if a future BSP enables the DPLL (e.g.
 *   160 MHz via 480M/3), CONFIG_CPU_FREQ_HZ must be updated to match and
 *   the reload recomputes automatically.
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

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SysTick reload for one OS tick.  CLK_TCK = 1000000 / CONFIG_USEC_PER_TICK
 * (ticks per second), derived from the configured tick period.  BOARD_CPU_FREQ_HZ
 * (board.h) is the SysTick source frequency (processor clock).
 */

#define BK7258_SYSTICK_RELOAD  ((BOARD_CPU_FREQ_HZ / CLK_TCK) - 1)

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
  /* Boot-trace marker: reached up_timer_initialize() inside nx_start(). */

  bk7258_timer_diag_putc('T');

  /* Program the reload for one OS tick.  systick_initialize() will then
   * arm CLKSOURCE | TICKINT (core clock, interrupt on wrap) and attach the
   * SysTick ISR; setting the reload first guarantees the first tick has the
   * correct period (some QEMU/model cores ignore CTRL writes when RELOAD
   * is zero, so follow the mps_timer.c ordering).
   */

  putreg32(BK7258_SYSTICK_RELOAD, NVIC_SYSTICK_RELOAD);

  /* coreclk=true  -> SysTick clocked at the processor clock (BOARD_CPU_FREQ_HZ).
   * minor=-1      -> do not register a /dev/timerN node; this timer is the
   *                  dedicated system clock only.
   */

  up_timer_set_lowerhalf(systick_initialize(true, BOARD_CPU_FREQ_HZ, -1));

  /* Boot-trace marker: up_timer_initialize() is about to return normally.
   * Lower-case 't' is distinct from the entry marker 'T' above, so board-side
   * observation can tell a function-body hang (only 'T' seen) from a clean
   * return that hands control back to clock_initialize() (Tt seen, hang is
   * then somewhere between here and arm_serialinit()'s 'C').
   */

  bk7258_timer_diag_putc('t');
}
