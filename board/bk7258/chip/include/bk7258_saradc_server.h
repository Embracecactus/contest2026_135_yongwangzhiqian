/****************************************************************************
 * board/bk7258/chip/include/bk7258_saradc_server.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_SERVER_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_SERVER_H

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SARADC_SERVER
#ifndef CONFIG_BK7258_AP_CORE
int bk7258_saradc_server_initialize(void);
#endif
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_SERVER_H */
