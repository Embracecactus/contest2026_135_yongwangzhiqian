/****************************************************************************
 * arch/arm/include/bk7258/bk7258_ota_trial.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-only N15 format-2 dual-bank publish/confirm/rollback wrapper.
 ****************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_TRIAL_H
#define __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_TRIAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BK7258_OTA_PENDING_RECORD_SIZE 512u

struct bk7258_ota_publish_result_s
{
  int status;
  uint32_t phase;
  uint32_t previous_state;
  uint32_t previous_records;
  uint32_t previous_bank;
  uint32_t published_bank;
  uint32_t stable_slot;
  uint32_t target_slot;
  uint32_t programmed_chunks;
  uint32_t verified_chunks;
  uint64_t previous_generation;
  uint64_t generation;
  bool base_verified;
  bool candidate_verified;
  bool metadata_degraded;
  bool mutation_attempted;
  bool sector_reclaimed;
  bool erase_verified;
  bool readback_verified;
  bool idempotent;
};

struct bk7258_ota_trial_status_s
{
  int status;
  uint32_t format;
  uint32_t state;
  uint32_t valid_records;
  uint32_t selected_bank;
  uint32_t base_slot;
  uint32_t target_slot;
  uint32_t stable_slot;
  uint32_t active_slot;
  uint64_t sequence;
  uint64_t generation;
  bool metadata_valid;
  bool metadata_trusted;
  bool metadata_erased;
  bool metadata_degraded;
  bool secondary_mapping_active;
  bool staging_write_enabled;
  bool metadata_write_enabled;
};

int bk7258_ota_trial_initialize(void);
int bk7258_ota_trial_confirm(uint64_t expected_generation,
                             uint32_t timeout_ms);
int bk7258_ota_trial_rollback(uint64_t expected_generation,
                              uint32_t timeout_ms);
int bk7258_ota_trial_get_status(struct bk7258_ota_trial_status_s *status,
                                uint32_t timeout_ms);
int bk7258_ota_publish_pending(
  const uint8_t pending_record[BK7258_OTA_PENDING_RECORD_SIZE],
  uint64_t expected_generation, uint32_t timeout_ms,
  struct bk7258_ota_publish_result_s *result);
bool bk7258_ota_trial_write_enabled(void);

#ifdef CONFIG_BK7258_OTA_TRIAL_WRITE
int bk7258_ota_trial_set_write_enabled(bool enabled);
#endif

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_TRIAL_H */
