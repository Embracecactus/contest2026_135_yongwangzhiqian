/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_pwm.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) PWM — NuttX pwm_lowerhalf_s lower-half wrapper.
 *
 * Wraps the Beken armino SDK bk_pwm_* driver (AP core only) as a NuttX
 * PWM lower half.  See PWM_BLOCKED_ROOT_CAUSE.md in this directory for the
 * bundle-export defect that currently keeps bk_pwm_* out of libdriver.a
 * (CONFIG_PWM missing from the exported sdkconfig.h).  Once the bundle is
 * re-exported with CONFIG_PWM=1, this wrapper links and works as-is.
 *
 * SDK semantics captured here:
 *   - PWM input clock is XTAL 26 MHz (SDK selects PWM_SCLK_XTAL), so
 *     period_cycle unit == 1/26MHz.
 *   - The SDK inverts duty internally (duty_cycle = period - on_ticks,
 *     see pwm_driver.c), so the wrapper passes on-ticks directly as
 *     duty_cycle; the SDK subtracts it from period internally.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PWM_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PWM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* BK7258 PWM input clock: SDK selects PWM_SCLK_XTAL (26 MHz).  Used to
 * convert NuttX frequency (Hz) into period_cycle ticks.
 */

#define BK7258_PWM_CLK_HZ              26000000u

/* Default channel if not overridden by Kconfig. */

#define BK7258_PWM_CHAN_DEFAULT         0

/* Default /dev/pwmN device name. */

#ifndef CONFIG_BK7258_PWM_DEVNAME
#  define CONFIG_BK7258_PWM_DEVNAME     "pwm0"
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_PWM
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_pwm_initialize
 *
 * Description:
 *   Construct a NuttX PWM lower-half for BK7258 PWM channel CONFIG_BK7258_PWM_CHAN
 *   and register it at /dev/pwmN (N = CONFIG_BK7258_PWM_BUS).
 *
 * Returned Value:
 *   OK on success, or a negated errno value.  Returns -ENOENT if the SDK
 *   bk_pwm_* driver is not linked (bundle not re-exported with CONFIG_PWM=1).
 *
 ****************************************************************************/

int bk7258_pwm_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_PWM */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PWM_H */
