/****************************************************************************
 * chips/bk7258/cp/bk7258_clock.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP/CPU0 240 MHz performance startup target.
 *
 * bk7258_clock_bringup_240m() requests the pinned v3.1.1.9 SDK
 * PM_CPU_FRQ_240M operating point through the common DVFS lower half.  This
 * gives physical CPU0, physical CPU1/CPU2 and the bus 240 MHz.  The SDK 320M
 * and 480M labels instead describe shared operating points where CPU0 is
 * divided to 160/240 MHz and AP physical CPU1/CPU2 run at 320/480 MHz.
 *
 * BL1 supplies the recovered 120 MHz safe handoff.  This helper asks the
 * runtime DVFS lower half for the current bring-up target; it is not the
 * final power policy.  Runtime clients and a later NuttX PM governor use the
 * same bk7258_dvfs_set_opp() path to change SDK operating points safely.
 *
 * Called early in __start(), before nx_start(), so the initial DWT conversion
 * observes csrc=3, cdiv=1 and CPU0 /1.  Scheduler SysTick stays on
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
 * Name: bk7258_clock_bringup_240m
 *
 * Description:
 *   Select the SDK PM_CPU_FRQ_240M operating point deterministically after the
 *   bootloader's 120 MHz handoff.  The DVFS lower half applies the SDK voltage,
 *   core divider and CPU divider ordering, then refreshes DWT conversion for
 *   the 240 MHz CPU0 frequency.  Scheduler SysTick remains fixed at 32 kHz.
 *
 * Returned Value:
 *   None.  A failed or stalled recalibration is recoverable by re-flash; this
 *   routine does not return in a way callers can react to (it executes before
 *   the scheduler exists).
 *
 ****************************************************************************/

#ifdef CONFIG_BK7258_CLOCK_240M
void bk7258_clock_bringup_240m(void);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_CLOCK_H */
