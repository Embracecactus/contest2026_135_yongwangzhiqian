/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/bk7258_dvfs.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) runtime CPU-frequency switching (DVFS) -- the product-grade
 * equivalent of the Armino SDK runtime path
 *
 *     sys_drv_switch_cpu_bus_freq
 *       -> sys_hal_switch_cpu_bus_freq_low_to_high / high_to_low
 *          (sys_hal_ctrl_vddd_h_vol + sys_hal_ctrl_vdddig_h_vol)
 *          (sys_hal_core_bus_clock_ctrl)
 *
 * Frequency selection is a *runtime* concern on this chip: the SDK's default
 * CONFIG_CPU_FREQ_HZ is 120 MHz, and 240/320/480 MHz are runtime tiers reached
 * only by sys_hal_switch_cpu_bus_freq(), never by boot-time setup.  The
 * bootloader's boot_clock.c therefore mirrors only sys_hal_early_init (DPLL
 * enable + SPI recalibration) and leaves the analog side at the SDK default
 * (VDDIG=0xB); per-tier VDDD/VDDIG lift and M1 mux switching happen here, one
 * tier at a time, so voltages step monotonically (no abrupt jumps).
 *
 * The interface mirrors NuttX's lc823450 DVFS pattern (a standalone
 * *_dvfs_set_freq(), not the NuttX PM state-machine subsystem) so this stays
 * minimal: no governor, no CONFIG_PM, no SCHED_HPWORK.  See
 * arch/arm/src/lc823450/lc823450_dvfs2.c for the OSS precedent.
 *
 * SysTick reload is recomputed after every switch via bk7258_systick_recalc()
 * (chip/common/bk7258_timerisr.c) because SysTick is clocked at the processor clock.
 *
 * Tier table (from sys_hal.c:548-686 case comments; fields = cksel_core,
 * clkdiv_core, cpu0_speed, VDDD vdighsel, VDDDIG vcorehsel):
 *
 *   tier   cpu0 freq   cksel clkdiv cpu0  VDDD  VDDIG
 *   26M    26  MHz     0x0   0x0    0x1   0x6   0xB
 *   60M    60  MHz     0x3   0x7    0x1   0x6   0xB
 *   80M    80  MHz     0x3   0x5    0x1   0x6   0xB
 *   120M   120 MHz     0x3   0x3    0x1   0x6   0xC
 *   240M   240 MHz     0x3   0x1    0x1   0x6   0xD
 *   320M   160 MHz(*)  0x2   0x0    0x0   0x7   0xE
 *   480M   240 MHz(*)  0x3   0x0    0x0   0x7   0xE   (SDK-guarded, not used)
 *
 * (*) The SDK's 320/480 tiers set cpu0_speed=0 (/2), giving CPU0 only
 *     160/240 MHz respectively; CPU1/CPU2 retain the full core clock.  In the
 *     CP NuttX image CPU0 therefore tops out at 160 MHz on the 320 tier (chosen
 *     as the "SDK-aligned stable" point).  Reaching CPU0=320 is a separate,
 *     future task.  This API stays the same regardless.
 *
 * 480 MHz is not attempted: the SDK guard rejects cksel=3/clkdiv=0 direct and
 * the required 0xE VDDIG/power is out of scope (per project constraint).
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_DVFS_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_DVFS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

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

/* CPU frequency tiers, ordered low -> high (the SDK enum order).  The
 * integer values double as the index into g_bk7258_dvfs_steps[] and as the
 * comparison currency for stepping up/down in bk7258_dvfs_set_freq().
 */

#define BK7258_FREQ_26M     0
#define BK7258_FREQ_60M     1
#define BK7258_FREQ_80M     2
#define BK7258_FREQ_120M    3
#define BK7258_FREQ_240M    4
#define BK7258_FREQ_320M    5
#define BK7258_FREQ_480M    6

/* The frequency order is part of the contract: bk7258_dvfs_set_freq() steps
 * one tier at a time using ++/-- between prev and target, so higher tiers
 * MUST sort strictly after lower ones here. */

#define BK7258_FREQ_MIN     BK7258_FREQ_26M
#define BK7258_FREQ_MAX     BK7258_FREQ_480M

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_dvfs_set_freq
 *
 * Description:
 *   Step the CPU0 core clock to the requested frequency tier.  Mirrors the
 *   SDK sys_drv_switch_cpu_bus_freq: ascend or descend one tier at a time
 *   calling the low_to_high / high_to_low step handler, so VDDD/VDDIG move
 *   monotonically.  The whole sequence runs with interrupts disabled (the
 *   M1 write, voltage writes and the trailing SysTick reload are atomic wrt
 *   ISRs).
 *
 *   After each per-tier switch the SysTick reload is recomputed via
 *   bk7258_systick_recalc().  Callers that want a single sysfreq change see
 *   one external side effect: the final reload.
 *
 * Input Parameters:
 *   tier  - one of BK7258_FREQ_* (26M..480M).  Out-of-range values are
 *           rejected with -EINVAL.
 *
 * Returned Value:
 *   0 on success, -errno on invalid tier.
 *
 ****************************************************************************/

#ifdef CONFIG_BK7258_DVFS
int bk7258_dvfs_set_freq(int tier);
int bk7258_dvfs_get_freq(void);

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
/* Register /proc/dvfs.  Must be called *before* the procfs is mounted (the
 * fs_procfs NOTE requires the entry table to be stable at mount time). */
int bk7258_dvfs_procfs_register(void);
#endif
#else
#  define bk7258_dvfs_set_freq(t)  (0)
#  define bk7258_dvfs_get_freq()   (BK7258_FREQ_26M)
#  define bk7258_dvfs_procfs_register()  (0)
#endif

/* Recompute and write the local SysTick one-tick reload for the live core
 * clock.  This is also required when CP applies an AP SDK frequency vote;
 * that path is independent of CONFIG_BK7258_DVFS.
 */

void bk7258_systick_recalc(void);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_DVFS_H */
