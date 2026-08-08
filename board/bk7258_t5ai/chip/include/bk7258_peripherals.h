/****************************************************************************
 * board/bk7258_t5ai/chip/include/bk7258_peripherals.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PERIPHERALS_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PERIPHERALS_H

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_peripherals_initialize(void);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_PERIPHERALS_H */
