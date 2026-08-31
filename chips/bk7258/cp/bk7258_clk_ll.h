/****************************************************************************
 * chips/bk7258/cp/bk7258_clk_ll.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 clock low-level register helpers shared by the boot-time
 * bring-up (bk7258_clock.c) and the runtime DVFS lower half
 * (bk7258_dvfs.c).  Everything here is `static inline` so the two
 * translation units can each include it without a multiple-definition
 * link conflict, and so the freestanding early-boot caller (no libc,
 * CPU at the BootROM 26 MHz residue clock) and the post-scheduler DVFS
 * caller share the exact same write/wait protocol.
 *
 * Mirrors the Armino SDK sys_ll analog-reg accessors
 * (sys_ll_set_ana_reg9_* / sys_ll_set_analog_reg_value) and the SDK
 * ctrl_vddd_h_vol / ctrl_vdddig_h_vol latch sequence (sys_hal.c:517-542).
 *
 * Register map (Armino soc/bk7258/soc/sys_reg.h + sys_hal.c):
 *   SYS block base        0x44010000
 *   ANA_SPI_STATE         0x440100E8  analog-SPI-write busy bit [idx]
 *                                       (idx = reg number; ANA_REG0 -> 0)
 *   CPU0_INT_HALT_CLK_OP  0x44010010  cpu0_speed [4]
 *   CPU_CLK_DIV_MODE1 M1 0x44010020  clkdiv_core [3:0], cksel_core [5:4]
 *   ANA_REG0              0x44010100
 *   ANA_REG5              0x44010114  EN_DPLL [5]
 *   ANA_REG9              0x44010124  spi_latch1v [9],
 *                                       vcorehsel (VDDIG) [16:19],
 *                                       vdighsel (VDDD)  [26:28]
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLK_LL_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLK_LL_H

#include <nuttx/config.h>
#include <stdint.h>

/* SYS register block. */
#define BK7258_SYS_BASE            0x44010000u
#define BK7258_ANA_SPI_STATE       (BK7258_SYS_BASE + 0x00e8u)
#define BK7258_CPU0_HALT_CLK_OP    (BK7258_SYS_BASE + 0x0010u)
#define BK7258_CPU1_HALT_CLK_OP    (BK7258_SYS_BASE + 0x0014u)
#define BK7258_CPU2_HALT_CLK_OP    (BK7258_SYS_BASE + 0x0018u)
#define BK7258_CPU_CLK_DIV_MODE1   (BK7258_SYS_BASE + 0x0020u)  /* M1 */
#define BK7258_ANA_REG0            (BK7258_SYS_BASE + 0x0100u)
#define BK7258_ANA_REG5            (BK7258_SYS_BASE + 0x0114u)
#define BK7258_ANA_REG9            (BK7258_SYS_BASE + 0x0124u)

/* M1 (CPU_CLK_DIV_MODE1) core-source fields. */
#define BK7258_M1_CLKDIV_MASK      0x0fu               /* clkdiv_core [3:0] */
#define BK7258_M1_CKSEL_MASK       (0x3u << 4)         /* cksel_core [5:4] */
#define BK7258_M1_CKSEL_SHIFT      4

/* CPU0_INT_HALT_CLK_OP: cpu0_speed [4] (0 = /2, 1 = /1 of the core clock
 * for CPU0).  This follows the latest SDK's
 * sys_hal_core_bus_clock_ctrl() operating-point table; scheduler SysTick is
 * independently clocked at 32 kHz. */
#define BK7258_CPU_SPEED_BIT       (1u << 4)

/* ANA_REG5: EN_DPLL [5]. */
#define BK7258_ANA5_EN_DPLL        (1u << 5)

/* ANA_REG9 field bit positions (from sys_struct.h:1166,1172,1177 and
 * sys_ll.h:7249,7294 rmw encoders). */
#define BK7258_ANA9_SPI_LATCH1V    (1u << 9)
#define BK7258_ANA9_VDDDIG_SHIFT   16u                  /* vcorehsel [16:19] */
#define BK7258_ANA9_VDDDIG_MASK    (0xfu << BK7258_ANA9_VDDDIG_SHIFT)
#define BK7258_ANA9_VDDD_SHIFT     26u                  /* vdighsel  [26:28] */
#define BK7258_ANA9_VDDD_MASK      (0x7u << BK7258_ANA9_VDDD_SHIFT)

/* The ANA_REG SPI busy bit index matches the register number (ANA_REG0 -> 0,
 * ANA_REG5 -> 5, ANA_REG9 -> 9). */
#define BK7258_ANA_SPI_BUSY(idx)   (1u << (idx))

/* Exact latest-SDK SYS_SWITCH_VDDDIG_VOL_DELAY_TIME loop count.  The SDK's
 * sys_hal_delay() is a volatile decrement loop and documents 2600 iterations
 * as about 10 us at the highest CPU0 operating point. */
#define BK7258_VDD_SETTLE_ITERS       2600u

/* Raw MMIO. */
#define BK7258_REG(a)               (*(volatile uint32_t *)(uintptr_t)(a))

/****************************************************************************
 * Inline helpers
 ****************************************************************************/

/* Block on the analog-SPI busy bit of <idx> (ANA_REG0 -> 0, ANA_REG9 -> 9).
 * Mirrors sys_ll_set_analog_reg_value's wait.  Bounded by callers in the
 * post-scheduler path via the shared delay budget; the early-boot caller
 * (bk7258_clock.c) bounds it separately. */
static inline void bk7258_ana_wait(unsigned idx)
{
  while ((BK7258_REG(BK7258_ANA_SPI_STATE) & BK7258_ANA_SPI_BUSY(idx)) != 0)
    {
    }
}

/* Bus-independent coarse microsecond delay.  One volatile MMIO read per
 * iteration so the compiler cannot elide it.  Sized for the slowest clock
 * relevant path (26 MHz cold residue); at higher post-switch clocks the
 * same count is a longer real wait, which is safe (it is a conservative
 * minimum settle). */
static inline void bk7258_clk_delay(volatile uint32_t times)
{
  while (times-- != 0)
    {
    }
}

/* RMW an ANA_REG9 field through the spi_latch1v gate, exactly as the SDK's
 * sys_hal_ctrl_vddd_h_vol / sys_hal_ctrl_vdddig_h_vol do (sys_hal.c:517-542):
 * latch ON -> set field -> latch OFF, with the analog-SPI busy wait after
 * each write and a settle delay.  <clear>/<set> are bit-aligned field masks.
 */
static inline void bk7258_ana9_set_field(uint32_t clear, uint32_t set)
{
  BK7258_REG(BK7258_ANA_REG9) |= BK7258_ANA9_SPI_LATCH1V;
  bk7258_ana_wait(9);

  BK7258_REG(BK7258_ANA_REG9) =
      (BK7258_REG(BK7258_ANA_REG9) & ~clear) | set;
  bk7258_ana_wait(9);

  BK7258_REG(BK7258_ANA_REG9) &= ~BK7258_ANA9_SPI_LATCH1V;
  bk7258_ana_wait(9);
}

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLK_LL_H */
