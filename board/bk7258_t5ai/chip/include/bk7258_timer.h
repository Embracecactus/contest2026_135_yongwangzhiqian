/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/include/
 * bk7258_timer.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) general-purpose timer — NuttX timer_lowerhalf_s wrapper.
 *
 * Wraps the Beken SDK bk_timer_* driver as a NuttX timer lower half and
 * publishes it at /dev/timerN.  The BK7258 has 6 hardware timer channels
 * (TIMER_ID0..TIMER_ID5).
 *
 * AP/CP: the 15 bk_timer_* symbols are exported by both the AP and the CP
 * libdriver.a (verified with `nm`).  This wrapper runs on the AP core,
 * consistent with the I2C/SPI/SDIO/SARADC/GPIOE/RTC wrappers.
 *
 * SDK semantics:
 *   - CONFIG_TIMER_US is set in the bundle, so TIMER_ID0 is reserved for
 *     the SDK's microsecond timer and bk_timer_start() rejects it.  The
 *     wrapper therefore defaults to TIMER_ID1 and the Kconfig channel
 *     range is 1..5.
 *   - bk_timer_start(id, ms, isr) arms the hardware to fire the ISR
 *     periodically (the SDK reprograms the period on each expiry); the
 *     ISR is dispatched per-channel from the SDK's timer_isr().
 *   - bk_timer_stop(id) disables the channel.  bk_timer_get_enable_status()
 *     returns a bitmask of running channels (BIT(id)).
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_TIMER_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_TIMER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default hardware timer channel.  TIMER_ID0 is reserved by the SDK for
 * its microsecond timer (CONFIG_TIMER_US), so default to TIMER_ID1.
 */

#ifndef CONFIG_BK7258_TIMER_CHAN
#  define CONFIG_BK7258_TIMER_CHAN      1
#endif

/* Default /dev/timerN device name. */

#ifndef CONFIG_BK7258_TIMER_DEVNAME
#  define CONFIG_BK7258_TIMER_DEVNAME   "timer0"
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_TIMER
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_timer_initialize
 *
 * Description:
 *   Construct a NuttX timer lower-half for BK7258 hardware timer channel
 *   CONFIG_BK7258_TIMER_CHAN and register it at /dev/timerN
 *   (N = CONFIG_BK7258_TIMER_BUS).  No hardware is touched until the upper
 *   half starts the timer.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.  Calling it twice is
 *   harmless and returns OK.
 *
 ****************************************************************************/

int bk7258_timer_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_TIMER */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_TIMER_H */
