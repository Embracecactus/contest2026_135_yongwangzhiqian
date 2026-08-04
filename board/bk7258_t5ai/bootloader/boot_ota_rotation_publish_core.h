/*
 * boot_ota_rotation_publish_core.h - format-2 dual-bank publisher.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_ROTATION_PUBLISH_CORE_H
#define BK7258_BOOT_OTA_ROTATION_PUBLISH_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_rotation_core.h"
#include "boot_ota_select_core.h"

#define BK7258_BOOT_OTA_ROTATION_PUBLISH_WORKSPACE_SIZE \
  (2u * BK7258_BOOT_OTA_ROTATION_BANK_SIZE + \
   BK7258_OTA_STAGE_SCRATCH_SIZE)

enum bk7258_boot_ota_rotation_publish_phase_e
{
  BK7258_BOOT_OTA_ROTATION_PUBLISH_IDLE = 0,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_GATE_CHECK,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_LOCKED,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_BANKS_READ,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_RECORD_VERIFIED,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_PAIRS_VERIFIED,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_ERASING,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_ERASE_VERIFIED,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_PROGRAMMING,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_CHUNK_VERIFIED,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_FINAL_READBACK,
  BK7258_BOOT_OTA_ROTATION_PUBLISH_COMMITTED
};

typedef uint64_t (*bk7258_boot_ota_rotation_publish_now_t)(void *arg);
typedef bool (*bk7258_boot_ota_rotation_publish_gate_t)(void *arg);
typedef enum bk7258_boot_ota_slot_e
  (*bk7258_boot_ota_rotation_publish_active_slot_t)(void *arg);
typedef int (*bk7258_boot_ota_rotation_publish_lock_t)(void *arg,
                                                        uint32_t timeout_ms);
typedef void (*bk7258_boot_ota_rotation_publish_unlock_t)(void *arg);
typedef int (*bk7258_boot_ota_rotation_publish_read_t)(void *arg,
                                                        uint32_t address,
                                                        uint8_t *data,
                                                        size_t len);
typedef int (*bk7258_boot_ota_rotation_publish_erase_t)(void *arg,
                                                         uint32_t address);
typedef int (*bk7258_boot_ota_rotation_publish_write_t)(void *arg,
                                                         uint32_t address,
                                                         const uint8_t *data,
                                                         size_t len);

struct bk7258_boot_ota_rotation_publish_ops_s
{
  void *arg;
  bk7258_boot_ota_rotation_publish_now_t now_ms;
  bk7258_boot_ota_rotation_publish_gate_t compile_write_enabled;
  bk7258_boot_ota_rotation_publish_gate_t runtime_write_enabled;
  bk7258_boot_ota_rotation_publish_active_slot_t active_slot;
  bk7258_boot_ota_rotation_publish_lock_t lock;
  bk7258_boot_ota_rotation_publish_unlock_t unlock;
  bk7258_boot_ota_rotation_publish_read_t read;
  bk7258_boot_ota_rotation_publish_erase_t erase_sector;
  bk7258_boot_ota_rotation_publish_write_t write;
};

struct bk7258_boot_ota_rotation_publish_result_s
{
  int status;
  enum bk7258_boot_ota_rotation_publish_phase_e phase;
  enum bk7258_boot_ota_rotation_state_e previous_state;
  enum bk7258_boot_ota_slot_e stable_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint32_t previous_bank;
  uint32_t published_bank;
  uint32_t previous_records;
  uint32_t programmed_chunks;
  uint32_t verified_chunks;
  uint64_t previous_generation;
  uint64_t generation;
  bool lock_acquired;
  bool unlock_completed;
  bool metadata_degraded;
  bool base_verified;
  bool candidate_verified;
  bool mutation_attempted;
  bool bank_reclaimed;
  bool erase_verified;
  bool readback_verified;
  bool idempotent;
};

/* Publish a canonical format-2 PENDING_A/PENDING_B record into the metadata
 * bank opposite the currently selected durable bank.  Both executable pairs
 * are fully verified before mutation.  The selected old bank is never
 * erased, so a reset at every erase/program/read-back boundary remains
 * recoverable.  A persisted TRIAL_A/TRIAL_B may start a newer generation only
 * after boot has returned to its base mapping; active-slot authority enforces
 * that distinction.  With two erased banks the first generation uses bank 0.
 */

int bk7258_boot_ota_rotation_publish_pending(
  const uint8_t pending_record[BK7258_BOOT_OTA_ROTATION_RECORD_SIZE],
  uint64_t expected_generation,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct bk7258_boot_ota_rotation_publish_ops_s *ops,
  uint32_t timeout_ms, uint8_t *workspace, size_t workspace_size,
  struct bk7258_boot_ota_rotation_publish_result_s *result);

#endif /* BK7258_BOOT_OTA_ROTATION_PUBLISH_CORE_H */
