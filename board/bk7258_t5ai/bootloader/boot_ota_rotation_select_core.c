/*
 * boot_ota_rotation_select_core.c - format-2 symmetric boot selector.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable code only: callers provide raw-Flash and SHA-256 operations.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_rotation_select_core.h"
#include "../chip/include/bk7258_partition_layout.h"

#define BANK0_START BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET
#define BANK1_START BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET
#define SLOT_A_START BK7258_ROLE_SLOT_A_CP_OFFSET
#define SLOT_B_START BK7258_ROLE_SLOT_B_PAIR_OFFSET

static void bytes_copy(uint8_t *destination, const uint8_t *source,
                       size_t len)
{
  while (len-- != 0)
    {
      *destination++ = *source++;
    }
}

static uint32_t slot_start(enum bk7258_boot_ota_slot_e slot)
{
  return slot == BK7258_BOOT_OTA_SLOT_A ? SLOT_A_START : SLOT_B_START;
}

static bool pending_state(enum bk7258_boot_ota_rotation_state_e state)
{
  return state == BK7258_BOOT_OTA_ROTATION_PENDING_A ||
         state == BK7258_BOOT_OTA_ROTATION_PENDING_B;
}

static bool trial_state(enum bk7258_boot_ota_rotation_state_e state)
{
  return state == BK7258_BOOT_OTA_ROTATION_TRIAL_A ||
         state == BK7258_BOOT_OTA_ROTATION_TRIAL_B;
}

static bool confirmed_state(enum bk7258_boot_ota_rotation_state_e state)
{
  return state == BK7258_BOOT_OTA_ROTATION_CONFIRMED_A ||
         state == BK7258_BOOT_OTA_ROTATION_CONFIRMED_B;
}

static bool rollback_state(enum bk7258_boot_ota_rotation_state_e state)
{
  return state == BK7258_BOOT_OTA_ROTATION_ROLLBACK_A ||
         state == BK7258_BOOT_OTA_ROTATION_ROLLBACK_B;
}

static int raw_read(const struct bk7258_boot_ota_raw_ops_s *ops,
                    uint32_t address, uint8_t *buffer, size_t len)
{
  int ret;

  if (ops == NULL || ops->read == NULL || buffer == NULL || len == 0 ||
      address >= BK7258_FLASH_SIZE || len > BK7258_FLASH_SIZE - address)
    {
      return -EINVAL;
    }

  ret = ops->read(ops->arg, address, buffer, len);
  return ret > 0 ? -EIO : ret;
}

static void clear_bank_info(struct bk7258_boot_ota_rotation_bank_s *info)
{
  info->state = BK7258_BOOT_OTA_ROTATION_ERASED;
  info->base_slot = BK7258_BOOT_OTA_SLOT_A;
  info->target_slot = BK7258_BOOT_OTA_SLOT_B;
  info->valid_records = 0;
  info->last_record_index = 0;
  info->sequence = 0;
  info->generation = 0;
  info->erased = false;
  info->trusted = false;
}

static void initialize_result(
  struct bk7258_boot_ota_rotation_result_s *result)
{
  result->status = -EINPROGRESS;
  result->decision = BK7258_BOOT_OTA_ROTATION_DECISION_A_METADATA_RECOVERY;
  result->reason = BK7258_BOOT_OTA_ROTATION_REASON_METADATA_INVALID;
  result->state = BK7258_BOOT_OTA_ROTATION_ERASED;
  result->boot_slot = BK7258_BOOT_OTA_SLOT_A;
  result->base_slot = BK7258_BOOT_OTA_SLOT_A;
  result->target_slot = BK7258_BOOT_OTA_SLOT_B;
  result->selected_bank = BK7258_BOOT_OTA_ROTATION_NO_BANK;
  result->valid_records = 0;
  result->sequence = 0;
  result->generation = 0;
  result->metadata_valid = false;
  result->metadata_degraded = false;
  result->base_verified = false;
  result->target_verified = false;
  result->trial_required = false;
}

static int recover_a(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_boot_ota_rotation_result_s *result,
  bool metadata_erased)
{
  int ret = bk7258_boot_ota_validate_pair_headers(
    SLOT_A_START, raw_ops, scratch, scratch_size);

  if (ret < 0)
    {
      result->status = ret;
      return ret;
    }

  result->boot_slot = BK7258_BOOT_OTA_SLOT_A;
  result->base_slot = BK7258_BOOT_OTA_SLOT_A;
  result->base_verified = true;
  result->decision = metadata_erased ?
    BK7258_BOOT_OTA_ROTATION_DECISION_A_BASELINE :
    BK7258_BOOT_OTA_ROTATION_DECISION_A_METADATA_RECOVERY;
  result->reason = metadata_erased ?
    BK7258_BOOT_OTA_ROTATION_REASON_METADATA_ERASED :
    BK7258_BOOT_OTA_ROTATION_REASON_METADATA_INVALID;
  result->status = 0;
  return 0;
}

int bk7258_boot_ota_rotation_select_core(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t bank_workspace[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_boot_ota_rotation_result_s *result)
{
  struct bk7258_boot_ota_rotation_bank_s banks[2];
  struct bk7258_boot_ota_rotation_view_s view;
  struct bk7258_boot_ota_rotation_identity_s identity;
  struct bk7258_boot_ota_base_pair_s base;
  struct bk7258_ota_expected_s expected;
  uint32_t bank_address[2] = {BANK0_START, BANK1_START};
  bool bank_invalid[2] = {false, false};
  int base_ret;
  int target_ret = -ENODATA;
  int ret;
  uint32_t index;

  if (raw_ops == NULL || hash_ops == NULL || bank_workspace == NULL ||
      scratch == NULL ||
      scratch_size < BK7258_OTA_STAGE_SCRATCH_SIZE || result == NULL)
    {
      return -EINVAL;
    }

  initialize_result(result);
  for (index = 0; index < 2; index++)
    {
      clear_bank_info(&banks[index]);
      ret = raw_read(raw_ops, bank_address[index], bank_workspace,
                     BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
      if (ret < 0 ||
          bk7258_boot_ota_rotation_inspect(bank_workspace,
                                            &banks[index]) < 0)
        {
          clear_bank_info(&banks[index]);
          bank_invalid[index] = true;
        }
    }

  result->metadata_degraded = bank_invalid[0] || bank_invalid[1];
  ret = bk7258_boot_ota_rotation_select(banks, &view);
  if (ret < 0)
    {
      result->metadata_degraded = true;
      return recover_a(raw_ops, scratch, scratch_size, result, false);
    }

  if (!view.metadata_present)
    {
      return recover_a(raw_ops, scratch, scratch_size, result, true);
    }

  ret = raw_read(raw_ops, bank_address[view.selected_bank], bank_workspace,
                 BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  if (ret < 0)
    {
      result->status = ret;
      return ret;
    }

  ret = bk7258_boot_ota_rotation_latest(bank_workspace, &identity);
  if (ret < 0 || identity.state != view.state ||
      identity.sequence != view.sequence ||
      identity.generation != view.generation ||
      identity.base_slot == identity.target_slot)
    {
      result->status = ret < 0 ? ret : -EBADMSG;
      return result->status;
    }

  result->state = identity.state;
  result->boot_slot = identity.base_slot;
  result->base_slot = identity.base_slot;
  result->target_slot = identity.target_slot;
  result->selected_bank = view.selected_bank;
  result->valid_records = view.valid_records;
  result->sequence = view.sequence;
  result->generation = view.generation;
  result->metadata_valid = true;

  base.cp_physical_length = identity.base_cp_physical_length;
  base.ap_physical_length = identity.base_ap_physical_length;
  base.sha256 = identity.base_sha256;
  base_ret = bk7258_boot_ota_validate_base_pair(
    slot_start(identity.base_slot), &base, raw_ops, hash_ops, scratch,
    scratch_size);
  result->base_verified = base_ret == 0;

  if (pending_state(identity.state) || confirmed_state(identity.state))
    {
      expected.generation = identity.generation;
      expected.timestamp = identity.timestamp;
      bytes_copy((uint8_t *)expected.version, identity.version,
                 BK7258_BOOT_OTA_ROTATION_VERSION_SIZE);
      bytes_copy((uint8_t *)expected.base_version, identity.base_version,
                 BK7258_BOOT_OTA_ROTATION_VERSION_SIZE);
      target_ret = bk7258_boot_ota_validate_candidate_pair(
        slot_start(identity.target_slot), identity.descriptor, &expected,
        raw_ops, hash_ops, scratch, scratch_size);
      result->target_verified = target_ret == 0;
    }

  if (pending_state(identity.state))
    {
      if (base_ret < 0)
        {
          result->status = base_ret;
          return base_ret;
        }

      if (target_ret < 0)
        {
          result->decision =
            BK7258_BOOT_OTA_ROTATION_DECISION_BASE_RECOVERY;
          result->reason =
            BK7258_BOOT_OTA_ROTATION_REASON_CANDIDATE_INVALID;
        }
      else
        {
          result->decision =
            BK7258_BOOT_OTA_ROTATION_DECISION_TARGET_TRIAL;
          result->reason = BK7258_BOOT_OTA_ROTATION_REASON_PENDING_VALID;
          result->trial_required = true;
        }
    }
  else if (trial_state(identity.state))
    {
      if (base_ret < 0)
        {
          result->status = base_ret;
          return base_ret;
        }

      result->decision = BK7258_BOOT_OTA_ROTATION_DECISION_BASE_STABLE;
      result->reason = BK7258_BOOT_OTA_ROTATION_REASON_TRIAL_CONSUMED;
    }
  else if (rollback_state(identity.state))
    {
      if (base_ret < 0)
        {
          result->status = base_ret;
          return base_ret;
        }

      result->decision = BK7258_BOOT_OTA_ROTATION_DECISION_BASE_STABLE;
      result->reason =
        BK7258_BOOT_OTA_ROTATION_REASON_ROLLBACK_REQUESTED;
    }
  else if (confirmed_state(identity.state))
    {
      if (target_ret == 0)
        {
          result->boot_slot = identity.target_slot;
          result->decision =
            BK7258_BOOT_OTA_ROTATION_DECISION_TARGET_CONFIRMED;
          result->reason =
            BK7258_BOOT_OTA_ROTATION_REASON_CONFIRMED_VALID;
        }
      else if (base_ret == 0)
        {
          result->decision =
            BK7258_BOOT_OTA_ROTATION_DECISION_BASE_RECOVERY;
          result->reason =
            BK7258_BOOT_OTA_ROTATION_REASON_CANDIDATE_INVALID;
        }
      else
        {
          result->status = target_ret < 0 ? target_ret : base_ret;
          return result->status;
        }
    }
  else
    {
      result->status = -EBADMSG;
      return -EBADMSG;
    }

  result->status = 0;
  return 0;
}
