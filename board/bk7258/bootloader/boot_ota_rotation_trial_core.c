/*
 * boot_ota_rotation_trial_core.c - format-2 append/read-back controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_rotation_trial_core.h"
#include "../chip/include/bk7258_partition_layout.h"

static void bytes_copy(uint8_t *destination, const uint8_t *source,
                       size_t len)
{
  while (len-- != 0)
    {
      *destination++ = *source++;
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

static int callback_status(int ret)
{
  return ret > 0 ? -EIO : ret;
}

static bool valid_bank_address(uint32_t address)
{
  return address == BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET ||
         address == BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET;
}

static int finish(
  const struct bk7258_boot_ota_trial_ops_s *ops,
  struct bk7258_boot_ota_rotation_trial_result_s *result, int status)
{
  if (result->lock_acquired && !result->unlock_completed)
    {
      ops->unlock(ops->arg);
      result->unlock_completed = true;
    }

  result->status = status;
  return status;
}

int bk7258_boot_ota_rotation_trial_transition(
  uint32_t bank_address, uint64_t expected_generation,
  enum bk7258_boot_ota_rotation_state_e expected_state,
  enum bk7258_boot_ota_rotation_state_e next_state,
  const struct bk7258_boot_ota_trial_ops_s *ops, uint32_t timeout_ms,
  uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  uint8_t scratch[BK7258_BOOT_OTA_TRIAL_SCRATCH_SIZE],
  struct bk7258_boot_ota_rotation_trial_result_s *result)
{
  struct bk7258_boot_ota_rotation_bank_s info;
  struct bk7258_boot_ota_rotation_transition_s transition;
  uint8_t *record;
  uint8_t *readback;
  uint32_t chunk;
  uint32_t address;
  int ret;

  if (result == NULL)
    {
      return -EINVAL;
    }

  result->status = -EINPROGRESS;
  result->phase = BK7258_BOOT_OTA_TRIAL_IDLE;
  result->previous_state = expected_state;
  result->next_state = next_state;
  result->base_slot = BK7258_BOOT_OTA_SLOT_A;
  result->target_slot = BK7258_BOOT_OTA_SLOT_B;
  result->bank_address = bank_address;
  result->record_index = 0;
  result->record_offset = 0;
  result->programmed_chunks = 0;
  result->verified_chunks = 0;
  result->sequence = 0;
  result->generation = expected_generation;
  result->lock_acquired = false;
  result->unlock_completed = false;
  result->mutation_attempted = false;
  result->readback_verified = false;
  result->current_boot_trial = false;

  if (!valid_bank_address(bank_address) || expected_generation == 0 ||
      ops == NULL || bank == NULL || scratch == NULL ||
      ops->compile_write_enabled == NULL ||
      ops->runtime_write_enabled == NULL || ops->lock == NULL ||
      ops->unlock == NULL || ops->read == NULL || ops->write == NULL)
    {
      result->status = -EINVAL;
      return -EINVAL;
    }

  result->phase = BK7258_BOOT_OTA_TRIAL_GATE_CHECK;
  if (!ops->compile_write_enabled(ops->arg) ||
      !ops->runtime_write_enabled(ops->arg))
    {
      result->status = -EACCES;
      return -EACCES;
    }

  ret = callback_status(ops->lock(ops->arg, timeout_ms));
  if (ret != 0)
    {
      result->status = ret;
      return ret;
    }

  result->lock_acquired = true;
  result->phase = BK7258_BOOT_OTA_TRIAL_LOCKED;
  ret = callback_status(ops->read(ops->arg, bank_address, bank,
                                  BK7258_BOOT_OTA_ROTATION_BANK_SIZE));
  if (ret != 0)
    {
      return finish(ops, result, ret);
    }

  result->phase = BK7258_BOOT_OTA_TRIAL_METADATA_READ;
  record = scratch;
  readback = scratch + BK7258_BOOT_OTA_ROTATION_RECORD_SIZE;
  ret = bk7258_boot_ota_rotation_prepare_transition(
    bank, expected_generation, expected_state, next_state, record,
    &transition);
  if (ret != 0)
    {
      return finish(ops, result, ret);
    }

  result->base_slot = transition.base_slot;
  result->target_slot = transition.target_slot;
  result->record_index = transition.record_index;
  result->record_offset = transition.record_offset;
  result->sequence = transition.sequence;
  result->generation = transition.generation;
  result->phase = BK7258_BOOT_OTA_TRIAL_PREPARED;
  address = bank_address + transition.record_offset;

  for (chunk = 0; chunk < BK7258_BOOT_OTA_PROGRAM_CHUNKS; chunk++)
    {
      uint32_t offset = chunk * BK7258_BOOT_OTA_PROGRAM_GRANULE;

      result->phase = BK7258_BOOT_OTA_TRIAL_PROGRAMMING;
      result->mutation_attempted = true;
      ret = callback_status(ops->write(
        ops->arg, address + offset, record + offset,
        BK7258_BOOT_OTA_PROGRAM_GRANULE));
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      result->programmed_chunks = chunk + 1u;
      ret = callback_status(ops->read(
        ops->arg, address + offset, readback + offset,
        BK7258_BOOT_OTA_PROGRAM_GRANULE));
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      if (!bytes_equal(record + offset, readback + offset,
                       BK7258_BOOT_OTA_PROGRAM_GRANULE))
        {
          return finish(ops, result, -EIO);
        }

      result->verified_chunks = chunk + 1u;
      result->phase = BK7258_BOOT_OTA_TRIAL_CHUNK_VERIFIED;
    }

  result->phase = BK7258_BOOT_OTA_TRIAL_FINAL_READBACK;
  ret = callback_status(ops->read(ops->arg, address, readback,
                                  BK7258_BOOT_OTA_ROTATION_RECORD_SIZE));
  if (ret != 0)
    {
      return finish(ops, result, ret);
    }

  if (!bytes_equal(record, readback,
                   BK7258_BOOT_OTA_ROTATION_RECORD_SIZE))
    {
      return finish(ops, result, -EIO);
    }

  bytes_copy(bank + transition.record_offset, readback,
             BK7258_BOOT_OTA_ROTATION_RECORD_SIZE);
  ret = bk7258_boot_ota_rotation_inspect(bank, &info);
  if (ret != 0 || info.erased || !info.trusted ||
      info.state != next_state ||
      info.valid_records != transition.record_index + 1u ||
      info.sequence != transition.sequence ||
      info.generation != expected_generation)
    {
      return finish(ops, result, ret != 0 ? ret : -EBADMSG);
    }

  result->readback_verified = true;
  result->current_boot_trial = transition.current_boot_trial;
  result->phase = BK7258_BOOT_OTA_TRIAL_COMMITTED;
  return finish(ops, result, 0);
}
