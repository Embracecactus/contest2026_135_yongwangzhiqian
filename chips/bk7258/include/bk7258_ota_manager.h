/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_ota_manager.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_MANAGER_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_MANAGER_H

#include <nuttx/config.h>

#include <stdint.h>

#include <arch/chip/bk7258_ota.h>

enum bk7258_ota_manager_state_e
{
  BK7258_OTA_MANAGER_IDLE = 0,
  BK7258_OTA_MANAGER_CHECKING,
  BK7258_OTA_MANAGER_PACKAGE_VERIFIED,
  BK7258_OTA_MANAGER_STAGING_AP,
  BK7258_OTA_MANAGER_STAGING_CP,
  BK7258_OTA_MANAGER_PAIR_VERIFIED,
  BK7258_OTA_MANAGER_READY_TO_REBOOT,
  BK7258_OTA_MANAGER_FAILED,
  BK7258_OTA_MANAGER_CANCELED
};

struct bk7258_ota_manager_status_s
{
  enum bk7258_ota_manager_state_e state;
  enum bk7258_ota_phase_e phase;
  enum bk7258_ota_image_e image;
  uint32_t completed;
  uint32_t total;
  int32_t last_error;
};

#ifdef CONFIG_BK7258_OTA_MANAGER
int bk7258_ota_manager_initialize(void);
int bk7258_ota_manager_apply(const struct bk7258_ota_source_ops_s *source,
                             void *context, uint32_t timeout_ms);
int bk7258_ota_manager_report_failure(int error);
int bk7258_ota_manager_cancel(void);
int bk7258_ota_manager_get_status(
  struct bk7258_ota_manager_status_s *status);
#endif

#endif
