/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/include/
 * bk7258_rtc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) RTC — NuttX rtc_lowerhalf_s wrapper.
 *
 * Wraps the Beken AON RTC driver (bk_rtc_*) as a NuttX RTC lower half.
 * The AON RTC keeps time across the SoC's low-power states, anchored to a
 * boot-time offset the SDK maintains in s_boot_time_us.
 *
 * AP role: the 4 bk_rtc_* symbols (gettimeofday/settimeofday/get_clock_freq/
 * get_ms_tick_count) live exclusively in the AP libdriver.a (verified with
 * `nm`); the CP archive does not export them.  So this driver is AP-only,
 * consistent with the I2C/SPI/SDIO/SARADC/GPIOE wrappers.
 *
 * SDK semantics:
 *   - bk_rtc_gettimeofday(&tv, NULL) returns epoch seconds + microseconds
 *     (tv_sec is a standard time_t).  The second (ptz) argument is ignored
 *     by the SDK.
 *   - bk_rtc_settimeofday(&tv, NULL) stores the epoch time; the timezone
 *     argument is ignored.
 *   - The SDK does not expose a "time has been set" query; this wrapper
 *     tracks that with its own flag, cleared on boot and set on settime().
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
 *   is called; bk_rtc_gettimeofday() runs lazily on first rdtime().
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
