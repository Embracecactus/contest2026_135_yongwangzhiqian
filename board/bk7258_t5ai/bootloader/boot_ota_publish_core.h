/*
 * boot_ota_publish_core.h - portable N15-E pending publication controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_PUBLISH_CORE_H
#define BK7258_BOOT_OTA_PUBLISH_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_select_core.h"

#define BK7258_BOOT_OTA_PUBLISH_WORKSPACE_SIZE \
  (2u * BK7258_BOOT_OTA_METADATA_SIZE + BK7258_OTA_STAGE_SCRATCH_SIZE)

enum bk7258_boot_ota_publish_phase_e
{
  BK7258_BOOT_OTA_PUBLISH_IDLE = 0,
  BK7258_BOOT_OTA_PUBLISH_GATE_CHECK,
  BK7258_BOOT_OTA_PUBLISH_LOCKED,
  BK7258_BOOT_OTA_PUBLISH_CURRENT_READ,
  BK7258_BOOT_OTA_PUBLISH_CANDIDATE_VERIFIED,
  BK7258_BOOT_OTA_PUBLISH_ERASING,
  BK7258_BOOT_OTA_PUBLISH_ERASE_VERIFIED,
  BK7258_BOOT_OTA_PUBLISH_PROGRAMMING,
  BK7258_BOOT_OTA_PUBLISH_CHUNK_VERIFIED,
  BK7258_BOOT_OTA_PUBLISH_FINAL_READBACK,
  BK7258_BOOT_OTA_PUBLISH_COMMITTED
};

typedef uint64_t (*bk7258_boot_ota_publish_now_t)(void *arg);
typedef bool (*bk7258_boot_ota_publish_gate_t)(void *arg);
typedef int (*bk7258_boot_ota_publish_lock_t)(void *arg,
                                              uint32_t timeout_ms);
typedef void (*bk7258_boot_ota_publish_unlock_t)(void *arg);
typedef int (*bk7258_boot_ota_publish_read_t)(void *arg, uint32_t address,
                                              uint8_t *data, size_t len);
typedef int (*bk7258_boot_ota_publish_erase_t)(void *arg,
                                               uint32_t address);
typedef int (*bk7258_boot_ota_publish_write_t)(void *arg, uint32_t address,
                                               const uint8_t *data,
                                               size_t len);

struct bk7258_boot_ota_publish_ops_s
{
  void *arg;
  bk7258_boot_ota_publish_now_t now_ms;
  bk7258_boot_ota_publish_gate_t compile_write_enabled;
  bk7258_boot_ota_publish_gate_t runtime_write_enabled;
  bk7258_boot_ota_publish_gate_t primary_mapping_active;
  bk7258_boot_ota_publish_lock_t lock;
  bk7258_boot_ota_publish_unlock_t unlock;
  bk7258_boot_ota_publish_read_t read;
  bk7258_boot_ota_publish_erase_t erase_sector;
  bk7258_boot_ota_publish_write_t write;
};

struct bk7258_boot_ota_publish_result_s
{
  int status;
  enum bk7258_boot_ota_publish_phase_e phase;
  enum bk7258_boot_ota_metadata_state_e previous_state;
  uint32_t previous_records;
  uint32_t programmed_chunks;
  uint32_t verified_chunks;
  uint64_t previous_generation;
  uint64_t generation;
  bool lock_acquired;
  bool unlock_completed;
  bool previous_metadata_valid;
  bool previous_metadata_trusted;
  bool candidate_verified;
  bool mutation_attempted;
  bool sector_reclaimed;
  bool erase_verified;
  bool readback_verified;
  bool idempotent;
};

/* Publish one canonical PENDING_B record after validating both live pairs.
 *
 * The caller supplies a 16 KiB workspace.  Current and proposed metadata
 * snapshots occupy the first 8 KiB and the existing selector uses the final
 * 8 KiB.  The operation is legal only while the raw primary mapping is
 * active.  Existing PENDING_B/CONFIRMED_B metadata is never erased.  Erased,
 * consumed-trial, rollback or structurally invalid metadata may be reclaimed;
 * a trusted prior lifecycle also requires a strictly newer generation.
 *
 * A reset before the final record commits leaves the normal selector on A.
 */

int bk7258_boot_ota_publish_pending(
  const uint8_t pending_record[BK7258_BOOT_OTA_RECORD_SIZE],
  uint64_t expected_generation,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct bk7258_boot_ota_publish_ops_s *ops, uint32_t timeout_ms,
  uint8_t *workspace, size_t workspace_size,
  struct bk7258_boot_ota_publish_result_s *result);

#endif /* BK7258_BOOT_OTA_PUBLISH_CORE_H */
