/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/bk7258_clockdiag.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 live CPU-clock runtime detection.
 *
 * Read-only helpers classify the live core-clock configuration left by the
 * Tier-1 bootloader and later DVFS votes.  The result drives CPU-clocked DWT
 * conversion and diagnostics; scheduler SysTick uses a separate fixed
 * 32-kHz source.  No clock-control register is written here.
 *
 * Contract: the including translation unit must include arm_internal.h first
 * so getreg32()/putreg32() are available.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCKDIAG_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCKDIAG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/* Peripheral base addresses (Armino cp/include/soc/bk7258/reg_base.h). */

#define BK7258_CDIAG_SYS_BASE        0x44010000u   /* SOC_SYS_REG_BASE     */
#define BK7258_CDIAG_AON_PMU_BASE    0x44000000u   /* SOC_AON_PMU_REG_BASE */

/* SYS register offsets (Armino soc/bk7258/soc/sys_reg.h, ADDR == base +
 * (index << 2)).  Raw values only; never written by this header.
 */

#define BK7258_CDIAG_SYS_CPU0_HALT_CLK_OP \
        (BK7258_CDIAG_SYS_BASE + (0x04u << 2))            /* 0x44010010 */
#define BK7258_CDIAG_SYS_CPU_CLK_DIV_MODE1 \
        (BK7258_CDIAG_SYS_BASE + (0x08u << 2))            /* 0x44010020 */
#define BK7258_CDIAG_SYS_CPU_CLK_DIV_MODE2 \
        (BK7258_CDIAG_SYS_BASE + (0x09u << 2))            /* 0x44010024 */
#define BK7258_CDIAG_SYS_CPU0_INT_32_63_STAT \
        (BK7258_CDIAG_SYS_BASE + (0x29u << 2))            /* 0x440100A4 */
#define BK7258_CDIAG_SYS_ANA_REG0 \
        (BK7258_CDIAG_SYS_BASE + (0x40u << 2))            /* 0x44010100 */
#define BK7258_CDIAG_SYS_ANA_REG1 \
        (BK7258_CDIAG_SYS_BASE + (0x41u << 2))            /* 0x44010104 */
#define BK7258_CDIAG_SYS_ANA_REG5 \
        (BK7258_CDIAG_SYS_BASE + (0x45u << 2))            /* 0x44010114 */
#define BK7258_CDIAG_SYS_ANA_REG8 \
        (BK7258_CDIAG_SYS_BASE + (0x48u << 2))            /* 0x44010120 */
#define BK7258_CDIAG_SYS_ANA_REG9 \
        (BK7258_CDIAG_SYS_BASE + (0x49u << 2))            /* 0x44010124 */
#define BK7258_CDIAG_SYS_ANA_REG10 \
        (BK7258_CDIAG_SYS_BASE + (0x4au << 2))            /* 0x44010128 */
#define BK7258_CDIAG_SYS_ANA_REG11 \
        (BK7258_CDIAG_SYS_BASE + (0x4bu << 2))            /* 0x4401012C */
#define BK7258_CDIAG_SYS_ANA_REG12 \
        (BK7258_CDIAG_SYS_BASE + (0x4cu << 2))            /* 0x44010130 */
#define BK7258_CDIAG_SYS_ANA_REG13 \
        (BK7258_CDIAG_SYS_BASE + (0x4du << 2))            /* 0x44010134 */

/* AON_PMU register offset (Armino soc/bk7258/soc/aon_pmu_reg.h). */

#define BK7258_CDIAG_AON_PMU_R7D \
        (BK7258_CDIAG_AON_PMU_BASE + (0x7du << 2))        /* 0x440001F4 */

/* SysTick CSR/RVR/CVR -- standard ARMv7-M/ARMv8-M addresses (identical to
 * nuttx NVIC_SYSTICK_CTRL/RELOAD/CURRENT); inlined so this header does not
 * require nvic.h in every including translation unit.
 */

#define BK7258_CDIAG_SYSTICK_CSR      0xe000e010u
#define BK7258_CDIAG_SYSTICK_RVR      0xe000e014u
#define BK7258_CDIAG_SYSTICK_CVR      0xe000e018u

/* Field bit definitions for the clock-case classifier (source: Armino
 * soc/bk7258/soc/sys_reg.h).
 *
 *   CPU_CLK_DIV_MODE1:
 *     CLKDIV_CORE  [0:3]   core divider (shared by all CPUs)
 *     CKSEL_CORE   [4:5]   core source select
 *   ANA_REG5:
 *     EN_DPLL      [5]     DPLL enable
 */

#define BK7258_CDIAG_F_CKSEL_CORE(v)   (((v) >> 4)  & 0x3u)
#define BK7258_CDIAG_F_CLKDIV_CORE(v)  (((v) >> 0)  & 0xfu)
#define BK7258_CDIAG_F_EN_DPLL(v)      (((v) >> 5)  & 0x1u)
#define BK7258_CDIAG_F_CPU0_SPEED(v)   (((v) >> 4)  & 0x1u)

/* Runtime CPU-frequency case identifiers used by
 * bk7258_clockdiag_last_clock_case() and bk7258_clockdiag_current_cpu_hz().
 * The case is decided from a read-only snapshot of CPU_CLK_DIV_MODE1 (M1)
 * and ANA_REG5 (A5) -- never from a write.
 */

#define BK7258_CDIAG_CASE_BASELINE   0   /* M1=0 dplle=0: 26 MHz XTAL.      */
#define BK7258_CDIAG_CASE_LOADER80   1   /* M1=0x423 csrc=2 cdiv=3 dplle=1:
                                         * loader --reboot 1 residue, ~80 MHz
                                         */
#define BK7258_CDIAG_CASE_CORE120    2
#define BK7258_CDIAG_CASE_CORE160    3
#define BK7258_CDIAG_CASE_CORE240    4
#define BK7258_CDIAG_CASE_CORE320    5
#define BK7258_CDIAG_CASE_CORE480    6
#define BK7258_CDIAG_CASE_CORE60     7
#define BK7258_CDIAG_CASE_CORE80     8
#define BK7258_CDIAG_CASE_UNKNOWN    9   /* fallback to baseline hz.        */

/****************************************************************************
 * Public Functions (static inline)
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_clockdiag_last_clock_case
 *
 * Description:
 *   Classify the live core-clock configuration from a read-only snapshot of
 *   CPU_CLK_DIV_MODE1 (M1) and ANA_REG5 (A5).  No register is written.
 *
 *     BK7258_CDIAG_CASE_BASELINE  Manual-reset BootROM default: M1 = 0 with
 *                                 DPLL disabled -- 26 MHz XTAL.
 *     BK7258_CDIAG_CASE_LOADER80  Tier-1 loader `--reboot 1` residue:
 *                                 M1 = 0x00000423 with csrc = 2, cdiv = 3
 *                                 and DPLL enabled -- f_cpu ~80 MHz.
 *     BK7258_CDIAG_CASE_CORE*     Shared core-source/divider result.  The
 *                                 number is not necessarily the local CPU
 *                                 frequency: CPU0 and the bus divide the SDK
 *                                 CORE320/CORE480 cases to 160/240 MHz.
 *     BK7258_CDIAG_CASE_UNKNOWN   Any other combination; callers fall back
 *                                 to the baseline frequency.
 *
 ****************************************************************************/

static inline int bk7258_clockdiag_last_clock_case(void)
{
  uint32_t m1    = getreg32(BK7258_CDIAG_SYS_CPU_CLK_DIV_MODE1);
  uint32_t a5    = getreg32(BK7258_CDIAG_SYS_ANA_REG5);
  uint32_t csrc  = BK7258_CDIAG_F_CKSEL_CORE(m1);
  uint32_t cdiv  = BK7258_CDIAG_F_CLKDIV_CORE(m1);
  uint32_t dplle = BK7258_CDIAG_F_EN_DPLL(a5);

  /* Classify by csrc/cdiv/dplle fields only, not the full 32-bit M1.
   * The upper bits of M1 (uart0 source, bus divider, etc.) differ between
   * cold-reset (BootROM default) and soft-reset (loader residue) paths;
   * matching the full word caused the 320 MHz case to miss on cold reset
   * (M1=0x020 vs loader's 0x420), making current_cpu_hz() fall back to
   * 26 MHz and corrupting CPU-cycle diagnostics.
   */

  if (csrc == 0 && dplle == 0)
    {
      return BK7258_CDIAG_CASE_BASELINE;
    }

  if (csrc == 2 && cdiv == 3 && dplle)
    {
      return BK7258_CDIAG_CASE_LOADER80;
    }

  if (csrc == 3 && cdiv == 7 && dplle)
    {
      return BK7258_CDIAG_CASE_CORE60;
    }

  if (csrc == 3 && cdiv == 5 && dplle)
    {
      return BK7258_CDIAG_CASE_CORE80;
    }

  if (csrc == 3 && cdiv == 3 && dplle)
    {
      return BK7258_CDIAG_CASE_CORE120;
    }

  if (csrc == 3 && cdiv == 2 && dplle)
    {
      return BK7258_CDIAG_CASE_CORE160;
    }

  if (csrc == 3 && cdiv == 1 && dplle)
    {
      return BK7258_CDIAG_CASE_CORE240;
    }

  if (csrc == 2 && cdiv == 0 && dplle)
    {
      return BK7258_CDIAG_CASE_CORE320;
    }

  if (csrc == 3 && cdiv == 0 && dplle)
    {
      return BK7258_CDIAG_CASE_CORE480;
    }

  return BK7258_CDIAG_CASE_UNKNOWN;
}

/****************************************************************************
 * Name: bk7258_clockdiag_core_hz_for_case
 ****************************************************************************/

static inline uint32_t bk7258_clockdiag_core_hz_for_case(int clock_case)
{
  switch (clock_case)
    {
      case BK7258_CDIAG_CASE_BASELINE:
        return 26000000u;

      case BK7258_CDIAG_CASE_LOADER80:
      case BK7258_CDIAG_CASE_CORE80:
        return 80000000u;

      case BK7258_CDIAG_CASE_CORE60:
        return 60000000u;

      case BK7258_CDIAG_CASE_CORE120:
        return 120000000u;

      case BK7258_CDIAG_CASE_CORE160:
        return 160000000u;

      case BK7258_CDIAG_CASE_CORE240:
        return 240000000u;

      case BK7258_CDIAG_CASE_CORE320:
        return 320000000u;

      case BK7258_CDIAG_CASE_CORE480:
        return 480000000u;

      default:
        return 26000000u;
    }
}

/****************************************************************************
 * Name: bk7258_clockdiag_current_cpu_hz
 *
 * Description:
 *   Return the live processor-clock frequency in Hz inferred
 *   from the live M1 and A5 registers.  Maps each case from
 *   bk7258_clockdiag_last_clock_case() to its measured frequency, and falls
 *   back to the 26 MHz baseline for the unknown case.  Read-only; no
 *   clock-control register is written.
 *
 *   The returned value is the CPU/DWT clock.  It is intentionally
 *   independent of the fixed 32-kHz scheduler SysTick source.
 *   BOARD_CPU_FREQ_HZ in
 *   board.h remains the build-time cold-reset fallback and matches the
 *   BK7258_CDIAG_CASE_BASELINE return.
 *
 ****************************************************************************/

static inline uint32_t bk7258_clockdiag_current_cpu_hz(void)
{
  int clock_case = bk7258_clockdiag_last_clock_case();
  uint32_t cpu_hz = bk7258_clockdiag_core_hz_for_case(clock_case);

#ifndef CONFIG_BK7258_AP_CORE
  /* The CP image executes on physical CPU0.  In the SDK 320/480 operating
   * points cpu0_speed=0 selects /2, while AP physical CPU1/CPU2 retain the
   * full core clock.  Keep the AP result unchanged and apply the divider
   * only to the CP role.
   */

  if ((clock_case == BK7258_CDIAG_CASE_CORE320 ||
       clock_case == BK7258_CDIAG_CASE_CORE480) &&
      BK7258_CDIAG_F_CPU0_SPEED(
        getreg32(BK7258_CDIAG_SYS_CPU0_HALT_CLK_OP)) == 0)
    {
      cpu_hz >>= 1;
    }
#endif

  return cpu_hz;
}

/****************************************************************************
 * Name: bk7258_clockdiag_current_bus_hz
 *
 * Description:
 *   Return the shared bus frequency for a recognized v3.1.1.9 SDK OPP.
 *   The 320M/480M OPPs keep the bus at half the shared core source
 *   (160/240 MHz), matching the official CPU0/bus ceiling.  The SDK always
 *   requests clkdiv_bus=0 and does not program M1 bit 6, so this helper does
 *   not claim to decode unsupported raw bit-6 states.  This role-independent
 *   value must be used by peripherals instead of AP CPU1/CPU2 Hz.
 ****************************************************************************/

static inline uint32_t bk7258_clockdiag_current_bus_hz(void)
{
  int clock_case = bk7258_clockdiag_last_clock_case();
  uint32_t bus_hz = bk7258_clockdiag_core_hz_for_case(clock_case);

  if (clock_case == BK7258_CDIAG_CASE_CORE320 ||
      clock_case == BK7258_CDIAG_CASE_CORE480)
    {
      bus_hz >>= 1;
    }

  return bus_hz;
}

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCKDIAG_H */
