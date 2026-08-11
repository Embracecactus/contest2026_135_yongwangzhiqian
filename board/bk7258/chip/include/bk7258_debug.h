/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/bk7258_debug.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DEBUG_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DEBUG_H

#include <nuttx/config.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_BK7258_SWD_RTT_DEBUG
int bk7258_swd_group1_initialize(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DEBUG_H */
