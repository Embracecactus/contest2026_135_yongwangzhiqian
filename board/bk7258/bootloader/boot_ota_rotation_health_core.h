/*
 * boot_ota_rotation_health_core.h - format-2 trial health policy.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_ROTATION_HEALTH_CORE_H
#define BK7258_BOOT_OTA_ROTATION_HEALTH_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_ota_rotation_core.h"

enum bk7258_boot_ota_rotation_health_reason_e
{
  BK7258_BOOT_OTA_ROTATION_HEALTH_NONE = 0,
  BK7258_BOOT_OTA_ROTATION_HEALTH_METADATA_INVALID,
  BK7258_BOOT_OTA_ROTATION_HEALTH_GENERATION_STALE,
  BK7258_BOOT_OTA_ROTATION_HEALTH_NOT_TRIAL,
  BK7258_BOOT_OTA_ROTATION_HEALTH_MAPPING_MISMATCH,
  BK7258_BOOT_OTA_ROTATION_HEALTH_SUPERVISOR_UNHEALTHY,
  BK7258_BOOT_OTA_ROTATION_HEALTH_CLOCK_REGRESSED,
  BK7258_BOOT_OTA_ROTATION_HEALTH_STABILIZING,
  BK7258_BOOT_OTA_ROTATION_HEALTH_READY
};

struct bk7258_boot_ota_rotation_health_sample_s
{
  uint64_t now_ms;
  uint32_t supervisor_generation;
  uint32_t supervisor_fault_count;
  enum bk7258_boot_ota_slot_e active_slot;
  bool supervisor_healthy;
  bool supervisor_fault_free;
};

struct bk7258_boot_ota_rotation_health_tracker_s
{
  uint64_t stable_since_ms;
  uint32_t supervisor_generation;
  uint32_t supervisor_fault_count;
  bool tracking;
};

struct bk7258_boot_ota_rotation_health_result_s
{
  int status;
  enum bk7258_boot_ota_rotation_health_reason_e reason;
  enum bk7258_boot_ota_rotation_state_e state;
  enum bk7258_boot_ota_slot_e target_slot;
  uint64_t generation;
  uint64_t stable_ms;
  uint32_t supervisor_generation;
  uint32_t supervisor_fault_count;
  bool ready;
};

int bk7258_boot_ota_rotation_health_update(
  struct bk7258_boot_ota_rotation_health_tracker_s *tracker,
  const uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  uint64_t expected_generation, uint32_t required_stable_ms,
  const struct bk7258_boot_ota_rotation_health_sample_s *sample,
  struct bk7258_boot_ota_rotation_health_result_s *result);

#endif /* BK7258_BOOT_OTA_ROTATION_HEALTH_CORE_H */
