/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_clock.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP/CPU0 performance startup -- thin DVFS caller.
 *
 * This used to be a hand-rolled analog/mux bring-up that mirrored only a
 * fragment of the Armino SDK early-init path.  The product-grade model now
 * matches the SDK exactly:
 *
 *   - the bootloader's boot_clock.c leaves BL2/NuttX at the recovered
 *     official 120 MHz safe handoff point;
 *   - per-tier frequency selection is a *runtime* concern solved by
 *     bk7258_dvfs.c, the NuttX overlay lower half that mirrors the SDK
 *     sys_drv_switch_cpu_bus_freq / sys_hal_switch_cpu_bus_freq path
 *     (lift VDDD/VDDIG one tier at a time, then switch the core mux).
 *
 * CONFIG_BK7258_CLOCK_240M is the CP-only performance profile.  Its helper
 * calls bk7258_dvfs_set_opp(BK7258_OPP_240M), moving from the official
 * 120 MHz boot handoff to PM_CPU_FRQ_240M.  That SDK operating point gives
 * CPU0/AP/bus 240 MHz and is the highest official effective CPU0 frequency.
 * The similarly named 320M/480M SDK operating points remain available for
 * AP votes but give CPU0 only 160/240 MHz respectively.
 *
 * The DVFS lower half refreshes CPU-clocked DWT conversion after the switch;
 * scheduler SysTick stays on fixed 32 kHz.  This startup target is not a
 * permanent governor decision: later runtime policy uses the same set_freq()
 * lower half to move between SDK operating points.
 *
 * Shared low-level register helpers (ANA_REG9 field/latch, M1 writes, the
 * analog-SPI wait, the bus-independent microsecond delay) live in
 * chip/cp/bk7258_clk_ll.h and are shared with bk7258_dvfs.c so boot-time and
 * runtime use byte-for-byte the same write/wait protocol.
 *
 * This file keeps only: the optional CONFIG_BK7258_CLOCK_240M_PROBE UART
 * evidence line (now expanded to print the live VDDD/VDDIG fields so a
 * board run confirms the SDK 240M operating-point voltages landed), and the
 * startup
 * entry point.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "arm_internal.h"
#include "bk7258_clk_ll.h"
#include "bk7258_clock.h"
#include "bk7258_dvfs.h"
#include "bk7258_clockdiag.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_BK7258_CLOCK_240M_PROBE
extern void arm_lowputc(char ch);
#endif

/****************************************************************************
 * Private Functions (UART probe helpers only)
 ****************************************************************************/

#ifdef CONFIG_BK7258_CLOCK_240M_PROBE
static void bk7258_clk_putc(unsigned char c)
{
  arm_lowputc((char)c);
}

static void bk7258_clk_puts(const char *s)
{
  while (*s)
    {
      bk7258_clk_putc((unsigned char)*s++);
    }
}

static void bk7258_clk_puthex8(uint32_t v)
{
  static const char hex[] = "0123456789abcdef";
  int i;

  for (i = 7; i >= 0; i--)
    {
      bk7258_clk_putc((unsigned char)hex[(v >> (i * 4)) & 0xfu]);
    }
}

static void bk7258_clock_probe(int tier)
{
  uint32_t m1 = BK7258_REG(BK7258_CPU_CLK_DIV_MODE1);
  uint32_t c0 = BK7258_REG(BK7258_CPU0_HALT_CLK_OP);
  uint32_t r5 = BK7258_REG(BK7258_ANA_REG5);
  uint32_t r9 = BK7258_REG(BK7258_ANA_REG9);
  uint32_t vddd = (r9 >> BK7258_ANA9_VDDD_SHIFT) & 0x7u;
  uint32_t vddig = (r9 >> BK7258_ANA9_VDDDIG_SHIFT) & 0xfu;
  uint32_t cksel = (m1 >> 4) & 0x3u;
  uint32_t cdiv = m1 & 0xfu;
  uint32_t c0div = (c0 & BK7258_CPU_SPEED_BIT) != 0 ? 1u : 2u;

  bk7258_clk_puts("ClockOPP tier=");
  bk7258_clk_puthex8((uint32_t)tier);
  bk7258_clk_puts(" M1=");
  bk7258_clk_puthex8(m1);
  bk7258_clk_puts("(cs=");
  bk7258_clk_puthex8(cksel);
  bk7258_clk_puts(" cd=");
  bk7258_clk_puthex8(cdiv);
  bk7258_clk_puts(" c0div=");
  bk7258_clk_puthex8(c0div);
  bk7258_clk_puts(") A5=");
  bk7258_clk_puthex8(r5);
  bk7258_clk_puts(" A9=");
  bk7258_clk_puthex8(r9);
  bk7258_clk_puts("(VDDD=");
  bk7258_clk_puthex8(vddd);
  bk7258_clk_puts(" VDDIG=");
  bk7258_clk_puthex8(vddig);
  bk7258_clk_puts(") cpu0_hz=");
  bk7258_clk_puthex8(bk7258_clockdiag_current_cpu_hz());
  bk7258_clk_puts(" bus_hz=");
  bk7258_clk_puthex8(bk7258_clockdiag_current_bus_hz());
  bk7258_clk_puts("\r\n");
}
#endif /* CONFIG_BK7258_CLOCK_240M_PROBE */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bk7258_clock_bringup_240m(void)
{
  int tier;

  /* Move from the BL1 120 MHz handoff to the SDK PM_CPU_FRQ_240M operating
   * point.  The DVFS lower half applies VDDD=0x6, VDDIG=0xD, M1 cksel=3 /
   * clkdiv=1 and CPU0 speed=/1 in the SDK order, then refreshes the DWT
   * conversion.  Scheduler SysTick remains on fixed 32 kHz.  The DPLL was
   * already enabled by boot_clock.c on a cold path or is already running on
   * a loader soft-reset path.
   */

  bk7258_dvfs_set_opp(BK7258_OPP_240M);
  tier = bk7258_dvfs_get_opp();

#ifdef CONFIG_BK7258_CLOCK_240M_PROBE
  bk7258_clock_probe(tier);
#else
  (void)tier;
#endif
}
