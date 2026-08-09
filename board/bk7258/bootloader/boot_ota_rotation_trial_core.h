/*
 * boot_ota_rotation_trial_core.h - format-2 append/read-back controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_ROTATION_TRIAL_CORE_H
#define BK7258_BOOT_OTA_ROTATION_TRIAL_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_ota_rotation_core.h"
#include "boot_ota_trial_core.h"

struct bk7258_boot_ota_rotation_trial_result_s
{
  int status;
  enum bk7258_boot_ota_trial_phase_e phase;
  enum bk7258_boot_ota_rotation_state_e previous_state;
  enum bk7258_boot_ota_rotation_state_e next_state;
  enum bk7258_boot_ota_slot_e base_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint32_t bank_address;
  uint32_t record_index;
  uint32_t record_offset;
  uint32_t programmed_chunks;
  uint32_t verified_chunks;
  uint64_t sequence;
  uint64_t generation;
  bool lock_acquired;
  bool unlock_completed;
  bool mutation_attempted;
  bool readback_verified;
  bool current_boot_trial;
};

int bk7258_boot_ota_rotation_trial_transition(
  uint32_t bank_address, uint64_t expected_generation,
  enum bk7258_boot_ota_rotation_state_e expected_state,
  enum bk7258_boot_ota_rotation_state_e next_state,
  const struct bk7258_boot_ota_trial_ops_s *ops, uint32_t timeout_ms,
  uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  uint8_t scratch[BK7258_BOOT_OTA_TRIAL_SCRATCH_SIZE],
  struct bk7258_boot_ota_rotation_trial_result_s *result);

#endif /* BK7258_BOOT_OTA_ROTATION_TRIAL_CORE_H */
