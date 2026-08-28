/****************************************************************************
 * chips/bk7258/include/bk7258_saradc_server.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_SERVER_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_SERVER_H

#include <nuttx/config.h>
#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_BK7258_SARADC_SERVER
#ifndef CONFIG_BK7258_AP_CORE
int bk7258_saradc_server_initialize(void);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_SERVER_H */
