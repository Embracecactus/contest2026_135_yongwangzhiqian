/****************************************************************************
 * chips/bk7258/include/bk7258_selftest.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Public entry points for optional BK7258 chip self-test commands.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SELFTEST_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SELFTEST_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_SDK_IRQ_TIMER_TEST
int bk7258_sdk_irq_timer_test(void);
#endif

#ifdef CONFIG_BK7258_SDK_TIMER_SELFTEST
int bk7258_sdk_timer_selftest(uint32_t iterations);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SELFTEST_H */
