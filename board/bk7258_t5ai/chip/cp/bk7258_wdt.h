/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/cp/bk7258_wdt.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) hardware watchdog NuttX lower-half driver.
 *
 * Mirrors the Armino SDK WDT BSP path:
 *   bk_wdt_driver_init  -> wdt_hal_init (select WDT unit, close unused)
 *                        -> bk_timer_start(TIMER_ID2, 1s, feed_handle)
 *   bk_wdt_start(ms)    -> wdt_init_common (power up clock)
 *                        -> wdt_hal_init_wdt -> wdt_ll_set_period
 *                            (global_ctrl.soft_reset=1,
 *                             key1(0x5A)+period, key2(0xA5)+period)
 *   bk_wdt_feed()       -> wdt_hal_init_wdt(hal, period)  (= reinit)
 *   bk_wdt_stop()       -> close_wdt (key1+period=0, key2+period=0)
 *
 * NuttX watchdog upper-half (CONFIG_WATCHDOG_AUTOMONITOR) handles periodic
 * feeding via work queue once /dev/watchdog0 is opened and started.
 * The lower-half only implements the hardware operations.
 *
 * The Tier-1 bootloader arms the APB WDT during cold-init to recover from
 * DPLL / SPI hangs.  This driver takes over after boot: it reinitializes
 * the WDT with the configured period (default 8 s) and registers as
 * /dev/watchdog0.  The bootloader's WDT remains armed during the handoff
 * (the vendor bootloader also does not close WDT before jumping to app).
 *
 * WDT register layout (wdt_struct.h, wdt_reg.h):
 *   APB_WDT base = 0x44800000
 *   ctrl @ +0x10:  period[15:0] + key[23:16]
 *   global_ctrl @ +0x08: soft_reset[0]
 *   Key1 = 0x5A, Key2 = 0xA5 (two-step write to ctrl)
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_CHIP_BK7258_WDT_H
#define __ARCH_ARM_SRC_BK7258_CHIP_BK7258_WDT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_WDT
int bk7258_wdt_initialize(void);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_CHIP_BK7258_WDT_H */
