/*
 * boot_ota_rotation_core.h - portable symmetric OTA metadata bank core.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_OTA_ROTATION_CORE_H
#define BK7258_BOOT_OTA_ROTATION_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BK7258_BOOT_OTA_ROTATION_BANK_SIZE       4096u
#define BK7258_BOOT_OTA_ROTATION_RECORD_SIZE      512u
#define BK7258_BOOT_OTA_ROTATION_RECORD_COUNT       8u
#define BK7258_BOOT_OTA_ROTATION_DESCRIPTOR_SIZE   384u
#define BK7258_BOOT_OTA_ROTATION_SHA256_SIZE        32u
#define BK7258_BOOT_OTA_ROTATION_VERSION_SIZE       24u
#define BK7258_BOOT_OTA_ROTATION_NO_BANK      UINT32_MAX

enum bk7258_boot_ota_slot_e
{
  BK7258_BOOT_OTA_SLOT_A = 0,
  BK7258_BOOT_OTA_SLOT_B = 1
};

/* Keep the format-1 B-family values stable while adding the symmetric
 * A-family.  ROLLBACK_A means the B candidate was rejected and A is stable;
 * ROLLBACK_B is the mirror image.
 */

enum bk7258_boot_ota_rotation_state_e
{
  BK7258_BOOT_OTA_ROTATION_ERASED = 0,
  BK7258_BOOT_OTA_ROTATION_PENDING_B = 1,
  BK7258_BOOT_OTA_ROTATION_TRIAL_B = 2,
  BK7258_BOOT_OTA_ROTATION_CONFIRMED_B = 3,
  BK7258_BOOT_OTA_ROTATION_ROLLBACK_A = 4,
  BK7258_BOOT_OTA_ROTATION_PENDING_A = 5,
  BK7258_BOOT_OTA_ROTATION_TRIAL_A = 6,
  BK7258_BOOT_OTA_ROTATION_CONFIRMED_A = 7,
  BK7258_BOOT_OTA_ROTATION_ROLLBACK_B = 8
};

struct bk7258_boot_ota_rotation_pending_s
{
  uint64_t generation;
  uint32_t timestamp;
  uint32_t base_cp_physical_length;
  uint32_t base_ap_physical_length;
  const char *version;
  const char *base_version;
  const uint8_t *base_sha256;
  const uint8_t *descriptor;
  enum bk7258_boot_ota_slot_e target_slot;
};

struct bk7258_boot_ota_rotation_bank_s
{
  enum bk7258_boot_ota_rotation_state_e state;
  enum bk7258_boot_ota_slot_e base_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint32_t valid_records;
  uint32_t last_record_index;
  uint64_t sequence;
  uint64_t generation;
  bool erased;
  bool trusted;
};

struct bk7258_boot_ota_rotation_view_s
{
  enum bk7258_boot_ota_rotation_state_e state;
  enum bk7258_boot_ota_slot_e stable_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint32_t selected_bank;
  uint32_t valid_records;
  uint64_t sequence;
  uint64_t generation;
  bool metadata_present;
  bool trial_required;
};

/* Borrowed views into the selected bank's latest record.  The pointers stay
 * valid only while the caller keeps the 4 KiB bank buffer unchanged.  This
 * avoids copying another 472 bytes onto the Tier-1 boot stack while still
 * giving the image-validation layer the complete, already parsed identity.
 */

struct bk7258_boot_ota_rotation_identity_s
{
  enum bk7258_boot_ota_rotation_state_e state;
  enum bk7258_boot_ota_slot_e base_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint64_t sequence;
  uint64_t generation;
  uint32_t timestamp;
  uint32_t base_cp_physical_length;
  uint32_t base_ap_physical_length;
  const uint8_t *version;
  const uint8_t *base_version;
  const uint8_t *base_sha256;
  const uint8_t *descriptor;
};

struct bk7258_boot_ota_rotation_transition_s
{
  enum bk7258_boot_ota_rotation_state_e previous_state;
  enum bk7258_boot_ota_rotation_state_e next_state;
  enum bk7258_boot_ota_slot_e base_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint32_t record_index;
  uint32_t record_offset;
  uint64_t sequence;
  uint64_t generation;
  bool current_boot_trial;
};

int bk7258_boot_ota_rotation_state_slots(
  enum bk7258_boot_ota_rotation_state_e state,
  enum bk7258_boot_ota_slot_e *base_slot,
  enum bk7258_boot_ota_slot_e *target_slot);

int bk7258_boot_ota_rotation_build_pending(
  const struct bk7258_boot_ota_rotation_pending_s *pending,
  uint8_t record[BK7258_BOOT_OTA_ROTATION_RECORD_SIZE]);

int bk7258_boot_ota_rotation_inspect(
  const uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  struct bk7258_boot_ota_rotation_bank_s *info);

int bk7258_boot_ota_rotation_select(
  const struct bk7258_boot_ota_rotation_bank_s banks[2],
  struct bk7258_boot_ota_rotation_view_s *view);

int bk7258_boot_ota_rotation_latest(
  const uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  struct bk7258_boot_ota_rotation_identity_s *identity);

int bk7258_boot_ota_rotation_prepare_transition(
  const uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  uint64_t expected_generation,
  enum bk7258_boot_ota_rotation_state_e expected_state,
  enum bk7258_boot_ota_rotation_state_e next_state,
  uint8_t record[BK7258_BOOT_OTA_ROTATION_RECORD_SIZE],
  struct bk7258_boot_ota_rotation_transition_s *transition);

#endif /* BK7258_BOOT_OTA_ROTATION_CORE_H */
