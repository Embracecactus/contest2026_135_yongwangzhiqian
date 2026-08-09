/*
 * boot_ota_rotation_control_core.c - selected-bank format-2 transition.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable code only: no NuttX, SDK, MMIO, heap or libc dependency.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_rotation_control_core.h"
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

static uint32_t bank_address(uint32_t bank)
{
  return bank == 0 ? BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET :
                     BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET;
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
  struct bk7258_boot_ota_rotation_control_result_s *result,
  uint64_t generation,
  enum bk7258_boot_ota_rotation_state_e expected_state,
  enum bk7258_boot_ota_rotation_state_e next_state)
{
  result->status = -EINPROGRESS;
  result->phase = BK7258_BOOT_OTA_TRIAL_IDLE;
  result->previous_state = expected_state;
  result->next_state = next_state;
  result->base_slot = BK7258_BOOT_OTA_SLOT_A;
  result->target_slot = BK7258_BOOT_OTA_SLOT_B;
  result->selected_bank = BK7258_BOOT_OTA_ROTATION_NO_BANK;
  result->bank_address = 0;
  result->record_index = 0;
  result->record_offset = 0;
  result->programmed_chunks = 0;
  result->verified_chunks = 0;
  result->sequence = 0;
  result->generation = generation;
  result->lock_acquired = false;
  result->unlock_completed = false;
  result->metadata_degraded = false;
  result->mutation_attempted = false;
  result->readback_verified = false;
  result->current_boot_trial = false;
}

static int finish(
  const struct bk7258_boot_ota_trial_ops_s *ops,
  struct bk7258_boot_ota_rotation_control_result_s *result, int status)
{
  if (result->lock_acquired && !result->unlock_completed)
    {
      ops->unlock(ops->arg);
      result->unlock_completed = true;
    }

  result->status = status;
  return status;
}

int bk7258_boot_ota_rotation_control_transition(
  uint64_t expected_generation,
  enum bk7258_boot_ota_rotation_state_e expected_state,
  enum bk7258_boot_ota_rotation_state_e next_state,
  const struct bk7258_boot_ota_trial_ops_s *ops, uint32_t timeout_ms,
  uint8_t *workspace, size_t workspace_size,
  struct bk7258_boot_ota_rotation_control_result_s *result)
{
  struct bk7258_boot_ota_rotation_bank_s banks[2];
  struct bk7258_boot_ota_rotation_view_s view;
  struct bk7258_boot_ota_rotation_transition_s transition;
  uint8_t *bank_data[2];
  uint8_t *record;
  uint8_t *readback;
  uint32_t selected_address;
  uint32_t address;
  uint32_t chunk;
  uint32_t index;
  int ret;

  if (result == NULL)
    {
      return -EINVAL;
    }

  initialize_result(result, expected_generation, expected_state, next_state);
  if (expected_generation == 0 || ops == NULL || timeout_ms == 0 ||
      workspace == NULL ||
      workspace_size < BK7258_BOOT_OTA_ROTATION_CONTROL_WORKSPACE_SIZE ||
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
  bank_data[0] = workspace;
  bank_data[1] = bank_data[0] + BK7258_BOOT_OTA_ROTATION_BANK_SIZE;
  record = bank_data[1] + BK7258_BOOT_OTA_ROTATION_BANK_SIZE;
  readback = record + BK7258_BOOT_OTA_ROTATION_RECORD_SIZE;

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

  result->phase = BK7258_BOOT_OTA_TRIAL_METADATA_READ;
  ret = bk7258_boot_ota_rotation_select(banks, &view);
  if (ret < 0 || !view.metadata_present ||
      view.selected_bank == BK7258_BOOT_OTA_ROTATION_NO_BANK)
    {
      return finish(ops, result, ret < 0 ? ret : -ENOENT);
    }

  result->selected_bank = view.selected_bank;
  selected_address = bank_address(view.selected_bank);
  result->bank_address = selected_address;
  ret = bk7258_boot_ota_rotation_prepare_transition(
    bank_data[view.selected_bank], expected_generation, expected_state,
    next_state, record, &transition);
  if (ret < 0)
    {
      return finish(ops, result, ret);
    }

  result->base_slot = transition.base_slot;
  result->target_slot = transition.target_slot;
  result->record_index = transition.record_index;
  result->record_offset = transition.record_offset;
  result->sequence = transition.sequence;
  result->phase = BK7258_BOOT_OTA_TRIAL_PREPARED;
  address = selected_address + transition.record_offset;

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
      if (ret != 0 ||
          !bytes_equal(record + offset, readback + offset,
                       BK7258_BOOT_OTA_PROGRAM_GRANULE))
        {
          return finish(ops, result, ret != 0 ? ret : -EIO);
        }

      result->verified_chunks = chunk + 1u;
      result->phase = BK7258_BOOT_OTA_TRIAL_CHUNK_VERIFIED;
    }

  result->phase = BK7258_BOOT_OTA_TRIAL_FINAL_READBACK;
  ret = callback_status(ops->read(
    ops->arg, address, readback, BK7258_BOOT_OTA_ROTATION_RECORD_SIZE));
  if (ret != 0 ||
      !bytes_equal(record, readback,
                   BK7258_BOOT_OTA_ROTATION_RECORD_SIZE))
    {
      return finish(ops, result, ret != 0 ? ret : -EIO);
    }

  bytes_copy(bank_data[view.selected_bank] + transition.record_offset,
             readback, BK7258_BOOT_OTA_ROTATION_RECORD_SIZE);
  ret = bk7258_boot_ota_rotation_inspect(bank_data[view.selected_bank],
                                          &banks[view.selected_bank]);
  if (ret < 0 || !banks[view.selected_bank].trusted ||
      banks[view.selected_bank].state != next_state ||
      banks[view.selected_bank].valid_records !=
        transition.record_index + 1u ||
      banks[view.selected_bank].sequence != transition.sequence ||
      banks[view.selected_bank].generation != expected_generation)
    {
      return finish(ops, result, ret < 0 ? ret : -EBADMSG);
    }

  ret = bk7258_boot_ota_rotation_select(banks, &view);
  if (ret < 0 || !view.metadata_present ||
      view.selected_bank != result->selected_bank ||
      view.state != next_state || view.sequence != transition.sequence ||
      view.generation != expected_generation)
    {
      return finish(ops, result, ret < 0 ? ret : -EBADMSG);
    }

  result->readback_verified = true;
  result->current_boot_trial = transition.current_boot_trial;
  result->phase = BK7258_BOOT_OTA_TRIAL_COMMITTED;
  return finish(ops, result, 0);
}
