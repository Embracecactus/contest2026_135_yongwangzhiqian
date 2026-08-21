/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_dvfs.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 runtime CPU-frequency switching (DVFS) -- NuttX overlay
 * lower half.  Mirrors the Armino SDK runtime clock path:
 *
 *   sys_drv_switch_cpu_bus_freq(target)        (sys_ps_driver.c:244-289)
 *     loop prev远近 target, each step calls
 *     sys_hal_switch_cpu_bus_freq_low_to_high(i)  (sys_hal.c:620-686) or
 *     sys_hal_switch_cpu_bus_freq_high_to_low(i)  (sys_hal.c:548-619)
 *       which does: ctrl_vddd_h_vol(vddd) + ctrl_vdddig_h_vol(vddig)
 *                   core_bus_clock_ctrl(cksel, clkdiv_core, ckdiv_bus,
 *                                       ckdiv_cpu0, ckdiv_cpu1)
 *
 * This is a *runtime* API: the bootloader's boot_clock.c mirrors
 * sys_hal_early_init (DPLL enable + SPI recalibration, analog side left at
 * SDK default VDDIG=0xB) and the official A/B bootloader's 120 MHz handoff.
 * Any per-tier lift of VDDD/VDDIG happens here, one tier at a time, so the
 * voltage rails move monotonically (no abrupt jump), exactly as the SDK
 * sys_drv_switch_cpu_bus_freq models.
 *
 * The register-switching lower half follows the NuttX lc823450 DVFS
 * precedent.  bk7258_pm_policy.c owns the stock NuttX PM integration and
 * v3.1.1.9-compatible multi-client frequency aggregation.
 *
 * The whole per-tier switch (voltage -> dividers -> mux) is atomic with
 * respect to interrupts via irqsave()/irqrestore().  Scheduler SysTick uses
 * the independent 32-kHz route; only DWT conversion follows the CPU mux.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_DVFS

#include <stdint.h>
#include <errno.h>

#include <nuttx/spinlock.h>

#include "arm_internal.h"
#include "bk7258_clk_ll.h"
#include "bk7258_dvfs.h"
#include "bk7258_clockdiag.h"

/****************************************************************************
 * Private types
 ****************************************************************************/

/* One operating-point tier.  Fields map 1:1 to the SDK case table in
 * sys_hal.c:548-686.  <vddd> is the vdighsel field value (3 bits, the 3-bit
 * raw the SDK passes to sys_hal_ctrl_vddd_h_vol), <vddig> the vcorehsel
 * field value (4 bits raw passed to sys_hal_ctrl_vdddig_h_vol).
 */
struct bk7258_dvfs_step_s
{
  uint8_t  cksel_core;    /* M1 cksel_core [5:4]                */
  uint8_t  clkdiv_core;   /* M1 clkdiv_core [3:0]               */
  uint8_t  cpu0_speed;    /* CPU0_INT_HALT_CLK_OP cpu0_speed [4]: 0=/2, 1=/1 */
  uint8_t  vddd;          /* vdighsel raw (VDDD)                */
  uint8_t  vddig;         /* vcorehsel raw (VDDIG)              */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Tier table -- one entry per BK7258_FREQ_*.  Order MUST be ascending
 * (bk7258_dvfs_set_freq() steps ++/-- between prev and target).
 *
 * Values are the SDK-defined operating points (sys_hal.c:548-686 case fields
 * for PM_CPU_FRQ_26M..480M), unconditionally the non-DCO / non-ATE branch
 * because our port does not enable DCO_CLK.  The cpu0_speed field value
 * matches sys_hal_cpu_clk_div_set(0, ckdiv_cpu0): for every tier the SDK
 * passes ckdiv_cpu0 = 0x1 to set cpu0_speed = 1 (/1) for the 26..240 MHz
 * tiers.  The 320 MHz tier is the exception: the SDK passes ckdiv_cpu0 = 0,
 * selecting /2 so physical CPU0 runs at 160 MHz while CPU1/CPU2 run at the
 * full 320 MHz core clock.  bk7258_clockdiag_current_cpu_hz() reports the
 * role-specific value used to refresh DWT conversion; SysTick stays at
 * 32 kHz.
 *
 * The 320 MHz tier yields CPU0 = 160 MHz.  The 480 MHz tier follows the
 * v3.1.1.9 video operating point: CPU0/bus = 240 MHz while the AP physical
 * CPU1/CPU2 cores run at 480 MHz. */
static const struct bk7258_dvfs_step_s g_bk7258_dvfs_steps[] =
{
  /* tier 0:  26  MHz  cksel=0x0 clkdiv=0x0  VDDD=0x6 VDDIG=0xB */
  [BK7258_FREQ_26M]  = { 0x0, 0x0, 0x1, 0x6, 0xB },
  /* tier 1:  60  MHz  cksel=0x3 clkdiv=0x7  VDDD=0x6 VDDIG=0xB */
  [BK7258_FREQ_60M]  = { 0x3, 0x7, 0x1, 0x6, 0xB },
  /* tier 2:  80  MHz  cksel=0x3 clkdiv=0x5  VDDD=0x6 VDDIG=0xB */
  [BK7258_FREQ_80M]  = { 0x3, 0x5, 0x1, 0x6, 0xB },
  /* tier 3: 120 MHz  cksel=0x3 clkdiv=0x3  VDDD=0x6 VDDIG=0xC */
  [BK7258_FREQ_120M] = { 0x3, 0x3, 0x1, 0x6, 0xC },
  /* tier 4: 240 MHz  cksel=0x3 clkdiv=0x1  VDDD=0x6 VDDIG=0xD */
  [BK7258_FREQ_240M] = { 0x3, 0x1, 0x1, 0x6, 0xD },
  /* tier 5: 320 MHz (CPU0=160)  cksel=0x2 clkdiv=0x0  VDDD=0x7 VDDIG=0xE */
  [BK7258_FREQ_320M] = { 0x2, 0x0, 0x0, 0x7, 0xE },
  /* tier 6: 480 MHz (CPU0=240)  cksel=0x3 clkdiv=0x0  VDDD=0x7 VDDIG=0xE */
  [BK7258_FREQ_480M] = { 0x3, 0x0, 0x0, 0x7, 0xE },
};

/* BL1 now enforces the recovered official 120 MHz handoff on cold and warm
 * paths before BL2/NuttX runs.  Start the runtime state machine from that
 * real operating point; the normal 320 MHz bring-up therefore walks only
 * 120 -> 240 -> 320 instead of first detouring through lower tiers. */

static int g_bk7258_dvfs_cur = BK7258_FREQ_120M;

/****************************************************************************
 * Private: ANA_REG9 voltage setters (mirror SDK sys_hal_ctrl_vddd(_ig)_h_vol)
 ****************************************************************************/

/* Write the vdighsel field (VDDD) of ANA_REG9 to <v>.  Latch + settle exactly
 * per sys_hal_ctrl_vddd_h_vol (sys_hal.c:517-529). */
static void bk7258_ctrl_vddd_h_vol(uint8_t v)
{
  uint32_t set = (uint32_t)v << BK7258_ANA9_VDDD_SHIFT;

  if ((BK7258_REG(BK7258_ANA_REG9) & BK7258_ANA9_VDDD_MASK) != set)
    {
      bk7258_ana9_set_field(BK7258_ANA9_VDDD_MASK, set);
      bk7258_clk_delay(BK7258_VDD_SETTLE_ITERS);
    }
}

static void bk7258_ctrl_vdddig_h_vol(uint8_t v)
{
  uint32_t set = (uint32_t)v << BK7258_ANA9_VDDDIG_SHIFT;

  if ((BK7258_REG(BK7258_ANA_REG9) & BK7258_ANA9_VDDDIG_MASK) != set)
    {
      bk7258_ana9_set_field(BK7258_ANA9_VDDDIG_MASK, set);
      bk7258_clk_delay(BK7258_VDD_SETTLE_ITERS);
    }
}

/****************************************************************************
 * Private: core/bus mux control (mirror SDK sys_hal_core_bus_clock_ctrl)
 *
 * SDK low_to_high ordering (sys_hal.c:483-512): when going UP first set the
 * cpu0 divider (avoid bus > 240 M at clkdiv_core == 0), then clkdiv_core,
 * then cpu0/cpu1 dividers, then cksel last; barriers afterwards.  We mirror
 * that for the up direction; the down mirror reorders exactly the SDK way
 * (sys_hal_high_to_low cortex-low-to-high handles low_to_high separately).
 *
 * CP is physical CPU0 and AP uses physical CPU1/CPU2.  Match the SDK by
 * programming CPU0 from the tier table and keeping CPU1/CPU2 at /1.
 ****************************************************************************/

static void bk7258_write_cpu_speed(uintptr_t reg, uint8_t speed)
{
  uint32_t v = BK7258_REG(reg);

  if (speed)
    {
      v |= BK7258_CPU_SPEED_BIT;
    }
  else
    {
      v &= ~BK7258_CPU_SPEED_BIT;
    }

  BK7258_REG(reg) = v;
}

static void bk7258_write_clkdiv_core(uint8_t clkdiv_core)
{
  uint32_t v = BK7258_REG(BK7258_CPU_CLK_DIV_MODE1);

  v = (v & ~BK7258_M1_CLKDIV_MASK) | (clkdiv_core & BK7258_M1_CLKDIV_MASK);
  BK7258_REG(BK7258_CPU_CLK_DIV_MODE1) = v;
}

static void bk7258_write_cksel_core(uint8_t cksel_core)
{
  uint32_t v = BK7258_REG(BK7258_CPU_CLK_DIV_MODE1);

  v = (v & ~BK7258_M1_CKSEL_MASK)  |
      ((uint32_t)cksel_core << BK7258_M1_CKSEL_SHIFT);
  BK7258_REG(BK7258_CPU_CLK_DIV_MODE1) = v;
}

/* Low-to-high tier switch: lift VDDD then VDDIG (when the target voltage is
 * higher than the current), then re-order the dividers before the mux. */
static void bk7258_dvfs_step_low_to_high(const struct bk7258_dvfs_step_s *s)
{
  /* SDK sys_hal_switch_cpu_bus_freq_low_to_high raises VDDD/VDDIG first,
   * then core_bus_clock_ctrl.  We lift VDDD, then VDDIG. */
  bk7258_ctrl_vddd_h_vol(s->vddd);
  bk7258_ctrl_vdddig_h_vol(s->vddig);

  /* core_bus_clock_ctrl up ordering: clkdiv_core==0 path writes the cpu
   * divider first (avoid bus > 240 M), then clkdiv_core, then cksel. */
  if (s->clkdiv_core == 0)
    {
      bk7258_write_cpu_speed(BK7258_CPU0_HALT_CLK_OP, s->cpu0_speed);
    }

  bk7258_write_clkdiv_core(s->clkdiv_core);

  if (s->clkdiv_core != 0)
    {
      bk7258_write_cpu_speed(BK7258_CPU0_HALT_CLK_OP, s->cpu0_speed);
    }

  bk7258_write_cpu_speed(BK7258_CPU1_HALT_CLK_OP, 1);
  bk7258_write_cpu_speed(BK7258_CPU2_HALT_CLK_OP, 1);
  bk7258_write_cksel_core(s->cksel_core);

  __asm__ volatile ("dsb 0xf" ::: "memory");
  __asm__ volatile ("isb 0xf" ::: "memory");
}

/* High-to-low tier switch: switch mux/dividers first, then lower VDDIG/VDDD
 * (per SDK sys_hal_switch_cpu_bus_freq_high_to_low ordering). */
static void bk7258_dvfs_step_high_to_low(const struct bk7258_dvfs_step_s *s)
{
  /* SDK high_to_low ordering: cksel first, then dividers, then voltages. */
  bk7258_write_cksel_core(s->cksel_core);
  bk7258_write_clkdiv_core(s->clkdiv_core);
  bk7258_write_cpu_speed(BK7258_CPU0_HALT_CLK_OP, s->cpu0_speed);
  bk7258_write_cpu_speed(BK7258_CPU1_HALT_CLK_OP, 1);
  bk7258_write_cpu_speed(BK7258_CPU2_HALT_CLK_OP, 1);

  __asm__ volatile ("dsb 0xf" ::: "memory");
  __asm__ volatile ("isb 0xf" ::: "memory");

  /* Lower VDDIG, then VDDD. */
  bk7258_ctrl_vdddig_h_vol(s->vddig);
  bk7258_ctrl_vddd_h_vol(s->vddd);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_dvfs_set_freq
 ****************************************************************************/

int bk7258_dvfs_set_freq(int tier)
{
  int prev;
  irqstate_t flags;
  int i;

  if (tier < BK7258_FREQ_MIN || tier > BK7258_FREQ_MAX)
    {
      return -EINVAL;
    }

  /* The whole step sequence is atomic wrt ISRs because an IRQ inside the
   * analog-SPI sequence can lose a write to the analog block. */
  flags = enter_critical_section();

  prev = g_bk7258_dvfs_cur;
  if (prev != tier)
    {
      if (tier > prev)
        {
          for (i = prev + 1; i <= tier; i++)
            {
              bk7258_dvfs_step_low_to_high(&g_bk7258_dvfs_steps[i]);
            }
        }
      else
        {
          for (i = prev - 1; i >= tier; i--)
            {
              bk7258_dvfs_step_high_to_low(&g_bk7258_dvfs_steps[i]);
            }
        }

      g_bk7258_dvfs_cur = tier;

      /* SysTick stays on fixed 32 kHz.  Refresh CPU-clocked DWT conversion
       * before interrupts are re-enabled. */
      bk7258_systick_recalc();
    }

  leave_critical_section(flags);
  return 0;
}

int bk7258_dvfs_get_freq(void)
{
  return g_bk7258_dvfs_cur;
}

#endif /* CONFIG_BK7258_DVFS */
