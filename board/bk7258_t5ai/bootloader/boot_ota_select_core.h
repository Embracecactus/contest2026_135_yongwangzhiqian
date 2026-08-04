/*
 * boot_ota_select_core.h - portable N15-C metadata and pair selector.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_SELECT_CORE_H
#define BK7258_BOOT_OTA_SELECT_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../chip/include/bk7258_partition_layout.h"
#include "../chip/cp/bk7258_ota_staging_core.h"

#define BK7258_BOOT_OTA_METADATA_SIZE BK7258_ROLE_OTA_METADATA_PRIMARY_SIZE
#define BK7258_BOOT_OTA_RECORD_SIZE    512u
#define BK7258_BOOT_OTA_RECORD_COUNT   8u

enum bk7258_boot_ota_metadata_state_e
{
  BK7258_BOOT_OTA_META_ERASED = 0,
  BK7258_BOOT_OTA_META_PENDING_B = 1,
  BK7258_BOOT_OTA_META_TRIAL_STARTED = 2,
  BK7258_BOOT_OTA_META_CONFIRMED_B = 3,
  BK7258_BOOT_OTA_META_ROLLBACK_A = 4
};

enum bk7258_boot_ota_decision_e
{
  BK7258_BOOT_OTA_DECISION_A_BASELINE = 0,
  BK7258_BOOT_OTA_DECISION_A_FAILSAFE = 1,
  BK7258_BOOT_OTA_DECISION_A_ROLLBACK = 2,
  BK7258_BOOT_OTA_DECISION_B_TRIAL_CANDIDATE = 3,
  BK7258_BOOT_OTA_DECISION_B_CONFIRMED = 4
};

enum bk7258_boot_ota_reason_e
{
  BK7258_BOOT_OTA_REASON_NONE = 0,
  BK7258_BOOT_OTA_REASON_METADATA_ERASED = 1,
  BK7258_BOOT_OTA_REASON_METADATA_INVALID = 2,
  BK7258_BOOT_OTA_REASON_CANDIDATE_INVALID = 3,
  BK7258_BOOT_OTA_REASON_TRIAL_CONSUMED = 4,
  BK7258_BOOT_OTA_REASON_ROLLBACK_REQUESTED = 5,
  BK7258_BOOT_OTA_REASON_PENDING_VALID = 6,
  BK7258_BOOT_OTA_REASON_CONFIRMED_VALID = 7
};

typedef int (*bk7258_boot_ota_raw_read_t)(void *arg, uint32_t address,
                                          uint8_t *buffer, size_t len);

struct bk7258_boot_ota_raw_ops_s
{
  void *arg;
  bk7258_boot_ota_raw_read_t read;
};

struct bk7258_boot_ota_base_pair_s
{
  uint32_t cp_physical_length;
  uint32_t ap_physical_length;
  const uint8_t *sha256;
};

struct bk7258_boot_ota_result_s
{
  int status;
  enum bk7258_boot_ota_decision_e decision;
  enum bk7258_boot_ota_reason_e reason;
  enum bk7258_boot_ota_metadata_state_e metadata_state;
  uint32_t valid_records;
  uint64_t generation;
  bool metadata_valid;
  bool primary_verified;
  bool primary_full_verified;
  bool secondary_verified;
};

/* A structural metadata inspection never reads either executable pair.  It
 * is the single parser used by N15-C selection and N15-D append preparation,
 * so the writer cannot accept a record chain the boot selector would reject.
 */

struct bk7258_boot_ota_metadata_info_s
{
  enum bk7258_boot_ota_metadata_state_e state;
  uint32_t valid_records;
  uint64_t sequence;
  uint64_t generation;
  bool erased;
  bool trusted;
};

struct bk7258_boot_ota_transition_s
{
  enum bk7258_boot_ota_metadata_state_e previous_state;
  enum bk7258_boot_ota_metadata_state_e next_state;
  uint32_t record_index;
  uint32_t record_offset;
  uint64_t sequence;
  uint64_t generation;
};

int bk7258_boot_ota_metadata_inspect(
  const uint8_t metadata[BK7258_BOOT_OTA_METADATA_SIZE],
  struct bk7258_boot_ota_metadata_info_s *info);

int bk7258_boot_ota_prepare_transition(
  const uint8_t metadata[BK7258_BOOT_OTA_METADATA_SIZE],
  uint64_t expected_generation,
  enum bk7258_boot_ota_metadata_state_e expected_state,
  enum bk7258_boot_ota_metadata_state_e next_state,
  uint8_t record[BK7258_BOOT_OTA_RECORD_SIZE],
  struct bk7258_boot_ota_transition_s *transition);

/* Slot-neutral, read-only pair validators shared by the historical format-1
 * selector and the format-2 symmetric selector.  physical_start must be the
 * exact raw start of slot A or B; arbitrary Flash regions are rejected.
 */

int bk7258_boot_ota_validate_pair_headers(
  uint32_t physical_start,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  uint8_t *scratch, size_t scratch_size);

int bk7258_boot_ota_validate_base_pair(
  uint32_t physical_start,
  const struct bk7258_boot_ota_base_pair_s *base,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size);

int bk7258_boot_ota_validate_candidate_pair(
  uint32_t physical_start,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size);

int bk7258_boot_ota_select_core(
  const uint8_t metadata[BK7258_BOOT_OTA_METADATA_SIZE],
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_boot_ota_result_s *result);

#endif /* BK7258_BOOT_OTA_SELECT_CORE_H */
