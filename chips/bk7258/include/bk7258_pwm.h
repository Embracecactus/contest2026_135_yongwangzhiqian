/****************************************************************************
 * chips/bk7258/include/bk7258_pwm.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 PWM — NuttX pwm_lowerhalf_s lower-half wrapper.
 *
 * Wraps the Beken armino SDK bk_pwm_* driver (AP core only) as a NuttX
 * PWM lower half.  The peripheral-complete v3.1.1.9 AP bundle supplies the
 * controller implementation while NuttX owns registration and policy.
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
#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* BK7258 PWM input clock: SDK selects PWM_SCLK_XTAL (26 MHz).  Used to
 * convert NuttX frequency (Hz) into period_cycle ticks.
 */

#define BK7258_PWM_CLK_HZ              26000000u

/* Default channel if not overridden by Kconfig. */

#define BK7258_PWM_CHAN_DEFAULT         0

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
 *   OK on success, or a negated errno value.
 *
 ****************************************************************************/

int bk7258_pwm_initialize(void);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_PWM */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PWM_H */
