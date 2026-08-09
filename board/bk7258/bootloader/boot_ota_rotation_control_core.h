/*
 * boot_ota_rotation_control_core.h - selected-bank format-2 transition.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_ROTATION_CONTROL_CORE_H
#define BK7258_BOOT_OTA_ROTATION_CONTROL_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_ota_rotation_core.h"
#include "boot_ota_trial_core.h"

#define BK7258_BOOT_OTA_ROTATION_CONTROL_WORKSPACE_SIZE \
  (2u * BK7258_BOOT_OTA_ROTATION_BANK_SIZE + \
   BK7258_BOOT_OTA_TRIAL_SCRATCH_SIZE)

struct bk7258_boot_ota_rotation_control_result_s
{
  int status;
  enum bk7258_boot_ota_trial_phase_e phase;
  enum bk7258_boot_ota_rotation_state_e previous_state;
  enum bk7258_boot_ota_rotation_state_e next_state;
  enum bk7258_boot_ota_slot_e base_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint32_t selected_bank;
  uint32_t bank_address;
  uint32_t record_index;
  uint32_t record_offset;
  uint32_t programmed_chunks;
  uint32_t verified_chunks;
  uint64_t sequence;
  uint64_t generation;
  bool lock_acquired;
  bool unlock_completed;
  bool metadata_degraded;
  bool mutation_attempted;
  bool readback_verified;
  bool current_boot_trial;
};

/* Read and select both metadata banks while holding one Flash guard, then
 * append exactly one legal transition to the newest trusted generation.
 * This closes the read/select/write race that would exist if a CP adapter
 * selected a bank before calling the single-bank boot transition helper.
 */

int bk7258_boot_ota_rotation_control_transition(
  uint64_t expected_generation,
  enum bk7258_boot_ota_rotation_state_e expected_state,
  enum bk7258_boot_ota_rotation_state_e next_state,
  const struct bk7258_boot_ota_trial_ops_s *ops, uint32_t timeout_ms,
  uint8_t *workspace, size_t workspace_size,
  struct bk7258_boot_ota_rotation_control_result_s *result);

#endif /* BK7258_BOOT_OTA_ROTATION_CONTROL_CORE_H */
