/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_clock.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) temporary 320 MHz CPU0 bring-up target.
 *
 * bk7258_clock_bringup_320m() mirrors the Armino SDK early-init clock path
 * (sys_hal_early_init -> sys_hal_dpll_cpu_flash_time_early_init ->
 * sys_hal_cali_dpll + sys_hal_core_bus_clock_ctrl) so that NuttX itself
 * drives the core clock to 320 MHz (DPLL /1.5, the highest board-verified
 * operating point on this chip; 480 MHz direct is SDK-rejected).
 *
 * BL1 supplies the recovered 120 MHz safe handoff.  This helper asks the
 * runtime DVFS lower half for the current bring-up target; it is not the
 * final power policy.  Runtime clients and a later NuttX PM governor use the
 * same bk7258_dvfs_set_freq() path to change SDK operating points safely.
 *
 * Called early in __start(), before nx_start(), so the initial DWT conversion
 * observes M1 low bits = 0x20 (csrc=2, cdiv=0).  Scheduler SysTick stays on
 * its fixed 32-kHz source.  The selected UART runs off an independent
 * clocking path and survives the core mux switch.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCK_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCK_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_clock_bringup_320m
 *
 * Description:
 *   Bring the CPU0 core clock up to 320 MHz deterministically.  Ensures the
 *   DPLL is enabled (running the SDK SPI recalibration), raises VDDDIG/VDDD
 *   to the voltage the SDK guard requires for 320M, then switches the core
 *   source/divider.  The caller refreshes DWT conversion for the new CPU
 *   frequency; scheduler SysTick remains fixed at 32 kHz.
 *
 * Returned Value:
 *   None.  A failed or stalled recalibration is recoverable by re-flash; this
 *   routine does not return in a way callers can react to (it executes before
 *   the scheduler exists).
 *
 ****************************************************************************/

#ifdef CONFIG_BK7258_CLOCK_320M
void bk7258_clock_bringup_320m(void);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCK_H */
