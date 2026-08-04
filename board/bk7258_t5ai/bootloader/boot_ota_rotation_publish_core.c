/*
 * boot_ota_rotation_publish_core.c - format-2 dual-bank publisher.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable code only: no NuttX, SDK, MMIO, heap or libc dependency.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_rotation_publish_core.h"
#include "boot_ota_trial_core.h"
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

static void bytes_fill(uint8_t *destination, uint8_t value, size_t len)
{
  while (len-- != 0)
    {
      *destination++ = value;
    }
}

static bool bytes_equal(const uint8_t *left, const uint8_t *right,
                        size_t len)
{
  while (len-- != 0)
    {
      if (*left++ != *right++)
        {
          return false;
        }
    }

  return true;
}

static bool bytes_value(const uint8_t *data, size_t len, uint8_t value)
{
  while (len-- != 0)
    {
      if (*data++ != value)
        {
          return false;
        }
    }

  return true;
}

static int callback_status(int ret)
{
  return ret > 0 ? -EIO : ret;
}

static uint32_t bank_address(uint32_t bank)
{
  return bank == 0 ? BANK0_START : BANK1_START;
}

static uint32_t slot_address(enum bk7258_boot_ota_slot_e slot)
{
  return slot == BK7258_BOOT_OTA_SLOT_A ? SLOT_A_START : SLOT_B_START;
}

static bool publication_base_stable(
  enum bk7258_boot_ota_rotation_state_e state)
{
  return state == BK7258_BOOT_OTA_ROTATION_TRIAL_A ||
         state == BK7258_BOOT_OTA_ROTATION_TRIAL_B ||
         state == BK7258_BOOT_OTA_ROTATION_CONFIRMED_A ||
         state == BK7258_BOOT_OTA_ROTATION_CONFIRMED_B ||
         state == BK7258_BOOT_OTA_ROTATION_ROLLBACK_A ||
         state == BK7258_BOOT_OTA_ROTATION_ROLLBACK_B;
}

static bool pending_state(enum bk7258_boot_ota_rotation_state_e state)
{
  return state == BK7258_BOOT_OTA_ROTATION_PENDING_A ||
         state == BK7258_BOOT_OTA_ROTATION_PENDING_B;
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

static bool canonical_bank(const uint8_t *bank, const uint8_t *record)
{
  return bytes_equal(bank, record,
                     BK7258_BOOT_OTA_ROTATION_RECORD_SIZE) &&
         bytes_value(bank + BK7258_BOOT_OTA_ROTATION_RECORD_SIZE,
                     BK7258_BOOT_OTA_ROTATION_BANK_SIZE -
                       BK7258_BOOT_OTA_ROTATION_RECORD_SIZE,
                     0xffu);
}

static bool timed_out(
  const struct bk7258_boot_ota_rotation_publish_ops_s *ops,
  uint64_t started, uint32_t timeout_ms)
{
  return ops->now_ms(ops->arg) - started >= (uint64_t)timeout_ms;
}

static int authority_status(
  const struct bk7258_boot_ota_rotation_publish_ops_s *ops,
  uint64_t started, uint32_t timeout_ms,
  enum bk7258_boot_ota_slot_e stable_slot)
{
  enum bk7258_boot_ota_slot_e active;

  if (timed_out(ops, started, timeout_ms))
    {
      return -ETIMEDOUT;
    }

  if (!ops->compile_write_enabled(ops->arg) ||
      !ops->runtime_write_enabled(ops->arg))
    {
      return -EACCES;
    }

  active = ops->active_slot(ops->arg);
  if ((active != BK7258_BOOT_OTA_SLOT_A &&
       active != BK7258_BOOT_OTA_SLOT_B) || active != stable_slot)
    {
      return -EPERM;
    }

  return 0;
}

static int finish(
  const struct bk7258_boot_ota_rotation_publish_ops_s *ops,
  struct bk7258_boot_ota_rotation_publish_result_s *result, int status)
{
  if (result->lock_acquired && !result->unlock_completed)
    {
      ops->unlock(ops->arg);
      result->unlock_completed = true;
    }

  result->status = status;
  return status;
}

static void initialize_result(
  struct bk7258_boot_ota_rotation_publish_result_s *result,
  uint64_t generation)
{
  result->status = -EINPROGRESS;
  result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_IDLE;
  result->previous_state = BK7258_BOOT_OTA_ROTATION_ERASED;
  result->stable_slot = BK7258_BOOT_OTA_SLOT_A;
  result->target_slot = BK7258_BOOT_OTA_SLOT_B;
  result->previous_bank = BK7258_BOOT_OTA_ROTATION_NO_BANK;
  result->published_bank = BK7258_BOOT_OTA_ROTATION_NO_BANK;
  result->previous_records = 0;
  result->programmed_chunks = 0;
  result->verified_chunks = 0;
  result->previous_generation = 0;
  result->generation = generation;
  result->lock_acquired = false;
  result->unlock_completed = false;
  result->metadata_degraded = false;
  result->base_verified = false;
  result->candidate_verified = false;
  result->mutation_attempted = false;
  result->bank_reclaimed = false;
  result->erase_verified = false;
  result->readback_verified = false;
  result->idempotent = false;
}

int bk7258_boot_ota_rotation_publish_pending(
  const uint8_t pending_record[BK7258_BOOT_OTA_ROTATION_RECORD_SIZE],
  uint64_t expected_generation,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct bk7258_boot_ota_rotation_publish_ops_s *ops,
  uint32_t timeout_ms, uint8_t *workspace, size_t workspace_size,
  struct bk7258_boot_ota_rotation_publish_result_s *result)
{
  struct bk7258_boot_ota_rotation_bank_s banks[2];
  struct bk7258_boot_ota_rotation_bank_s proposed_info;
  struct bk7258_boot_ota_rotation_identity_s identity;
  struct bk7258_boot_ota_rotation_view_s view;
  struct bk7258_boot_ota_base_pair_s base;
  struct bk7258_ota_expected_s expected;
  uint8_t *bank_data[2];
  uint8_t *proposed;
  uint8_t *scratch;
  enum bk7258_boot_ota_slot_e stable_slot;
  uint64_t started;
  uint32_t publication_bank;
  uint32_t selected_bank;
  uint32_t chunk;
  bool publication_erased;
  int ret;
  uint32_t index;

  if (result == NULL)
    {
      return -EINVAL;
    }

  initialize_result(result, expected_generation);
  if (pending_record == NULL || expected_generation == 0 ||
      raw_ops == NULL || raw_ops->read == NULL || hash_ops == NULL ||
      ops == NULL || timeout_ms == 0 || workspace == NULL ||
      workspace_size < BK7258_BOOT_OTA_ROTATION_PUBLISH_WORKSPACE_SIZE ||
      ops->now_ms == NULL || ops->compile_write_enabled == NULL ||
      ops->runtime_write_enabled == NULL || ops->active_slot == NULL ||
      ops->lock == NULL || ops->unlock == NULL || ops->read == NULL ||
      ops->erase_sector == NULL || ops->write == NULL)
    {
      result->status = -EINVAL;
      return -EINVAL;
    }

  result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_GATE_CHECK;
  if (!ops->compile_write_enabled(ops->arg) ||
      !ops->runtime_write_enabled(ops->arg))
    {
      result->status = -EACCES;
      return -EACCES;
    }

  started = ops->now_ms(ops->arg);
  ret = callback_status(ops->lock(ops->arg, timeout_ms));
  if (ret != 0)
    {
      result->status = ret;
      return ret;
    }

  result->lock_acquired = true;
  result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_LOCKED;
  bank_data[0] = workspace;
  bank_data[1] = bank_data[0] + BK7258_BOOT_OTA_ROTATION_BANK_SIZE;
  scratch = bank_data[1] + BK7258_BOOT_OTA_ROTATION_BANK_SIZE;

  for (index = 0; index < 2; index++)
    {
      ret = callback_status(ops->read(
        ops->arg, bank_address(index), bank_data[index],
        BK7258_BOOT_OTA_ROTATION_BANK_SIZE));
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      clear_bank_info(&banks[index]);
      if (bk7258_boot_ota_rotation_inspect(bank_data[index],
                                            &banks[index]) < 0)
        {
          clear_bank_info(&banks[index]);
          result->metadata_degraded = true;
        }
    }

  result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_BANKS_READ;
  ret = bk7258_boot_ota_rotation_select(banks, &view);
  if (ret < 0)
    {
      return finish(ops, result, ret);
    }

  if (view.metadata_present)
    {
      selected_bank = view.selected_bank;
      publication_bank = selected_bank ^ 1u;
      stable_slot = view.stable_slot;
      result->previous_state = view.state;
      result->previous_bank = selected_bank;
      result->previous_generation = view.generation;
      result->previous_records = view.valid_records;
    }
  else
    {
      selected_bank = BK7258_BOOT_OTA_ROTATION_NO_BANK;
      publication_bank = 0;
      stable_slot = BK7258_BOOT_OTA_SLOT_A;
    }

  result->stable_slot = stable_slot;
  result->published_bank = publication_bank;
  publication_erased = bytes_value(
    bank_data[publication_bank], BK7258_BOOT_OTA_ROTATION_BANK_SIZE, 0xffu);
  proposed = bank_data[publication_bank];
  bytes_fill(proposed, 0xffu, BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  bytes_copy(proposed, pending_record,
             BK7258_BOOT_OTA_ROTATION_RECORD_SIZE);

  ret = bk7258_boot_ota_rotation_inspect(proposed, &proposed_info);
  if (ret < 0 || !proposed_info.trusted || proposed_info.erased ||
      proposed_info.valid_records != 1u || proposed_info.sequence != 1u ||
      proposed_info.generation != expected_generation ||
      !pending_state(proposed_info.state))
    {
      return finish(ops, result, ret < 0 ? ret : -EBADMSG);
    }

  ret = bk7258_boot_ota_rotation_latest(proposed, &identity);
  if (ret < 0 || identity.base_slot != stable_slot ||
      identity.target_slot == stable_slot)
    {
      return finish(ops, result, ret < 0 ? ret : -EPERM);
    }

  result->target_slot = identity.target_slot;
  ret = authority_status(ops, started, timeout_ms, stable_slot);
  if (ret < 0)
    {
      return finish(ops, result, ret);
    }

  if (view.metadata_present)
    {
      if (pending_state(view.state))
        {
          if (view.generation == expected_generation &&
              canonical_bank(bank_data[selected_bank], pending_record))
            {
              result->published_bank = selected_bank;
              result->idempotent = true;
            }
          else
            {
              return finish(ops, result,
                            view.generation == expected_generation ?
                              -EALREADY : -EBUSY);
            }
        }
      else if (!publication_base_stable(view.state))
        {
          return finish(ops, result, -EBUSY);
        }
      else if (view.generation == UINT64_MAX)
        {
          return finish(ops, result, -EOVERFLOW);
        }
      else if (expected_generation <= view.generation)
        {
          return finish(ops, result, -ESTALE);
        }
    }

  result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_RECORD_VERIFIED;
  base.cp_physical_length = identity.base_cp_physical_length;
  base.ap_physical_length = identity.base_ap_physical_length;
  base.sha256 = identity.base_sha256;
  ret = bk7258_boot_ota_validate_base_pair(
    slot_address(identity.base_slot), &base, raw_ops, hash_ops, scratch,
    BK7258_OTA_STAGE_SCRATCH_SIZE);
  if (ret < 0)
    {
      return finish(ops, result, ret);
    }

  result->base_verified = true;
  expected.generation = identity.generation;
  expected.timestamp = identity.timestamp;
  bytes_copy((uint8_t *)expected.version, identity.version,
             BK7258_BOOT_OTA_ROTATION_VERSION_SIZE);
  bytes_copy((uint8_t *)expected.base_version, identity.base_version,
             BK7258_BOOT_OTA_ROTATION_VERSION_SIZE);
  ret = bk7258_boot_ota_validate_candidate_pair(
    slot_address(identity.target_slot), identity.descriptor, &expected,
    raw_ops, hash_ops, scratch, BK7258_OTA_STAGE_SCRATCH_SIZE);
  if (ret < 0)
    {
      return finish(ops, result, ret);
    }

  result->candidate_verified = true;
  result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_PAIRS_VERIFIED;
  ret = authority_status(ops, started, timeout_ms, stable_slot);
  if (ret < 0)
    {
      return finish(ops, result, ret);
    }

  if (result->idempotent)
    {
      result->erase_verified = true;
      result->readback_verified = true;
      result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_COMMITTED;
      return finish(ops, result, 0);
    }

  if (!publication_erased)
    {
      result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_ERASING;
      result->mutation_attempted = true;
      ret = callback_status(ops->erase_sector(
        ops->arg, bank_address(publication_bank)));
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      result->bank_reclaimed = true;
      ret = callback_status(ops->read(
        ops->arg, bank_address(publication_bank), proposed,
        BK7258_BOOT_OTA_ROTATION_BANK_SIZE));
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      if (!bytes_value(proposed, BK7258_BOOT_OTA_ROTATION_BANK_SIZE, 0xffu))
        {
          return finish(ops, result, -EIO);
        }
    }

  result->erase_verified = true;
  result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_ERASE_VERIFIED;
  for (chunk = 0; chunk < BK7258_BOOT_OTA_PROGRAM_CHUNKS; chunk++)
    {
      uint32_t offset = chunk * BK7258_BOOT_OTA_PROGRAM_GRANULE;
      uint32_t address = bank_address(publication_bank) + offset;

      ret = authority_status(ops, started, timeout_ms, stable_slot);
      if (ret < 0)
        {
          return finish(ops, result, ret);
        }

      result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_PROGRAMMING;
      result->mutation_attempted = true;
      ret = callback_status(ops->write(
        ops->arg, address, pending_record + offset,
        BK7258_BOOT_OTA_PROGRAM_GRANULE));
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      result->programmed_chunks = chunk + 1u;
      ret = callback_status(ops->read(
        ops->arg, address, proposed + offset,
        BK7258_BOOT_OTA_PROGRAM_GRANULE));
      if (ret != 0 ||
          !bytes_equal(proposed + offset, pending_record + offset,
                       BK7258_BOOT_OTA_PROGRAM_GRANULE))
        {
          return finish(ops, result, ret != 0 ? ret : -EIO);
        }

      result->verified_chunks = chunk + 1u;
      result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_CHUNK_VERIFIED;
    }

  result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_FINAL_READBACK;
  ret = callback_status(ops->read(
    ops->arg, bank_address(publication_bank), proposed,
    BK7258_BOOT_OTA_ROTATION_BANK_SIZE));
  if (ret != 0 || !canonical_bank(proposed, pending_record))
    {
      return finish(ops, result, ret != 0 ? ret : -EIO);
    }

  ret = bk7258_boot_ota_rotation_inspect(proposed,
                                          &banks[publication_bank]);
  if (ret < 0)
    {
      return finish(ops, result, ret);
    }

  ret = bk7258_boot_ota_rotation_select(banks, &view);
  if (ret < 0 || !view.metadata_present ||
      view.selected_bank != publication_bank ||
      view.generation != expected_generation ||
      view.state != proposed_info.state || !view.trial_required)
    {
      return finish(ops, result, ret < 0 ? ret : -EBADMSG);
    }

  ret = authority_status(ops, started, timeout_ms, stable_slot);
  if (ret < 0)
    {
      return finish(ops, result, ret);
    }

  result->readback_verified = true;
  result->phase = BK7258_BOOT_OTA_ROTATION_PUBLISH_COMMITTED;
  return finish(ops, result, 0);
}
