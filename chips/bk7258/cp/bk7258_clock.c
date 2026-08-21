/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_clock.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 boot-time CPU0 clock bring-up -- thin DVFS caller.
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
 * CONFIG_BK7258_CLOCK_320M is only the current bring-up profile.  Its helper
 * is a single
 * bk7258_dvfs_set_freq(BK7258_FREQ_320M) call, which steps the CPU0 core one
 * tier at a time from the 26 MHz boot residue up to the 320 MHz tier
 * (CPU0 effective 160 MHz on this single-core port; see bk7258_dvfs.h for the
 * SDK "cpu0:160m" note).  The DVFS lower half refreshes CPU-clocked DWT
 * conversion after the switch; scheduler SysTick stays on fixed 32 kHz.  It
 * is not a permanent governor decision: later runtime policy uses the same
 * set_freq() lower half to move between SDK tiers.
 *
 * Shared low-level register helpers (ANA_REG9 field/latch, M1 writes, the
 * analog-SPI wait, the bus-independent microsecond delay) live in
 * chip/cp/bk7258_clk_ll.h and are shared with bk7258_dvfs.c so boot-time and
 * runtime use byte-for-byte the same write/wait protocol.
 *
 * This file keeps only: the optional CONFIG_BK7258_CLOCK_320M_PROBE UART
 * evidence line (now expanded to print the live VDDD/VDDIG fields so a
 * board run confirms the SDK 320-tier voltages landed), and the bring-up
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

#ifdef CONFIG_BK7258_CLOCK_320M_PROBE
extern void arm_lowputc(char ch);
#endif

/****************************************************************************
 * Private Functions (UART probe helpers only)
 ****************************************************************************/

#ifdef CONFIG_BK7258_CLOCK_320M_PROBE
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
#endif /* CONFIG_BK7258_CLOCK_320M_PROBE */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bk7258_clock_bringup_320m(void)
{
  int tier;

  /* Stepping the core clock from the BL1 120 MHz handoff to the 320 MHz
   * runtime tier is the DVFS lower half's job: it walks the remaining SDK
   * per-tier voltage/mux program one step at a time (VDDD -> 0x7,
   * VDDIG -> 0xE, CPU0 /2, then M1
   * cksel=2/clkdiv=0) and finally refreshes the DWT conversion.  Scheduler
   * SysTick remains on fixed 32 kHz.  The DPLL was already enabled by the
   * bootloader's boot_clock.c cold-init (cold path) or is already running
   * (loader --reboot 1 residue on the soft path). */

  bk7258_dvfs_set_freq(BK7258_FREQ_320M);
  tier = bk7258_dvfs_get_freq();

#ifdef CONFIG_BK7258_CLOCK_320M_PROBE
  {
    uint32_t m1 = BK7258_REG(BK7258_CPU_CLK_DIV_MODE1);
    uint32_t r5 = BK7258_REG(BK7258_ANA_REG5);
    uint32_t r9 = BK7258_REG(BK7258_ANA_REG9);

    uint32_t vddd  = (r9 >> BK7258_ANA9_VDDD_SHIFT)  & 0x7u;
    uint32_t vddig = (r9 >> BK7258_ANA9_VDDDIG_SHIFT) & 0xfu;
    /* M1 cksel_core is bits [5:4], clkdiv_core is bits [3:0] (sys_reg.h). */
    uint32_t cksel = (m1 >> 4) & 0x3u;
    uint32_t cdiv  = m1 & 0xfu;

    bk7258_clk_puts("N4Clk tier=");
    bk7258_clk_puthex8((uint32_t)tier);
    bk7258_clk_puts(" M1=");
    bk7258_clk_puthex8(m1);
    bk7258_clk_puts("(cs=");
    bk7258_clk_puthex8(cksel);
    bk7258_clk_puts(" cd=");
    bk7258_clk_puthex8(cdiv);
    bk7258_clk_puts(") A5=");
    bk7258_clk_puthex8(r5);
    bk7258_clk_puts(" A9=");
    bk7258_clk_puthex8(r9);
    bk7258_clk_puts("(VDDD=");
    bk7258_clk_puthex8(vddd);
    bk7258_clk_puts(" VDDIG=");
    bk7258_clk_puthex8(vddig);
    bk7258_clk_puts(") hz=");
    bk7258_clk_puthex8(bk7258_clockdiag_current_cpu_hz());
    bk7258_clk_puts("\r\n");
  }
#else
  (void)tier;
#endif
}
