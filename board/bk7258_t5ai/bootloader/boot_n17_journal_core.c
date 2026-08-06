/*
 * boot_n17_journal_core.c - portable N17 format-3 reader and selector.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * No NuttX, SDK, MMIO, heap or libc dependency.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_n17_journal_core.h"
#include "../chip/include/bk7258_partition_layout.h"

#define RECORD_VERSION_OFFSET       0x008u
#define RECORD_SIZE_OFFSET          0x00au
#define RECORD_PHASE_OFFSET         0x00cu
#define RECORD_STABLE_SLOT_OFFSET   0x00du
#define RECORD_TARGET_SLOT_OFFSET   0x00eu
#define RECORD_OUTCOME_OFFSET       0x00fu
#define RECORD_FLAGS_OFFSET         0x010u
#define RECORD_DIGEST_ALG_OFFSET    0x014u
#define RECORD_RESERVED16_OFFSET    0x016u
#define RECORD_SEQUENCE_OFFSET      0x018u
#define RECORD_GENERATION_OFFSET    0x020u
#define RECORD_COUNTER_OFFSET       0x028u
#define RECORD_TIMESTAMP_OFFSET     0x030u
#define RECORD_RESERVED32_OFFSET    0x034u
#define RECORD_MANIFEST_A_OFFSET    0x038u
#define RECORD_MANIFEST_B_OFFSET    0x058u
#define RECORD_PREVIOUS_OFFSET      0x078u
#define RECORD_RESERVED_OFFSET      0x098u
#define RECORD_COMMIT_OFFSET        0x0f8u
#define RECORD_CRC_OFFSET           0x0fcu

static const uint8_t g_record_magic[8] =
  {'B', 'K', 'O', 'T', 'A', '1', '7', 'J'};
static const uint8_t g_commit_magic[4] = {'C', 'M', 'T', '3'};
static const uint8_t g_policy_magic[8] =
  {'B', 'K', 'O', 'T', 'A', '1', '7', 'A'};
static const uint8_t g_layout_sha256[32] =
  BK7258_PARTITION_LAYOUT_SHA256_BYTES;

static uint16_t getle16(const uint8_t *value)
{
  return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t getle32(const uint8_t *value)
{
  return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
         ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static uint64_t getle64(const uint8_t *value)
{
  return (uint64_t)getle32(value) | ((uint64_t)getle32(value + 4) << 32);
}

static void putle16(uint8_t *value, uint16_t data)
{
  value[0] = (uint8_t)data;
  value[1] = (uint8_t)(data >> 8);
}

static void putle32(uint8_t *value, uint32_t data)
{
  value[0] = (uint8_t)data;
  value[1] = (uint8_t)(data >> 8);
  value[2] = (uint8_t)(data >> 16);
  value[3] = (uint8_t)(data >> 24);
}

static void putle64(uint8_t *value, uint64_t data)
{
  putle32(value, (uint32_t)data);
  putle32(value + 4, (uint32_t)(data >> 32));
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

static void bytes_copy(uint8_t *destination, const uint8_t *source,
                       size_t len)
{
  while (len-- != 0)
    {
      *destination++ = *source++;
    }
}

static void bytes_clear(void *data, size_t len)
{
  uint8_t *bytes = data;

  while (len-- != 0)
    {
      *bytes++ = 0;
    }
}

static uint32_t crc32_bytes(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xffffffffu;

  while (len-- != 0)
    {
      uint32_t bit;

      crc ^= *data++;
      for (bit = 0; bit < 8; bit++)
        {
          uint32_t mask = 0u - (crc & 1u);
          crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }

  return crc ^ 0xffffffffu;
}

static bool digest_set(const uint8_t *digest)
{
  return !bytes_value(digest, 32, 0) && !bytes_value(digest, 32, 0xffu);
}

static const uint8_t *manifest_digest(
  const struct bk7258_boot_n17_record_s *record, uint8_t slot)
{
  return slot == 0 ? record->slot_a_manifest_sha256 :
                     record->slot_b_manifest_sha256;
}

static int record_shape(const struct bk7258_boot_n17_record_s *record)
{
  if (record->stable_slot > 1 || record->generation == 0 ||
      record->sequence == 0 || record->accepted_security_counter == 0 ||
      !digest_set(record->slot_a_manifest_sha256) ||
      !digest_set(record->slot_b_manifest_sha256))
    {
      return -EBADMSG;
    }

  if (record->sequence == 1)
    {
      if (!bytes_value(record->previous_record_sha256, 32, 0))
        {
          return -EBADMSG;
        }
    }
  else if (!digest_set(record->previous_record_sha256))
    {
      return -EBADMSG;
    }

  if (record->phase == BK7258_BOOT_N17_STABLE)
    {
      if (record->target_slot != 0xffu)
        {
          return -EBADMSG;
        }

      if ((record->outcome == BK7258_BOOT_N17_FACTORY ||
           record->outcome == BK7258_BOOT_N17_MIGRATED) &&
          record->sequence == 1)
        {
          return 0;
        }

      if (record->outcome == BK7258_BOOT_N17_CONFIRMED &&
          record->sequence == 3)
        {
          return 0;
        }

      if (record->outcome == BK7258_BOOT_N17_ROLLED_BACK &&
          (record->sequence == 2 || record->sequence == 3))
        {
          return 0;
        }

      return -EBADMSG;
    }

  if ((record->phase != BK7258_BOOT_N17_PENDING &&
       record->phase != BK7258_BOOT_N17_TRIAL) ||
      record->target_slot > 1 ||
      record->target_slot == record->stable_slot ||
      record->outcome != BK7258_BOOT_N17_NONE)
    {
      return -EBADMSG;
    }

  if ((record->phase == BK7258_BOOT_N17_PENDING &&
       record->sequence != 1) ||
      (record->phase == BK7258_BOOT_N17_TRIAL &&
       record->sequence != 2))
    {
      return -EBADMSG;
    }

  return 0;
}

int bk7258_boot_n17_record_parse(
  const uint8_t record[BK7258_BOOT_N17_RECORD_SIZE],
  struct bk7258_boot_n17_record_s *parsed)
{
  if (record == NULL || parsed == NULL)
    {
      return -EINVAL;
    }

  bytes_clear(parsed, sizeof(*parsed));
  if (!bytes_equal(record, g_record_magic, 8) ||
      getle16(record + RECORD_VERSION_OFFSET) != 3 ||
      getle16(record + RECORD_SIZE_OFFSET) != BK7258_BOOT_N17_RECORD_SIZE ||
      getle32(record + RECORD_FLAGS_OFFSET) != 0 ||
      getle16(record + RECORD_DIGEST_ALG_OFFSET) != 1 ||
      getle16(record + RECORD_RESERVED16_OFFSET) != 0 ||
      getle32(record + RECORD_RESERVED32_OFFSET) != 0 ||
      !bytes_value(record + RECORD_RESERVED_OFFSET, 96, 0) ||
      !bytes_equal(record + RECORD_COMMIT_OFFSET, g_commit_magic, 4) ||
      crc32_bytes(record, RECORD_CRC_OFFSET) !=
        getle32(record + RECORD_CRC_OFFSET))
    {
      return -EBADMSG;
    }

  parsed->phase =
    (enum bk7258_boot_n17_phase_e)record[RECORD_PHASE_OFFSET];
  parsed->stable_slot = record[RECORD_STABLE_SLOT_OFFSET];
  parsed->target_slot = record[RECORD_TARGET_SLOT_OFFSET];
  parsed->outcome =
    (enum bk7258_boot_n17_outcome_e)record[RECORD_OUTCOME_OFFSET];
  parsed->sequence = getle64(record + RECORD_SEQUENCE_OFFSET);
  parsed->generation = getle64(record + RECORD_GENERATION_OFFSET);
  parsed->accepted_security_counter = getle64(record + RECORD_COUNTER_OFFSET);
  parsed->created_timestamp = getle32(record + RECORD_TIMESTAMP_OFFSET);
  bytes_copy(parsed->slot_a_manifest_sha256,
             record + RECORD_MANIFEST_A_OFFSET, 32);
  bytes_copy(parsed->slot_b_manifest_sha256,
             record + RECORD_MANIFEST_B_OFFSET, 32);
  bytes_copy(parsed->previous_record_sha256,
             record + RECORD_PREVIOUS_OFFSET, 32);
  return record_shape(parsed);
}

int bk7258_boot_n17_record_encode(
  const struct bk7258_boot_n17_record_s *record,
  uint8_t encoded[BK7258_BOOT_N17_RECORD_SIZE])
{
  if (record == NULL || encoded == NULL || record_shape(record) < 0)
    {
      return -EINVAL;
    }

  bytes_clear(encoded, BK7258_BOOT_N17_RECORD_SIZE);
  bytes_copy(encoded, g_record_magic, sizeof(g_record_magic));
  putle16(encoded + RECORD_VERSION_OFFSET, 3u);
  putle16(encoded + RECORD_SIZE_OFFSET, BK7258_BOOT_N17_RECORD_SIZE);
  encoded[RECORD_PHASE_OFFSET] = (uint8_t)record->phase;
  encoded[RECORD_STABLE_SLOT_OFFSET] = record->stable_slot;
  encoded[RECORD_TARGET_SLOT_OFFSET] = record->target_slot;
  encoded[RECORD_OUTCOME_OFFSET] = (uint8_t)record->outcome;
  putle16(encoded + RECORD_DIGEST_ALG_OFFSET, 1u);
  putle64(encoded + RECORD_SEQUENCE_OFFSET, record->sequence);
  putle64(encoded + RECORD_GENERATION_OFFSET, record->generation);
  putle64(encoded + RECORD_COUNTER_OFFSET, record->accepted_security_counter);
  putle32(encoded + RECORD_TIMESTAMP_OFFSET, record->created_timestamp);
  bytes_copy(encoded + RECORD_MANIFEST_A_OFFSET,
             record->slot_a_manifest_sha256, 32);
  bytes_copy(encoded + RECORD_MANIFEST_B_OFFSET,
             record->slot_b_manifest_sha256, 32);
  bytes_copy(encoded + RECORD_PREVIOUS_OFFSET,
             record->previous_record_sha256, 32);
  bytes_copy(encoded + RECORD_COMMIT_OFFSET, g_commit_magic,
             sizeof(g_commit_magic));
  putle32(encoded + RECORD_CRC_OFFSET,
           crc32_bytes(encoded, RECORD_CRC_OFFSET));
  return 0;
}

static int transition_valid(
  const struct bk7258_boot_n17_record_s *previous,
  const struct bk7258_boot_n17_record_s *current,
  const uint8_t previous_sha256[32])
{
  if (current->sequence != previous->sequence + 1 ||
      current->generation != previous->generation ||
      current->created_timestamp != previous->created_timestamp ||
      !bytes_equal(current->slot_a_manifest_sha256,
                   previous->slot_a_manifest_sha256, 32) ||
      !bytes_equal(current->slot_b_manifest_sha256,
                   previous->slot_b_manifest_sha256, 32) ||
      !bytes_equal(current->previous_record_sha256, previous_sha256, 32))
    {
      return -EBADMSG;
    }

  if (previous->phase == BK7258_BOOT_N17_PENDING)
    {
      if (current->stable_slot != previous->stable_slot ||
          current->accepted_security_counter !=
            previous->accepted_security_counter)
        {
          return -EBADMSG;
        }

      if (current->phase == BK7258_BOOT_N17_TRIAL)
        {
          return current->target_slot == previous->target_slot ? 0 : -EBADMSG;
        }

      return current->phase == BK7258_BOOT_N17_STABLE &&
             current->outcome == BK7258_BOOT_N17_ROLLED_BACK ? 0 : -EBADMSG;
    }

  if (previous->phase == BK7258_BOOT_N17_TRIAL &&
      current->phase == BK7258_BOOT_N17_STABLE)
    {
      if (current->outcome == BK7258_BOOT_N17_CONFIRMED)
        {
          return current->stable_slot == previous->target_slot &&
                 current->accepted_security_counter >
                   previous->accepted_security_counter ? 0 : -EBADMSG;
        }

      if (current->outcome == BK7258_BOOT_N17_ROLLED_BACK)
        {
          return current->stable_slot == previous->stable_slot &&
                 current->accepted_security_counter ==
                   previous->accepted_security_counter ? 0 : -EBADMSG;
        }
    }

  return -EBADMSG;
}

int bk7258_boot_n17_bank_inspect(
  const uint8_t bank[BK7258_BOOT_N17_BANK_SIZE],
  const struct bk7258_boot_n17_journal_ops_s *ops,
  struct bk7258_boot_n17_bank_s *info)
{
  struct bk7258_boot_n17_record_s parsed;
  struct bk7258_boot_n17_record_s previous;
  uint8_t previous_digest[32];
  uint32_t index;
  int ret;

  if (bank == NULL || ops == NULL || ops->sha256 == NULL || info == NULL)
    {
      return -EINVAL;
    }

  bytes_clear(info, sizeof(*info));
  for (index = 0; index < BK7258_BOOT_N17_RECORD_COUNT; index++)
    {
      const uint8_t *raw = bank + index * BK7258_BOOT_N17_RECORD_SIZE;
      size_t later_size = BK7258_BOOT_N17_BANK_SIZE -
                          (index + 1u) * BK7258_BOOT_N17_RECORD_SIZE;

      if (bytes_value(raw, BK7258_BOOT_N17_RECORD_SIZE, 0xffu))
        {
          info->tail_slot = index;
          info->tail_dirty = !bytes_value(
            raw + BK7258_BOOT_N17_RECORD_SIZE, later_size, 0xffu);
          return 0;
        }

      ret = bk7258_boot_n17_record_parse(raw, &parsed);
      if (ret < 0 || (index == 0 && parsed.sequence != 1))
        {
          info->tail_slot = index;
          info->tail_dirty = true;
          return 0;
        }

      if (index != 0)
        {
          ret = transition_valid(&previous, &parsed, previous_digest);
          if (ret < 0)
            {
              info->tail_slot = index;
              info->tail_dirty = true;
              return 0;
            }
        }

      if (ops->sha256(ops->arg, raw, BK7258_BOOT_N17_RECORD_SIZE,
                      previous_digest) != 0)
        {
          return -EIO;
        }

      if (index == 0)
        {
          info->first = parsed;
        }

      info->last = parsed;
      info->valid_records++;
      info->eligible = true;
      previous = parsed;
    }

  info->tail_slot = BK7258_BOOT_N17_RECORD_COUNT;
  return 0;
}

static bool successor_valid(const struct bk7258_boot_n17_bank_s *older,
                            const struct bk7258_boot_n17_bank_s *newer)
{
  const struct bk7258_boot_n17_record_s *old = &older->last;
  const struct bk7258_boot_n17_record_s *first = &newer->first;

  if (!older->eligible || !newer->eligible ||
      first->generation <= old->generation ||
      old->phase != BK7258_BOOT_N17_STABLE)
    {
      return false;
    }

  if (first->phase == BK7258_BOOT_N17_PENDING)
    {
      return first->stable_slot == old->stable_slot &&
             first->accepted_security_counter ==
               old->accepted_security_counter &&
             bytes_equal(manifest_digest(first, first->stable_slot),
                         manifest_digest(old, old->stable_slot), 32);
    }

  return first->phase == BK7258_BOOT_N17_STABLE &&
         first->outcome == BK7258_BOOT_N17_MIGRATED &&
         old->outcome == BK7258_BOOT_N17_MIGRATED &&
         first->stable_slot == old->stable_slot &&
         first->accepted_security_counter == old->accepted_security_counter &&
         bytes_equal(first->slot_a_manifest_sha256,
                     old->slot_a_manifest_sha256, 32) &&
         bytes_equal(first->slot_b_manifest_sha256,
                     old->slot_b_manifest_sha256, 32);
}

int bk7258_boot_n17_bank_select(
  const uint8_t bank0[BK7258_BOOT_N17_BANK_SIZE],
  const uint8_t bank1[BK7258_BOOT_N17_BANK_SIZE],
  const struct bk7258_boot_n17_journal_ops_s *ops,
  struct bk7258_boot_n17_selection_s *selection)
{
  struct bk7258_boot_n17_bank_s info[2];
  uint32_t newer;
  uint32_t older;
  int ret;

  if (bank0 == NULL || bank1 == NULL || ops == NULL || selection == NULL)
    {
      return -EINVAL;
    }

  bytes_clear(selection, sizeof(*selection));
  selection->bank_index = BK7258_BOOT_N17_NO_BANK;
  ret = bk7258_boot_n17_bank_inspect(bank0, ops, &info[0]);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_boot_n17_bank_inspect(bank1, ops, &info[1]);
  if (ret < 0)
    {
      return ret;
    }

  if (!info[0].eligible && !info[1].eligible)
    {
      return 0;
    }

  if (!info[0].eligible || !info[1].eligible)
    {
      selection->bank_index = info[0].eligible ? 0 : 1;
      selection->bank = info[selection->bank_index];
      selection->present = true;
      return 0;
    }

  if (info[0].last.generation == info[1].last.generation)
    {
      return -EBADMSG;
    }

  newer = info[0].last.generation > info[1].last.generation ? 0 : 1;
  older = 1u - newer;
  selection->bank_index = successor_valid(&info[older], &info[newer]) ?
                          newer : older;
  selection->rejected_newer = selection->bank_index == older;
  selection->bank = info[selection->bank_index];
  selection->present = true;
  return 0;
}

static void build_policy_marker(uint8_t marker[32])
{
  uint32_t index;

  for (index = 0; index < 32; index++)
    {
      marker[index] = 0;
    }

  bytes_copy(marker, g_policy_magic, 8);
  putle16(marker + 8, 1);
  putle16(marker + 10, 32);
  putle32(marker + 12, 0);
  bytes_copy(marker + 16, g_layout_sha256, 12);
  putle32(marker + 28, crc32_bytes(marker, 28));
}

int bk7258_boot_n17_policy_classify(
  const uint8_t sector[BK7258_BOOT_N17_BANK_SIZE],
  enum bk7258_boot_n17_policy_e *policy)
{
  uint8_t marker[32];

  if (sector == NULL || policy == NULL)
    {
      return -EINVAL;
    }

  if (bytes_value(sector, BK7258_BOOT_N17_BANK_SIZE, 0xffu))
    {
      *policy = BK7258_BOOT_N17_POLICY_UNARMED;
      return 0;
    }

  build_policy_marker(marker);
  *policy = bytes_equal(sector, marker, 32) &&
            bytes_value(sector + 32, BK7258_BOOT_N17_BANK_SIZE - 32,
                        0xffu) ?
            BK7258_BOOT_N17_POLICY_ARMED_CANONICAL :
            BK7258_BOOT_N17_POLICY_ARMED_DEGRADED;
  return 0;
}

enum bk7258_boot_n17_format_e bk7258_boot_n17_select_format(
  enum bk7258_boot_n17_policy_e policy, bool format3_present,
  bool format2_valid)
{
  if (format3_present)
    {
      return BK7258_BOOT_N17_FORMAT3;
    }

  if (policy == BK7258_BOOT_N17_POLICY_UNARMED && format2_valid)
    {
      return BK7258_BOOT_N17_FORMAT2;
    }

  return BK7258_BOOT_N17_FORMAT_FAIL_CLOSED;
}

int bk7258_boot_n17_counter_validate(
  const struct bk7258_boot_n17_record_s *record,
  const uint64_t counters[2])
{
  uint64_t stable;

  if (record == NULL || counters == NULL || record_shape(record) < 0)
    {
      return -EINVAL;
    }

  stable = counters[record->stable_slot];
  if (stable == 0 || stable != record->accepted_security_counter)
    {
      return -ESTALE;
    }

  if (record->phase == BK7258_BOOT_N17_PENDING ||
      record->phase == BK7258_BOOT_N17_TRIAL)
    {
      return counters[record->target_slot] > stable ? 0 : -ESTALE;
    }

  if (record->outcome == BK7258_BOOT_N17_MIGRATED)
    {
      return counters[1u - record->stable_slot] == stable ? 0 : -ESTALE;
    }

  return 0;
}

static bool evidence_valid(
  const struct bk7258_boot_n17_record_s *record,
  const struct bk7258_boot_n17_slot_evidence_s evidence[2], uint8_t slot,
  bool strictly_newer)
{
  return evidence[slot].valid &&
         bytes_equal(evidence[slot].manifest_sha256,
                     manifest_digest(record, slot), 32) &&
         (strictly_newer ?
          evidence[slot].security_counter > record->accepted_security_counter :
          evidence[slot].security_counter >= record->accepted_security_counter);
}

int bk7258_boot_n17_decide_boot(
  const struct bk7258_boot_n17_record_s *record,
  const struct bk7258_boot_n17_slot_evidence_s evidence[2],
  struct bk7258_boot_n17_boot_decision_s *decision)
{
  uint8_t stable;
  uint8_t other;

  if (record == NULL || evidence == NULL || decision == NULL ||
      record_shape(record) < 0)
    {
      return -EINVAL;
    }

  stable = record->stable_slot;
  other = 1u - stable;
  decision->slot = 0xffu;
  if (!evidence_valid(record, evidence, stable, false))
    {
      if (record->phase == BK7258_BOOT_N17_STABLE &&
          evidence_valid(record, evidence, other, false))
        {
          decision->action = BK7258_BOOT_N17_ACTION_SIGNED_FALLBACK;
          decision->slot = other;
          return 0;
        }

      decision->action = BK7258_BOOT_N17_ACTION_FAIL_CLOSED;
      return 0;
    }

  decision->slot = stable;
  if (record->phase == BK7258_BOOT_N17_STABLE)
    {
      decision->action = BK7258_BOOT_N17_ACTION_STABLE;
    }
  else if (record->phase == BK7258_BOOT_N17_TRIAL)
    {
      decision->action = BK7258_BOOT_N17_ACTION_STABLE_TRIAL_CONSUMED;
    }
  else if (evidence_valid(record, evidence, record->target_slot, true))
    {
      decision->action = BK7258_BOOT_N17_ACTION_TARGET_TRIAL;
      decision->slot = record->target_slot;
    }
  else
    {
      decision->action = BK7258_BOOT_N17_ACTION_STABLE_TARGET_REJECTED;
    }

  return 0;
}
