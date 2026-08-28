/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/common/bk7258_dvfs.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 runtime CPU-frequency switching (DVFS) -- the product-grade
 * equivalent of the Armino SDK runtime path
 *
 *     sys_drv_switch_cpu_bus_freq
 *       -> sys_hal_switch_cpu_bus_freq_low_to_high / high_to_low
 *          (sys_hal_ctrl_vddd_h_vol + sys_hal_ctrl_vdddig_h_vol)
 *          (sys_hal_core_bus_clock_ctrl)
 *
 * Operating-point selection is a *runtime* concern on this chip: the SDK's
 * default CONFIG_CPU_FREQ_HZ is 120 MHz, and 240M/320M/480M are shared SoC
 * operating-point labels reached only by sys_hal_switch_cpu_bus_freq(),
 * never by boot-time setup.  The bootloader's boot_clock.c therefore mirrors
 * only sys_hal_early_init (DPLL
 * enable + SPI recalibration) and leaves the analog side at the SDK default
 * (VDDIG=0xB); per-tier VDDD/VDDIG lift and M1 mux switching happen here,
 * one tier at a time, so voltages step monotonically (no abrupt jumps).
 *
 * The register lower half mirrors NuttX's lc823450 standalone DVFS pattern.
 * CP's bk7258_pm_policy.c integrates it with the stock NuttX PM lifecycle
 * and adds the v3.1.1.9-compatible multi-client max-vote policy.
 *
 * Scheduler SysTick uses BK7258's fixed 32-kHz route and is independent of
 * the processor mux.  bk7258_systick_recalc() refreshes only the DWT
 * cycle-to-time conversion after a switch.
 *
 * Operating-point table (from sys_hal.c:571-707 case comments; fields =
 * cksel_core, clkdiv_core, cpu0_speed, VDDD vdighsel, VDDDIG vcorehsel):
 *
 *   OPP    CPU0    CPU1/2  bus     cksel clkdiv cpu0  VDDD VDDIG
 *   26M     26M      26M    26M      0     0     /1     6    B
 *   60M     60M      60M    60M      3     7     /1     6    B
 *   80M     80M      80M    80M      3     5     /1     6    B
 *   120M   120M     120M   120M      3     3     /1     6    C
 *   240M   240M     240M   240M      3     1     /1     6    D
 *   320M   160M     320M   160M      2     0     /2     7    E
 *   480M   240M     480M   240M      3     0     /2     7    E
 *
 * The SDK enum order is policy precedence, not monotonically increasing CPU0
 * frequency: moving from 240M OPP to 320M OPP raises AP to 320 MHz but
 * lowers CPU0/bus to 160 MHz.  The CP image therefore has an official
 * maximum of 240 MHz.  The 480M OPP must remain available because the SDK
 * video/audio paths use it to run physical CPU1/CPU2 at 480 MHz; at that OPP
 * CPU0 and the bus remain at the official 240 MHz limit.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_DVFS_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_DVFS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Shared SoC operating points in the exact v3.1.1.9 pm_cpu_freq_e order.
 * The integer values double as the index into g_bk7258_dvfs_steps[] and as
 * the SDK policy comparison currency for bk7258_dvfs_set_opp().
 */

#define BK7258_OPP_26M      0
#define BK7258_OPP_60M      1
#define BK7258_OPP_80M      2
#define BK7258_OPP_120M     3
#define BK7258_OPP_240M     4
#define BK7258_OPP_320M     5
#define BK7258_OPP_480M     6

/* The OPP order is part of the SDK ABI.  Do not reorder it by physical CPU0
 * Hz: AP 320/480 MHz requests depend on OPP 5/6 remaining the highest votes.
 */

#define BK7258_OPP_MIN      BK7258_OPP_26M
#define BK7258_OPP_MAX      BK7258_OPP_480M

#define BK7258_CPU0_MAX_HZ  240000000u
#define BK7258_AP_MAX_HZ    480000000u
#define BK7258_BUS_MAX_HZ   240000000u

/* Compatibility names retained for existing out-of-tree callers.  New code
 * should say OPP because 320M/480M are not physical CPU0 frequencies.
 */

#define BK7258_FREQ_26M     BK7258_OPP_26M
#define BK7258_FREQ_60M     BK7258_OPP_60M
#define BK7258_FREQ_80M     BK7258_OPP_80M
#define BK7258_FREQ_120M    BK7258_OPP_120M
#define BK7258_FREQ_240M    BK7258_OPP_240M
#define BK7258_FREQ_320M    BK7258_OPP_320M
#define BK7258_FREQ_480M    BK7258_OPP_480M
#define BK7258_FREQ_MIN     BK7258_OPP_MIN
#define BK7258_FREQ_MAX     BK7258_OPP_MAX

/****************************************************************************
 * Inline operating-point helpers
 ****************************************************************************/

static inline uint32_t bk7258_dvfs_cpu0_hz_for_opp(int opp)
{
  static const uint32_t cpu0_hz[] =
  {
    26000000u, 60000000u, 80000000u, 120000000u,
    240000000u, 160000000u, 240000000u
  };

  return opp >= BK7258_OPP_MIN && opp <= BK7258_OPP_MAX ?
         cpu0_hz[opp] : 0u;
}

static inline uint32_t bk7258_dvfs_ap_hz_for_opp(int opp)
{
  static const uint32_t ap_hz[] =
  {
    26000000u, 60000000u, 80000000u, 120000000u,
    240000000u, 320000000u, 480000000u
  };

  return opp >= BK7258_OPP_MIN && opp <= BK7258_OPP_MAX ?
         ap_hz[opp] : 0u;
}

static inline uint32_t bk7258_dvfs_bus_hz_for_opp(int opp)
{
  return bk7258_dvfs_cpu0_hz_for_opp(opp);
}

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_dvfs_set_opp
 *
 * Description:
 *   Select the requested shared SDK operating point.  Mirrors the SDK
 *   sys_drv_switch_cpu_bus_freq: ascend or descend one OPP at a time
 *   calling the low_to_high / high_to_low step handler, so VDDD/VDDIG move
 *   monotonically.  CPU0/AP/bus frequencies are role-dependent at OPP 5/6;
 *   use the helpers above instead of interpreting the label as CPU0 MHz.
 *   The whole sequence runs with interrupts disabled (the M1 and voltage
 *   writes are atomic wrt ISRs).
 *
 *   After each per-tier switch bk7258_systick_recalc() refreshes the DWT
 *   frequency used by performance diagnostics.  The fixed scheduler clock
 *   is deliberately not restarted or rephased.
 *
 * Input Parameters:
 *   opp   - one of BK7258_OPP_* (26M..480M SDK labels).  Out-of-range
 *           values are rejected with -EINVAL.
 *
 * Returned Value:
 *   0 on success, -errno on invalid tier.
 *
 ****************************************************************************/

#ifdef CONFIG_BK7258_DVFS
int bk7258_dvfs_set_opp(int opp);
int bk7258_dvfs_get_opp(void);

/* Compatibility ABI for existing out-of-tree users. */

int bk7258_dvfs_set_freq(int tier);
int bk7258_dvfs_get_freq(void);

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
/* Register /proc/dvfs.  Must be called *before* the procfs is mounted (the
 * fs_procfs NOTE requires the entry table to be stable at mount time).
 */

int bk7258_dvfs_procfs_register(void);
#endif
#else
#  define bk7258_dvfs_set_opp(o)   (0)
#  define bk7258_dvfs_get_opp()    (BK7258_OPP_26M)
#  define bk7258_dvfs_set_freq(t)  bk7258_dvfs_set_opp(t)
#  define bk7258_dvfs_get_freq()   bk7258_dvfs_get_opp()
#  define bk7258_dvfs_procfs_register()  (0)
#endif

/* Refresh local CPU-clocked performance time after a frequency switch.
 * SysTick itself remains on the fixed 32-kHz source.  This is also required
 * when CP applies an AP SDK frequency vote; that path is independent of
 * CONFIG_BK7258_DVFS.
 */

void bk7258_systick_recalc(void);

/* Capture the architecture timer's current fractional phase immediately
 * before the immutable CP low-voltage leaf takes SysTick ownership.  After
 * wake, restore the fixed route and advance the saved NuttX arch-timer
 * callback by the elapsed whole ticks while carrying the residual phase in
 * a shortened first hardware interval.  The return value is the number of
 * whole scheduler ticks queued for the pending hard-IRQ SysTick trampoline.
 */

#ifdef CONFIG_TIMER_ARCH
int bk7258_systick_prepare_sleep(void);
uint32_t bk7258_systick_restore_after_sleep(uint64_t elapsed_us,
                                            uint32_t max_ticks);
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_DVFS_H */
