/*
 * boot_ota_rotation_select_core.h - format-2 symmetric boot selector.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_ROTATION_SELECT_CORE_H
#define BK7258_BOOT_OTA_ROTATION_SELECT_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_rotation_core.h"
#include "boot_ota_select_core.h"

enum bk7258_boot_ota_rotation_decision_e
{
  BK7258_BOOT_OTA_ROTATION_DECISION_A_BASELINE = 0,
  BK7258_BOOT_OTA_ROTATION_DECISION_BASE_STABLE = 1,
  BK7258_BOOT_OTA_ROTATION_DECISION_TARGET_TRIAL = 2,
  BK7258_BOOT_OTA_ROTATION_DECISION_TARGET_CONFIRMED = 3,
  BK7258_BOOT_OTA_ROTATION_DECISION_BASE_RECOVERY = 4,
  BK7258_BOOT_OTA_ROTATION_DECISION_A_METADATA_RECOVERY = 5
};

enum bk7258_boot_ota_rotation_reason_e
{
  BK7258_BOOT_OTA_ROTATION_REASON_METADATA_ERASED = 0,
  BK7258_BOOT_OTA_ROTATION_REASON_PENDING_VALID = 1,
  BK7258_BOOT_OTA_ROTATION_REASON_TRIAL_CONSUMED = 2,
  BK7258_BOOT_OTA_ROTATION_REASON_CONFIRMED_VALID = 3,
  BK7258_BOOT_OTA_ROTATION_REASON_ROLLBACK_REQUESTED = 4,
  BK7258_BOOT_OTA_ROTATION_REASON_CANDIDATE_INVALID = 5,
  BK7258_BOOT_OTA_ROTATION_REASON_METADATA_INVALID = 6
};

struct bk7258_boot_ota_rotation_result_s
{
  int status;
  enum bk7258_boot_ota_rotation_decision_e decision;
  enum bk7258_boot_ota_rotation_reason_e reason;
  enum bk7258_boot_ota_rotation_state_e state;
  enum bk7258_boot_ota_slot_e boot_slot;
  enum bk7258_boot_ota_slot_e base_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint32_t selected_bank;
  uint32_t valid_records;
  uint64_t sequence;
  uint64_t generation;
  bool metadata_valid;
  bool metadata_degraded;
  bool base_verified;
  bool target_verified;
  bool trial_required;
};

/* The two banks are read sequentially into bank_workspace.  On a successful
 * metadata-backed decision the selected bank is re-read last and remains in
 * that buffer, allowing the boot transition writer to append without a
 * second 4 KiB allocation.
 */

int bk7258_boot_ota_rotation_select_core(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t bank_workspace[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_boot_ota_rotation_result_s *result);

#endif /* BK7258_BOOT_OTA_ROTATION_SELECT_CORE_H */
