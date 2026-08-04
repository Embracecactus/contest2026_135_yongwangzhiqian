/*
 * boot_ota_publish_core.c - portable N15-E pending publication controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * No NuttX, SDK, MMIO, heap or libc dependency is permitted here.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_publish_core.h"
#include "boot_ota_trial_core.h"

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

static bool bytes_erased(const uint8_t *data, size_t len)
{
  while (len-- != 0)
    {
      if (*data++ != 0xffu)
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

static bool timed_out(const struct bk7258_boot_ota_publish_ops_s *ops,
                      uint64_t started, uint32_t timeout_ms)
{
  return ops->now_ms(ops->arg) - started >= (uint64_t)timeout_ms;
}

static int authority_status(
  const struct bk7258_boot_ota_publish_ops_s *ops, uint64_t started,
  uint32_t timeout_ms)
{
  if (timed_out(ops, started, timeout_ms))
    {
      return -ETIMEDOUT;
    }

  if (!ops->compile_write_enabled(ops->arg) ||
      !ops->runtime_write_enabled(ops->arg))
    {
      return -EACCES;
    }

  return ops->primary_mapping_active(ops->arg) ? 0 : -EPERM;
}

static int finish(
  const struct bk7258_boot_ota_publish_ops_s *ops,
  struct bk7258_boot_ota_publish_result_s *result, int status)
{
  if (result->lock_acquired && !result->unlock_completed)
    {
      ops->unlock(ops->arg);
      result->unlock_completed = true;
    }

  result->status = status;
  return status;
}

static bool reclaimable_state(
  enum bk7258_boot_ota_metadata_state_e state)
{
  return state == BK7258_BOOT_OTA_META_TRIAL_STARTED ||
         state == BK7258_BOOT_OTA_META_ROLLBACK_A;
}

int bk7258_boot_ota_publish_pending(
  const uint8_t pending_record[BK7258_BOOT_OTA_RECORD_SIZE],
  uint64_t expected_generation,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct bk7258_boot_ota_publish_ops_s *ops, uint32_t timeout_ms,
  uint8_t *workspace, size_t workspace_size,
  struct bk7258_boot_ota_publish_result_s *result)
{
  struct bk7258_boot_ota_metadata_info_s previous_info;
  struct bk7258_boot_ota_metadata_info_s proposed_info;
  struct bk7258_boot_ota_result_s select_result;
  uint8_t *current_metadata;
  uint8_t *proposed_metadata;
  uint8_t *selector_scratch;
  uint64_t started;
  uint32_t chunk;
  int inspect_ret;
  int ret;

  if (result == NULL)
    {
      return -EINVAL;
    }

  result->status = -EINPROGRESS;
  result->phase = BK7258_BOOT_OTA_PUBLISH_IDLE;
  result->previous_state = BK7258_BOOT_OTA_META_ERASED;
  result->previous_records = 0;
  result->programmed_chunks = 0;
  result->verified_chunks = 0;
  result->previous_generation = 0;
  result->generation = expected_generation;
  result->lock_acquired = false;
  result->unlock_completed = false;
  result->previous_metadata_valid = false;
  result->previous_metadata_trusted = false;
  result->candidate_verified = false;
  result->mutation_attempted = false;
  result->sector_reclaimed = false;
  result->erase_verified = false;
  result->readback_verified = false;
  result->idempotent = false;

  if (pending_record == NULL || expected_generation == 0 || raw_ops == NULL ||
      raw_ops->read == NULL || hash_ops == NULL || ops == NULL ||
      timeout_ms == 0 || workspace == NULL ||
      workspace_size < BK7258_BOOT_OTA_PUBLISH_WORKSPACE_SIZE ||
      ops->now_ms == NULL || ops->compile_write_enabled == NULL ||
      ops->runtime_write_enabled == NULL ||
      ops->primary_mapping_active == NULL || ops->lock == NULL ||
      ops->unlock == NULL || ops->read == NULL ||
      ops->erase_sector == NULL || ops->write == NULL)
    {
      result->status = -EINVAL;
      return -EINVAL;
    }

  result->phase = BK7258_BOOT_OTA_PUBLISH_GATE_CHECK;
  if (!ops->compile_write_enabled(ops->arg) ||
      !ops->runtime_write_enabled(ops->arg))
    {
      result->status = -EACCES;
      return -EACCES;
    }

  if (!ops->primary_mapping_active(ops->arg))
    {
      result->status = -EPERM;
      return -EPERM;
    }

  started = ops->now_ms(ops->arg);
  ret = callback_status(ops->lock(ops->arg, timeout_ms));
  if (ret != 0)
    {
      result->status = ret;
      return ret;
    }

  result->lock_acquired = true;
  result->phase = BK7258_BOOT_OTA_PUBLISH_LOCKED;
  ret = authority_status(ops, started, timeout_ms);
  if (ret != 0)
    {
      return finish(ops, result, ret);
    }

  current_metadata = workspace;
  proposed_metadata = current_metadata + BK7258_BOOT_OTA_METADATA_SIZE;
  selector_scratch = proposed_metadata + BK7258_BOOT_OTA_METADATA_SIZE;

  ret = callback_status(ops->read(ops->arg,
                                  BK7258_BOOT_OTA_METADATA_START,
                                  current_metadata,
                                  BK7258_BOOT_OTA_METADATA_SIZE));
  if (ret != 0)
    {
      return finish(ops, result, ret);
    }

  if (timed_out(ops, started, timeout_ms))
    {
      return finish(ops, result, -ETIMEDOUT);
    }

  result->phase = BK7258_BOOT_OTA_PUBLISH_CURRENT_READ;
  bytes_fill(proposed_metadata, 0xffu, BK7258_BOOT_OTA_METADATA_SIZE);
  bytes_copy(proposed_metadata, pending_record,
             BK7258_BOOT_OTA_RECORD_SIZE);

  ret = bk7258_boot_ota_metadata_inspect(proposed_metadata,
                                         &proposed_info);
  if (ret != 0 || proposed_info.erased || !proposed_info.trusted ||
      proposed_info.state != BK7258_BOOT_OTA_META_PENDING_B ||
      proposed_info.valid_records != 1u || proposed_info.sequence != 1u ||
      proposed_info.generation != expected_generation)
    {
      return finish(ops, result, ret != 0 ? ret : -EBADMSG);
    }

  ret = bk7258_boot_ota_select_core(
    proposed_metadata, raw_ops, hash_ops, selector_scratch,
    BK7258_OTA_STAGE_SCRATCH_SIZE, &select_result);
  if (ret != 0 || select_result.status != 0 ||
      select_result.decision !=
        BK7258_BOOT_OTA_DECISION_B_TRIAL_CANDIDATE ||
      select_result.generation != expected_generation ||
      !select_result.metadata_valid ||
      !select_result.primary_full_verified ||
      !select_result.secondary_verified)
    {
      return finish(ops, result,
                    ret != 0 ? ret : -EBADMSG);
    }

  result->candidate_verified = true;
  result->phase = BK7258_BOOT_OTA_PUBLISH_CANDIDATE_VERIFIED;
  inspect_ret = bk7258_boot_ota_metadata_inspect(current_metadata,
                                                 &previous_info);
  if (inspect_ret == 0)
    {
      result->previous_metadata_valid = true;
      result->previous_metadata_trusted = previous_info.trusted;
      result->previous_state = previous_info.state;
      result->previous_records = previous_info.valid_records;
      result->previous_generation = previous_info.generation;

      if (bytes_equal(current_metadata, proposed_metadata,
                      BK7258_BOOT_OTA_METADATA_SIZE))
        {
          result->idempotent = true;
          result->readback_verified = true;
          result->phase = BK7258_BOOT_OTA_PUBLISH_COMMITTED;
          return finish(ops, result, 0);
        }

      if (previous_info.trusted &&
          previous_info.state == BK7258_BOOT_OTA_META_PENDING_B)
        {
          return finish(ops, result, -EALREADY);
        }

      if (previous_info.trusted &&
          previous_info.state == BK7258_BOOT_OTA_META_CONFIRMED_B)
        {
          return finish(ops, result, -EBUSY);
        }

      if (previous_info.trusted &&
          (!reclaimable_state(previous_info.state) ||
           expected_generation <= previous_info.generation))
        {
          return finish(ops, result, -ESTALE);
        }
    }

  if (!bytes_erased(current_metadata, BK7258_BOOT_OTA_METADATA_SIZE))
    {
      ret = authority_status(ops, started, timeout_ms);
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      result->phase = BK7258_BOOT_OTA_PUBLISH_ERASING;
      result->mutation_attempted = true;
      ret = callback_status(ops->erase_sector(
        ops->arg, BK7258_BOOT_OTA_METADATA_START));
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      result->sector_reclaimed = true;
      ret = callback_status(ops->read(ops->arg,
                                      BK7258_BOOT_OTA_METADATA_START,
                                      current_metadata,
                                      BK7258_BOOT_OTA_METADATA_SIZE));
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      if (!bytes_erased(current_metadata,
                        BK7258_BOOT_OTA_METADATA_SIZE))
        {
          return finish(ops, result, -EIO);
        }

      result->erase_verified = true;
      result->phase = BK7258_BOOT_OTA_PUBLISH_ERASE_VERIFIED;
    }
  else
    {
      result->erase_verified = true;
    }

  ret = authority_status(ops, started, timeout_ms);
  if (ret != 0)
    {
      return finish(ops, result, ret);
    }

  for (chunk = 0; chunk < BK7258_BOOT_OTA_PROGRAM_CHUNKS; chunk++)
    {
      uint32_t offset = chunk * BK7258_BOOT_OTA_PROGRAM_GRANULE;
      uint32_t address = BK7258_BOOT_OTA_METADATA_START + offset;

      ret = authority_status(ops, started, timeout_ms);
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      result->phase = BK7258_BOOT_OTA_PUBLISH_PROGRAMMING;
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
        ops->arg, address, current_metadata + offset,
        BK7258_BOOT_OTA_PROGRAM_GRANULE));
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }

      if (!bytes_equal(pending_record + offset,
                       current_metadata + offset,
                       BK7258_BOOT_OTA_PROGRAM_GRANULE))
        {
          return finish(ops, result, -EIO);
        }

      result->verified_chunks = chunk + 1u;
      result->phase = BK7258_BOOT_OTA_PUBLISH_CHUNK_VERIFIED;
      ret = authority_status(ops, started, timeout_ms);
      if (ret != 0)
        {
          return finish(ops, result, ret);
        }
    }

  result->phase = BK7258_BOOT_OTA_PUBLISH_FINAL_READBACK;
  ret = callback_status(ops->read(ops->arg,
                                  BK7258_BOOT_OTA_METADATA_START,
                                  current_metadata,
                                  BK7258_BOOT_OTA_METADATA_SIZE));
  if (ret != 0)
    {
      return finish(ops, result, ret);
    }

  if (!bytes_equal(current_metadata, proposed_metadata,
                   BK7258_BOOT_OTA_METADATA_SIZE))
    {
      return finish(ops, result, -EIO);
    }

  ret = bk7258_boot_ota_select_core(
    current_metadata, raw_ops, hash_ops, selector_scratch,
    BK7258_OTA_STAGE_SCRATCH_SIZE, &select_result);
  if (ret != 0 || select_result.status != 0 ||
      select_result.decision !=
        BK7258_BOOT_OTA_DECISION_B_TRIAL_CANDIDATE ||
      select_result.generation != expected_generation ||
      !select_result.metadata_valid ||
      !select_result.primary_full_verified ||
      !select_result.secondary_verified)
    {
      return finish(ops, result,
                    ret != 0 ? ret : -EBADMSG);
    }

  ret = authority_status(ops, started, timeout_ms);
  if (ret != 0)
    {
      return finish(ops, result, ret);
    }

  result->readback_verified = true;
  result->phase = BK7258_BOOT_OTA_PUBLISH_COMMITTED;
  return finish(ops, result, 0);
}
