/*
 * boot_ota_trial_core.h - portable N15-D append/read-back controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_TRIAL_CORE_H
#define BK7258_BOOT_OTA_TRIAL_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_select_core.h"

#define BK7258_BOOT_OTA_METADATA_START       \
  BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET
#define BK7258_BOOT_OTA_PROGRAM_GRANULE      32u
#define BK7258_BOOT_OTA_PROGRAM_CHUNKS       \
  (BK7258_BOOT_OTA_RECORD_SIZE / BK7258_BOOT_OTA_PROGRAM_GRANULE)
#define BK7258_BOOT_OTA_TRIAL_SCRATCH_SIZE   \
  (2u * BK7258_BOOT_OTA_RECORD_SIZE)

enum bk7258_boot_ota_trial_phase_e
{
  BK7258_BOOT_OTA_TRIAL_IDLE = 0,
  BK7258_BOOT_OTA_TRIAL_GATE_CHECK,
  BK7258_BOOT_OTA_TRIAL_LOCKED,
  BK7258_BOOT_OTA_TRIAL_METADATA_READ,
  BK7258_BOOT_OTA_TRIAL_PREPARED,
  BK7258_BOOT_OTA_TRIAL_PROGRAMMING,
  BK7258_BOOT_OTA_TRIAL_CHUNK_VERIFIED,
  BK7258_BOOT_OTA_TRIAL_FINAL_READBACK,
  BK7258_BOOT_OTA_TRIAL_COMMITTED
};

typedef bool (*bk7258_boot_ota_trial_gate_t)(void *arg);
typedef int (*bk7258_boot_ota_trial_lock_t)(void *arg,
                                            uint32_t timeout_ms);
typedef void (*bk7258_boot_ota_trial_unlock_t)(void *arg);
typedef int (*bk7258_boot_ota_trial_read_t)(void *arg, uint32_t address,
                                            uint8_t *data, size_t len);
typedef int (*bk7258_boot_ota_trial_write_t)(void *arg, uint32_t address,
                                             const uint8_t *data,
                                             size_t len);

struct bk7258_boot_ota_trial_ops_s
{
  void *arg;
  bk7258_boot_ota_trial_gate_t compile_write_enabled;
  bk7258_boot_ota_trial_gate_t runtime_write_enabled;
  bk7258_boot_ota_trial_lock_t lock;
  bk7258_boot_ota_trial_unlock_t unlock;
  bk7258_boot_ota_trial_read_t read;
  bk7258_boot_ota_trial_write_t write;
};

struct bk7258_boot_ota_trial_result_s
{
  int status;
  enum bk7258_boot_ota_trial_phase_e phase;
  enum bk7258_boot_ota_metadata_state_e previous_state;
  enum bk7258_boot_ota_metadata_state_e next_state;
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

/* Perform exactly one legal append while holding the caller's Flash guard.
 * The caller supplies 4 KiB for the metadata snapshot and 1 KiB scratch.
 * Each 32-byte SDK-compatible program chunk is immediately read back, then
 * the complete 512-byte record and the whole chain are verified again.
 *
 * current_boot_trial is true only for a fully verified
 * PENDING_B -> TRIAL_STARTED append.  Persisted TRIAL_STARTED remains a
 * consumed trial to the normal selector, so a reset before handoff returns A.
 */

int bk7258_boot_ota_trial_transition(
  uint64_t expected_generation,
  enum bk7258_boot_ota_metadata_state_e expected_state,
  enum bk7258_boot_ota_metadata_state_e next_state,
  const struct bk7258_boot_ota_trial_ops_s *ops, uint32_t timeout_ms,
  uint8_t metadata[BK7258_BOOT_OTA_METADATA_SIZE],
  uint8_t scratch[BK7258_BOOT_OTA_TRIAL_SCRATCH_SIZE],
  struct bk7258_boot_ota_trial_result_s *result);

#endif /* BK7258_BOOT_OTA_TRIAL_CORE_H */
