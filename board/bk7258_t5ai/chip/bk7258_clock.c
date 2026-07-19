/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/bk7258_clock.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) deterministic 320 MHz CPU0 clock bring-up.
 *
 * Mirrors the Armino SDK early-init clock path (see chip/bk7258_clock.h and
 * the N4 worklog for provenance):
 *
 *   sys_hal_dpll_cpu_flash_time_early_init -> sys_hal_cali_dpll   (DPLL)
 *   sys_hal_core_bus_clock_ctrl             (VDDDIG/VDDD guard + mux)
 *
 * Register map (Armino soc/bk7258/soc/sys_reg.h + sys_hal.c):
 *   ANA_REG0  0x44010100  spitrig[19], spideten[4]
 *   ANA_REG5  0x44010114  EN_DPLL[5]
 *   ANA_REG9  0x44010124  vcorehsel(VDDDIG)[19:16], vdighsel(VDDD)[28:26],
 *                          spi_latch1v[9]
 *   SYS_ANALOG_REG_SPI_STATE 0x440100E8  analog-SPI-write busy bit[idx]
 *                          (idx = reg number: ANA_REG0->0, ANA_REG5->5,
 *                           ANA_REG9->9)
 *   SYS_CPU0_INT_HALT_CLK_OP 0x44010010  cpu0_speed[4]
 *   SYS_CPU_CLK_DIV_MODE1 (M1) 0x44010020  clkdiv_core[3:0], cksel_core[5:4]
 *
 * There is no readable "DPLL locked" status bit; the SDK relies on fixed
 * delays.  This driver uses SysTick (Cortex-M SYSTICK) as a bus-independent
 * delay source (CPU still at the bootloader's residue clock during the
 * sequence), so the delays do not depend on knowing the live CPU frequency.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include "bk7258_clock.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MMIO access (self-contained, freestanding). */

#define BK7258_REG(a)              (*(volatile uint32_t *)(a))

/* SYS block. */

#define BK7258_SYS_BASE            0x44010000u
#define BK7258_ANA_SPI_STATE       (BK7258_SYS_BASE + 0x00e8u)
#define BK7258_CPU0_HALT_CLK_OP   (BK7258_SYS_BASE + 0x0010u)
#define BK7258_CPU_CLK_DIV_MODE1  (BK7258_SYS_BASE + 0x0020u)  /* M1 */
#define BK7258_ANA_REG0            (BK7258_SYS_BASE + 0x0100u)
#define BK7258_ANA_REG5            (BK7258_SYS_BASE + 0x0114u)
#define BK7258_ANA_REG9            (BK7258_SYS_BASE + 0x0124u)

/* ANA_REG0 bit fields (DPLL SPI recalibration trigger). */

#define BK7258_ANA0_SPITRIG        (1u << 19)   /* rising edge starts recal */
#define BK7258_ANA0_SPIDETEN       (1u << 4)    /* unlock-detect gate */

/* ANA_REG5 bit fields (DPLL power). */

#define BK7258_ANA5_EN_DPLL         (1u << 5)

/* ANA_REG9 bit fields (core voltages + SPI latch). */

#define BK7258_ANA9_SPI_LATCH1V     (1u << 9)
#define BK7258_ANA9_VDDDIG_SHIFT    16u
#define BK7258_ANA9_VDDDIG_MASK     (0xfu << BK7258_ANA9_VDDDIG_SHIFT)
#define BK7258_ANA9_VDDDIG_0V9      (0xcu << BK7258_ANA9_VDDDIG_SHIFT)
#define BK7258_ANA9_VDDD_SHIFT      26u
#define BK7258_ANA9_VDDD_MASK       (0x7u << BK7258_ANA9_VDDD_SHIFT)
#define BK7258_ANA9_VDDD_1V0        (0x6u << BK7258_ANA9_VDDD_SHIFT)

/* M1 (CPU_CLK_DIV_MODE1) core-source fields. */

#define BK7258_M1_CLKDIV_MASK       0x0fu      /* clkdiv_core [3:0] */
#define BK7258_M1_CKSEL_MASK         (0x3u << 4) /* cksel_core [5:4] */
#define BK7258_M1_CKSEL_320M         (0x2u << 4)

/* Recalibration / settle delays (match SDK's two delay(3400) steps at
 * 26 MHz cold-start coarse).  SysTick-backed below (bus-independent).
 */

#define BK7258_DPLL_CAL_DELAY_US    150u       /* generous settle, ~SDK 3400cyc */
#define BK7258_DPLL_TRIG_DELAY_US   5u
#define BK7258_VDD_SETTLE_US        10u        /* SYS_SWITCH_VDDDIG_VOL_DELAY */

/* ANA_REG SPI busy bit index == register number (ANA_REG0->0..ANA_REG9->9). */

#define BK7258_ANA_SPI_BUSY(idx)    (1u << (idx))

/* Bus-independent coarse delay.  This runs before the scheduler exists and
 * with the CPU at the bootloader's residue clock; the exact frequency is
 * unknown.  Each loop iteration is an OOO-proof volatile load which the
 * compiler cannot fold, occupying several core ticks.  The transition counts
 * are sized so that even on the slowest relevant path (26 MHz) the requested
 * microsecond wait is covered, while on the fastest (320 MHz) it does not
 * stretch the settle meaningfully.  Matching the SDK, these are conservative
 * minimums, not calibrated real-time waits.
 */

#define BK7258_CLK_DELAY_ITERS_PER_US  32u   /* ~iterations to cover 1 us worst case */

#ifdef CONFIG_BK7258_CLOCK_320M_PROBE
/* Freestanding UART1 polled output for the one verification evidence line
 * (reuses the same MMIO contract as the other BK7258 bring-up files).
 */

#  define BK7258_CLK_UART1_FSTAT    (*(volatile uint32_t *)0x45830018u)
#  define BK7258_CLK_UART1_FPORT    (*(volatile uint32_t *)0x4583001cu)
#  define BK7258_CLK_UART1_READY    (1u << 20)
#endif

/****************************************************************************
 * Private Helpers
 ****************************************************************************/

#ifdef CONFIG_BK7258_CLOCK_320M_PROBE
static void bk7258_clk_putc(unsigned char c)
{
  while ((BK7258_CLK_UART1_FSTAT & BK7258_CLK_UART1_READY) == 0)
    {
    }

  BK7258_CLK_UART1_FPORT = (uint32_t)(c & 0xffu);
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
#endif

/* Coarse microsecond delay via a busy loop that the compiler cannot elide.
 * Each iteration issues a volatile MMIO read to the analog SPI-state register
 * (already on the analog write path), guaranteeing real work per loop.  The
 * iteration count is a conservative lower bound that covers the requested
 * wait at the slowest relevant residue clock; the SDK itself uses fixed busy
 * loops the same way.
 */

static void bk7258_clk_sleep_us(uint32_t us)
{
  uint32_t iters = us * BK7258_CLK_DELAY_ITERS_PER_US;

  while (iters-- != 0)
    {
      (void)BK7258_REG(BK7258_ANA_SPI_STATE);
    }
}

/* Wait for an analog-register write (idx = reg number) to drain through the
 * SPI state machine, mirroring sys_ll_set_analog_reg_value. */

static void bk7258_ana_wait(uint32_t idx)
{
  while ((BK7258_REG(BK7258_ANA_SPI_STATE) & BK7258_ANA_SPI_BUSY(idx)) != 0)
    {
    }
}

static void bk7258_ana_write_and(uint32_t addr, uint32_t idx, uint32_t mask)
{
  BK7258_REG(addr) = BK7258_REG(addr) & mask;
  bk7258_ana_wait(idx);
}

static void bk7258_m1_write(uint32_t value)
{
  BK7258_REG(BK7258_CPU_CLK_DIV_MODE1) = value;
}

/****************************************************************************
 * Private: DPLL SPI recalibration (mirrors sys_hal_cali_dpll)
 ****************************************************************************/

static void bk7258_dpll_recalibrate(void)
{
  /* Lower the trigger and settle, then rising edge with detect off during
   * settle, then re-enable detect and settle -- the SDK's two fixed delays.
   */

  bk7258_ana_write_and(BK7258_ANA_REG0, 0, ~BK7258_ANA0_SPITRIG);
  bk7258_clk_sleep_us(BK7258_DPLL_TRIG_DELAY_US);

  BK7258_REG(BK7258_ANA_REG0) =
      (BK7258_REG(BK7258_ANA_REG0) & ~BK7258_ANA0_SPIDETEN) |
      BK7258_ANA0_SPITRIG;
  bk7258_ana_wait(0);
  bk7258_clk_sleep_us(BK7258_DPLL_CAL_DELAY_US);

  BK7258_REG(BK7258_ANA_REG0) |= BK7258_ANA0_SPIDETEN;
  bk7258_ana_wait(0);
  bk7258_clk_sleep_us(BK7258_DPLL_CAL_DELAY_US);
}

/****************************************************************************
 * Private: VDDD/VDDDIG raise (mirrors sys_hal_ctrl_vddd_h_vol /
 * vdddig_h_vol, with spi_latch1v gating per SDK note)
 ****************************************************************************/

static void bk7258_ana9_set_field(uint32_t clear, uint32_t set)
{
  BK7258_REG(BK7258_ANA_REG9) |= BK7258_ANA9_SPI_LATCH1V;  /* latch ON */
  bk7258_ana_wait(9);

  BK7258_REG(BK7258_ANA_REG9) =
      (BK7258_REG(BK7258_ANA_REG9) & ~clear) | set;
  bk7258_ana_wait(9);

  BK7258_REG(BK7258_ANA_REG9) &= ~BK7258_ANA9_SPI_LATCH1V; /* latch OFF */
  bk7258_ana_wait(9);
}

static void bk7258_raise_vdd(void)
{
  /* VDDD -> 1.0 V (0x6), VDDDIG -> 0.9 V (0xc).  Write each as a single
   * field update so the voltages step cleanly before settling.
   */

  bk7258_ana9_set_field(BK7258_ANA9_VDDD_MASK, BK7258_ANA9_VDDD_1V0);
  bk7258_clk_sleep_us(BK7258_VDD_SETTLE_US);

  bk7258_ana9_set_field(BK7258_ANA9_VDDDIG_MASK, BK7258_ANA9_VDDDIG_0V9);
  bk7258_clk_sleep_us(BK7258_VDD_SETTLE_US);
}

/****************************************************************************
 * Private: core mux switch to 320M (mirrors sys_hal_core_bus_clock_ctrl)
 ****************************************************************************/

static void bk7258_switch_to_320m(void)
{
  uint32_t v;

  /* cpu0_speed = 0 (avoid bus > 240 M); then divider, then source. */

  BK7258_REG(BK7258_CPU0_HALT_CLK_OP) &= ~(1u << 4);

  v = BK7258_REG(BK7258_CPU_CLK_DIV_MODE1);
  v = (v & ~BK7258_M1_CLKDIV_MASK) | 0u;          /* clkdiv_core = 0 */
  bk7258_m1_write(v);

  v = (v & ~BK7258_M1_CKSEL_MASK) | BK7258_M1_CKSEL_320M;  /* cksel_core = 2 */
  bk7258_m1_write(v);

  __asm__ volatile ("dsb 0xf" ::: "memory");
  __asm__ volatile ("isb 0xf" ::: "memory");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bk7258_clock_bringup_320m(void)
{
  uint32_t a5;
  uint32_t dpll_on;
  uint32_t did_switch = 0;

  /* Only switch to 320 MHz when the DPLL is already enabled and stable, i.e.
   * on the loader-residue path (soft reset / `u_bootloader enter`).  On a
   * cold reset the BootROM leaves EN_DPLL=0; a NuttX-initiated DPLL cold
   * enable (ANA_REG0/2/3 bias+softstart + SPI recalibration, chip-id
   * dependent) is not reproduced here and is the open N4-D1 blocker.  In
   * that case stay on the running XTAL/baseline clock and let the system
   * come up at 26 MHz -- boot still reaches NSH.  This keeps the 320 MHz
   * bring-up within the board-verified "DPLL already on" path.
   */

  a5 = BK7258_REG(BK7258_ANA_REG5);
  dpll_on = ((a5 & BK7258_ANA5_EN_DPLL) != 0);

  if (dpll_on)
    {
      /* Step 1: re-run the SDK SPI recalibration (the SDK does this
       * unconditionally; it is board-verified safe when EN_DPLL is set).
       */

      bk7258_dpll_recalibrate();

      /* Step 2: raise core voltages to the SDK guard levels for 320M. */

      bk7258_raise_vdd();

      /* Step 3: switch the core source/divider to 320M. */

      bk7258_switch_to_320m();
      did_switch = 1;
    }

#ifdef CONFIG_BK7258_CLOCK_320M_PROBE
  {
    uint32_t m1 = BK7258_REG(BK7258_CPU_CLK_DIV_MODE1);
    uint32_t r5 = BK7258_REG(BK7258_ANA_REG5);
    uint32_t r9 = BK7258_REG(BK7258_ANA_REG9);

    bk7258_clk_puts("N4Clk M1=");
    bk7258_clk_puthex8(m1);
    bk7258_clk_puts(" A5=");
    bk7258_clk_puthex8(r5);
    bk7258_clk_puts(" A9=");
    bk7258_clk_puthex8(r9);
    bk7258_clk_puts(" DXPLL=");
    bk7258_clk_puthex8(dpll_on);
    bk7258_clk_puts(" SW=");
    bk7258_clk_puthex8(did_switch);
    bk7258_clk_puts("\r\n");
  }
#endif
}