/****************************************************************************
 * chips/bk7258/include/bk7258_ap_platform.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-local BK7258 platform preparation contract.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AP_PLATFORM_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AP_PLATFORM_H

#include <nuttx/compiler.h>

#include <arch/chip/bk7258_platform.h>

enum bk7258_ap_platform_stage_e
{
  BK7258_AP_STAGE_ROLE_CONTRACT = 0,
  BK7258_AP_STAGE_SDK_RUNTIME,
  BK7258_AP_STAGE_PM_CLIENT,
  BK7258_AP_STAGE_TEMPERATURE_CLIENT,
  BK7258_AP_STAGE_PSRAM,
  BK7258_AP_STAGE_COUNT
};

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_ap_platform_prepare(void);
int bk7258_ap_platform_result(void);
int bk7258_ap_platform_get_status(
  FAR struct bk7258_platform_status_s *status);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AP_PLATFORM_H */
