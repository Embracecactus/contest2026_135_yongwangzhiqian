/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_rtc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) RTC — NuttX rtc_lowerhalf_s wrapper.
 *
 * Wraps the Beken AON RTC free-running counter as a NuttX RTC lower half.
 * Epoch time uses an AP-local RAM offset.  It intentionally does not call
 * the SDK EasyFlash-backed settimeofday path because Flash belongs to CP.
 *
 * AP role: the 4 bk_rtc_* symbols (gettimeofday/settimeofday/get_clock_freq/
 * get_ms_tick_count) live exclusively in the AP libdriver.a (verified with
 * `nm`); the CP archive does not export them.  So this driver is AP-only,
 * consistent with the I2C/SPI/SDIO/SARADC/GPIOE wrappers.
 *
 * The offset survives low-power states that retain AP RAM, but not an AP
 * restart.  Persistent wall-clock storage requires a future CP RPMsg service.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_RTC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_RTC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_RTC
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_rtc_initialize
 *
 * Description:
 *   Construct the BK7258 RTC lower half and register it at /dev/rtc0 via
 *   rtc_initialize().  No hardware is touched until an upper-half method
 *   is called; the AON counter is read lazily on first rdtime().
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.  Calling it twice is
 *   harmless and returns OK.
 *
 ****************************************************************************/

int bk7258_rtc_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_RTC */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_RTC_H */
