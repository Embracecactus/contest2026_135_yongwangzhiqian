/****************************************************************************
 * boards/bk7258/common/include/bk7258_ota_trial.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Product health policy for confirming a pending BK7258 CP/AP pair.
 ****************************************************************************/

#ifndef __ARCH_BOARD_BK7258_OTA_TRIAL_H
#define __ARCH_BOARD_BK7258_OTA_TRIAL_H

#include <stdint.h>

#include <arch/chip/bk7258_ota.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BK7258_OTA_TRIAL_STATUS_VERSION 1u

enum bk7258_ota_trial_state_e
{
  BK7258_OTA_TRIAL_NOT_PENDING = 0,
  BK7258_OTA_TRIAL_WAITING_HEALTH,
  BK7258_OTA_TRIAL_STABLE,
  BK7258_OTA_TRIAL_CONFIRMING,
  BK7258_OTA_TRIAL_CONFIRMED,
  BK7258_OTA_TRIAL_RESETTING,
  BK7258_OTA_TRIAL_ERROR
};

struct bk7258_ota_trial_status_s
{
  uint32_t version;
  uint32_t size;
  uint32_t state;
  enum bk7258_boot_slot_e active_slot;
  struct bk7258_mcuboot_version_s image_version;
  uint32_t security_counter;
  uint32_t supervisor_generation;
  uint32_t sample_sequence;
  uint32_t elapsed_ms;
  uint32_t stable_age_ms;
  uint32_t confirm_age_ms;
  uint32_t policy_age_ms;
  uint32_t deadline_age_ms;
  int32_t last_error;
};

#ifdef CONFIG_BK7258_OTA_AUTO_CONFIRM
int bk7258_ota_trial_initialize(void);
int bk7258_ota_trial_get_status(
  struct bk7258_ota_trial_status_s *status);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_BOARD_BK7258_OTA_TRIAL_H */
