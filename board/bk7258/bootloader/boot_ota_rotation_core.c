/*
 * boot_ota_rotation_core.c - portable symmetric OTA metadata bank core.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file has no NuttX, SDK, MMIO, heap or libc dependency.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_rotation_core.h"
#include "../chip/include/bk7258_partition_layout.h"

#define ROTATION_MAGIC                 "BKOTA15R"
#define ROTATION_FORMAT                2u

#define META_MAGIC_OFFSET              0u
#define META_FORMAT_OFFSET             8u
#define META_SIZE_OFFSET              10u
#define META_STATE_OFFSET             12u
#define META_SEQUENCE_OFFSET          16u
#define META_GENERATION_OFFSET        24u
#define META_TIMESTAMP_OFFSET         32u
#define META_CP_LENGTH_OFFSET         36u
#define META_AP_LENGTH_OFFSET         40u
#define META_VERSION_OFFSET           44u
#define META_BASE_VERSION_OFFSET      68u
#define META_BASE_SHA_OFFSET          92u
#define META_DESCRIPTOR_OFFSET       124u
#define META_CRC_OFFSET              508u

#define DESCRIPTOR_MAGIC              "BKOTA15B"
#define DESCRIPTOR_FORMAT             1u
#define DESCRIPTOR_MAGIC_OFFSET       0u
#define DESCRIPTOR_FORMAT_OFFSET      8u
#define DESCRIPTOR_SIZE_OFFSET       10u
#define DESCRIPTOR_FLAGS_OFFSET      12u
#define DESCRIPTOR_GENERATION_OFFSET 96u
#define DESCRIPTOR_TIMESTAMP_OFFSET 104u
#define DESCRIPTOR_PHYSICAL_OFFSET  108u
#define DESCRIPTOR_PHYSICAL_SIZE    112u
#define DESCRIPTOR_CP_LENGTH        136u
#define DESCRIPTOR_AP_LENGTH        152u
#define DESCRIPTOR_VERSION_OFFSET   160u
#define DESCRIPTOR_BASE_VERSION     184u
#define DESCRIPTOR_CRC_OFFSET       380u

#define SLOT_A_START                 BK7258_ROLE_SLOT_A_CP_OFFSET
#define SLOT_B_START                 BK7258_ROLE_SLOT_B_PAIR_OFFSET
#define SLOT_PHYSICAL_SIZE           BK7258_ROLE_SLOT_B_PAIR_SIZE
#define CP_PHYSICAL_SIZE             BK7258_ROLE_SLOT_A_CP_SIZE
#define AP_PHYSICAL_SIZE             BK7258_ROLE_SLOT_A_AP_SIZE
#define CP_LOGICAL_SIZE              BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE
#define AP_LOGICAL_SIZE              BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE
#define PACKET_SIZE                  BK7258_FLASH_CRC_TOTAL_SIZE

struct rotation_record_s
{
  enum bk7258_boot_ota_rotation_state_e state;
  enum bk7258_boot_ota_slot_e base_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint64_t sequence;
  uint64_t generation;
};

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

static bool canonical_string(const uint8_t *value)
{
  size_t index;
  bool terminated = false;

  for (index = 0; index < BK7258_BOOT_OTA_ROTATION_VERSION_SIZE; index++)
    {
      uint8_t byte = value[index];

      if (terminated)
        {
          if (byte != 0)
            {
              return false;
            }
        }
      else if (byte == 0)
        {
          if (index == 0)
            {
              return false;
            }

          terminated = true;
        }
      else if (byte < 0x21u || byte > 0x7eu)
        {
          return false;
        }
    }

  return terminated;
}

static int encode_string(uint8_t *output, const char *value)
{
  size_t index;

  if (value == NULL)
    {
      return -EINVAL;
    }

  for (index = 0; index < BK7258_BOOT_OTA_ROTATION_VERSION_SIZE; index++)
    {
      uint8_t byte = (uint8_t)value[index];

      if (byte == 0)
        {
          if (index == 0)
            {
              return -EINVAL;
            }

          bytes_fill(output + index, 0,
                     BK7258_BOOT_OTA_ROTATION_VERSION_SIZE - index);
          return 0;
        }

      if (byte < 0x21u || byte > 0x7eu)
        {
          return -EINVAL;
        }

      output[index] = byte;
    }

  return -EINVAL;
}

static bool digest_nonzero(const uint8_t *digest)
{
  return !bytes_value(digest, BK7258_BOOT_OTA_ROTATION_SHA256_SIZE, 0);
}

int bk7258_boot_ota_rotation_state_slots(
  enum bk7258_boot_ota_rotation_state_e state,
  enum bk7258_boot_ota_slot_e *base_slot,
  enum bk7258_boot_ota_slot_e *target_slot)
{
  if (base_slot == NULL || target_slot == NULL)
    {
      return -EINVAL;
    }

  if (state >= BK7258_BOOT_OTA_ROTATION_PENDING_B &&
      state <= BK7258_BOOT_OTA_ROTATION_ROLLBACK_A)
    {
      *base_slot = BK7258_BOOT_OTA_SLOT_A;
      *target_slot = BK7258_BOOT_OTA_SLOT_B;
      return 0;
    }

  if (state >= BK7258_BOOT_OTA_ROTATION_PENDING_A &&
      state <= BK7258_BOOT_OTA_ROTATION_ROLLBACK_B)
    {
      *base_slot = BK7258_BOOT_OTA_SLOT_B;
      *target_slot = BK7258_BOOT_OTA_SLOT_A;
      return 0;
    }

  return -EINVAL;
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

static bool valid_transition(enum bk7258_boot_ota_rotation_state_e previous,
                             enum bk7258_boot_ota_rotation_state_e next)
{
  if (previous == BK7258_BOOT_OTA_ROTATION_PENDING_B)
    {
      return next == BK7258_BOOT_OTA_ROTATION_TRIAL_B;
    }

  if (previous == BK7258_BOOT_OTA_ROTATION_TRIAL_B)
    {
      return next == BK7258_BOOT_OTA_ROTATION_CONFIRMED_B ||
             next == BK7258_BOOT_OTA_ROTATION_ROLLBACK_A;
    }

  if (previous == BK7258_BOOT_OTA_ROTATION_PENDING_A)
    {
      return next == BK7258_BOOT_OTA_ROTATION_TRIAL_A;
    }

  if (previous == BK7258_BOOT_OTA_ROTATION_TRIAL_A)
    {
      return next == BK7258_BOOT_OTA_ROTATION_CONFIRMED_A ||
             next == BK7258_BOOT_OTA_ROTATION_ROLLBACK_B;
    }

  return false;
}

static uint32_t slot_start(enum bk7258_boot_ota_slot_e slot)
{
  return slot == BK7258_BOOT_OTA_SLOT_A ? SLOT_A_START : SLOT_B_START;
}

static int descriptor_valid(const uint8_t *descriptor, uint64_t generation,
                            uint32_t timestamp, const uint8_t *version,
                            const uint8_t *base_version,
                            enum bk7258_boot_ota_slot_e target_slot)
{
  uint32_t cp_length;
  uint32_t ap_length;

  if (!bytes_equal(descriptor + DESCRIPTOR_MAGIC_OFFSET,
                   (const uint8_t *)DESCRIPTOR_MAGIC, 8) ||
      getle16(descriptor + DESCRIPTOR_FORMAT_OFFSET) != DESCRIPTOR_FORMAT ||
      getle16(descriptor + DESCRIPTOR_SIZE_OFFSET) !=
        BK7258_BOOT_OTA_ROTATION_DESCRIPTOR_SIZE ||
      getle32(descriptor + DESCRIPTOR_FLAGS_OFFSET) != 0 ||
      getle64(descriptor + DESCRIPTOR_GENERATION_OFFSET) != generation ||
      getle32(descriptor + DESCRIPTOR_TIMESTAMP_OFFSET) != timestamp ||
      getle32(descriptor + DESCRIPTOR_PHYSICAL_OFFSET) !=
        slot_start(target_slot) ||
      getle32(descriptor + DESCRIPTOR_PHYSICAL_SIZE) != SLOT_PHYSICAL_SIZE ||
      !bytes_equal(descriptor + DESCRIPTOR_VERSION_OFFSET, version,
                   BK7258_BOOT_OTA_ROTATION_VERSION_SIZE) ||
      !bytes_equal(descriptor + DESCRIPTOR_BASE_VERSION, base_version,
                   BK7258_BOOT_OTA_ROTATION_VERSION_SIZE) ||
      crc32_bytes(descriptor, DESCRIPTOR_CRC_OFFSET) !=
        getle32(descriptor + DESCRIPTOR_CRC_OFFSET))
    {
      return -EBADMSG;
    }

  cp_length = getle32(descriptor + DESCRIPTOR_CP_LENGTH);
  ap_length = getle32(descriptor + DESCRIPTOR_AP_LENGTH);
  if (cp_length < 0x108u || cp_length > CP_LOGICAL_SIZE ||
      ap_length < 8u || ap_length > AP_LOGICAL_SIZE)
    {
      return -EBADMSG;
    }

  return 0;
}

static int parse_record(const uint8_t *record,
                        struct rotation_record_s *parsed)
{
  uint32_t cp_length;
  uint32_t ap_length;
  uint32_t raw_state;
  int ret;

  if (crc32_bytes(record, META_CRC_OFFSET) !=
        getle32(record + META_CRC_OFFSET) ||
      !bytes_equal(record + META_MAGIC_OFFSET,
                   (const uint8_t *)ROTATION_MAGIC, 8) ||
      getle16(record + META_FORMAT_OFFSET) != ROTATION_FORMAT ||
      getle16(record + META_SIZE_OFFSET) !=
        BK7258_BOOT_OTA_ROTATION_RECORD_SIZE ||
      getle64(record + META_SEQUENCE_OFFSET) == 0 ||
      getle64(record + META_GENERATION_OFFSET) == 0 ||
      !canonical_string(record + META_VERSION_OFFSET) ||
      !canonical_string(record + META_BASE_VERSION_OFFSET) ||
      bytes_equal(record + META_VERSION_OFFSET,
                  record + META_BASE_VERSION_OFFSET,
                  BK7258_BOOT_OTA_ROTATION_VERSION_SIZE) ||
      !digest_nonzero(record + META_BASE_SHA_OFFSET))
    {
      return -EBADMSG;
    }

  cp_length = getle32(record + META_CP_LENGTH_OFFSET);
  ap_length = getle32(record + META_AP_LENGTH_OFFSET);
  if (cp_length < 9u * PACKET_SIZE || cp_length > CP_PHYSICAL_SIZE ||
      cp_length % PACKET_SIZE != 0 || ap_length < PACKET_SIZE ||
      ap_length > AP_PHYSICAL_SIZE || ap_length % PACKET_SIZE != 0)
    {
      return -EBADMSG;
    }

  raw_state = getle32(record + META_STATE_OFFSET);
  if (raw_state < BK7258_BOOT_OTA_ROTATION_PENDING_B ||
      raw_state > BK7258_BOOT_OTA_ROTATION_ROLLBACK_B)
    {
      return -EBADMSG;
    }

  parsed->state = (enum bk7258_boot_ota_rotation_state_e)raw_state;
  ret = bk7258_boot_ota_rotation_state_slots(parsed->state,
                                              &parsed->base_slot,
                                              &parsed->target_slot);
  if (ret < 0)
    {
      return ret;
    }

  parsed->sequence = getle64(record + META_SEQUENCE_OFFSET);
  parsed->generation = getle64(record + META_GENERATION_OFFSET);
  return descriptor_valid(record + META_DESCRIPTOR_OFFSET,
                          parsed->generation,
                          getle32(record + META_TIMESTAMP_OFFSET),
                          record + META_VERSION_OFFSET,
                          record + META_BASE_VERSION_OFFSET,
                          parsed->target_slot);
}

static bool same_identity(const uint8_t *first, const uint8_t *current)
{
  return bytes_equal(first + META_MAGIC_OFFSET,
                     current + META_MAGIC_OFFSET, META_STATE_OFFSET) &&
         bytes_equal(first + META_GENERATION_OFFSET,
                     current + META_GENERATION_OFFSET,
                     META_CRC_OFFSET - META_GENERATION_OFFSET);
}

int bk7258_boot_ota_rotation_build_pending(
  const struct bk7258_boot_ota_rotation_pending_s *pending,
  uint8_t record[BK7258_BOOT_OTA_ROTATION_RECORD_SIZE])
{
  enum bk7258_boot_ota_rotation_state_e state;
  uint8_t *version;
  uint8_t *base_version;
  uint32_t crc;

  if (pending == NULL || record == NULL || pending->generation == 0 ||
      pending->base_sha256 == NULL ||
      pending->descriptor == NULL ||
      (pending->target_slot != BK7258_BOOT_OTA_SLOT_A &&
       pending->target_slot != BK7258_BOOT_OTA_SLOT_B) ||
      pending->base_cp_physical_length < 9u * PACKET_SIZE ||
      pending->base_cp_physical_length > CP_PHYSICAL_SIZE ||
      pending->base_cp_physical_length % PACKET_SIZE != 0 ||
      pending->base_ap_physical_length < PACKET_SIZE ||
      pending->base_ap_physical_length > AP_PHYSICAL_SIZE ||
      pending->base_ap_physical_length % PACKET_SIZE != 0 ||
      !digest_nonzero(pending->base_sha256))
    {
      return -EINVAL;
    }

  bytes_fill(record, 0, BK7258_BOOT_OTA_ROTATION_RECORD_SIZE);
  bytes_copy(record + META_MAGIC_OFFSET,
             (const uint8_t *)ROTATION_MAGIC, 8);
  putle16(record + META_FORMAT_OFFSET, ROTATION_FORMAT);
  putle16(record + META_SIZE_OFFSET,
          BK7258_BOOT_OTA_ROTATION_RECORD_SIZE);
  state = pending->target_slot == BK7258_BOOT_OTA_SLOT_A ?
          BK7258_BOOT_OTA_ROTATION_PENDING_A :
          BK7258_BOOT_OTA_ROTATION_PENDING_B;
  putle32(record + META_STATE_OFFSET, (uint32_t)state);
  putle64(record + META_SEQUENCE_OFFSET, 1);
  putle64(record + META_GENERATION_OFFSET, pending->generation);
  putle32(record + META_TIMESTAMP_OFFSET, pending->timestamp);
  putle32(record + META_CP_LENGTH_OFFSET,
          pending->base_cp_physical_length);
  putle32(record + META_AP_LENGTH_OFFSET,
          pending->base_ap_physical_length);
  version = record + META_VERSION_OFFSET;
  base_version = record + META_BASE_VERSION_OFFSET;
  if (encode_string(version, pending->version) < 0 ||
      encode_string(base_version, pending->base_version) < 0 ||
      bytes_equal(version, base_version,
                  BK7258_BOOT_OTA_ROTATION_VERSION_SIZE))
    {
      return -EINVAL;
    }

  bytes_copy(record + META_BASE_SHA_OFFSET, pending->base_sha256,
             BK7258_BOOT_OTA_ROTATION_SHA256_SIZE);
  bytes_copy(record + META_DESCRIPTOR_OFFSET, pending->descriptor,
             BK7258_BOOT_OTA_ROTATION_DESCRIPTOR_SIZE);
  if (descriptor_valid(record + META_DESCRIPTOR_OFFSET,
                       pending->generation, pending->timestamp, version,
                       base_version, pending->target_slot) < 0)
    {
      return -EBADMSG;
    }

  crc = crc32_bytes(record, META_CRC_OFFSET);
  putle32(record + META_CRC_OFFSET, crc);
  return 0;
}

int bk7258_boot_ota_rotation_inspect(
  const uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  struct bk7258_boot_ota_rotation_bank_s *info)
{
  struct rotation_record_s current;
  struct rotation_record_s previous = {0};
  const uint8_t *first = NULL;
  uint32_t index;

  if (bank == NULL || info == NULL)
    {
      return -EINVAL;
    }

  info->state = BK7258_BOOT_OTA_ROTATION_ERASED;
  info->base_slot = BK7258_BOOT_OTA_SLOT_A;
  info->target_slot = BK7258_BOOT_OTA_SLOT_B;
  info->valid_records = 0;
  info->last_record_index = 0;
  info->sequence = 0;
  info->generation = 0;
  info->erased = false;
  info->trusted = false;

  for (index = 0; index < BK7258_BOOT_OTA_ROTATION_RECORD_COUNT; index++)
    {
      const uint8_t *record = bank +
        index * BK7258_BOOT_OTA_ROTATION_RECORD_SIZE;
      size_t later_size;

      if (bytes_value(record, BK7258_BOOT_OTA_ROTATION_RECORD_SIZE, 0xff))
        {
          later_size = BK7258_BOOT_OTA_ROTATION_BANK_SIZE -
            (index + 1u) * BK7258_BOOT_OTA_ROTATION_RECORD_SIZE;
          if (!bytes_value(record + BK7258_BOOT_OTA_ROTATION_RECORD_SIZE,
                           later_size, 0xff))
            {
              return -EBADMSG;
            }

          if (index == 0)
            {
              info->erased = true;
            }

          return 0;
        }

      if (parse_record(record, &current) < 0)
        {
          return -EBADMSG;
        }

      if (index == 0)
        {
          if (!pending_state(current.state) || current.sequence != 1)
            {
              return -EBADMSG;
            }

          first = record;
        }
      else if (!same_identity(first, record) ||
               previous.sequence == UINT64_MAX ||
               current.sequence != previous.sequence + 1u ||
               !valid_transition(previous.state, current.state))
        {
          return -EBADMSG;
        }

      previous = current;
      info->state = current.state;
      info->base_slot = current.base_slot;
      info->target_slot = current.target_slot;
      info->valid_records = index + 1u;
      info->last_record_index = index;
      info->sequence = current.sequence;
      info->generation = current.generation;
      info->trusted = true;
    }

  return 0;
}

static void baseline_view(struct bk7258_boot_ota_rotation_view_s *view)
{
  view->state = BK7258_BOOT_OTA_ROTATION_ERASED;
  view->stable_slot = BK7258_BOOT_OTA_SLOT_A;
  view->target_slot = BK7258_BOOT_OTA_SLOT_B;
  view->selected_bank = BK7258_BOOT_OTA_ROTATION_NO_BANK;
  view->valid_records = 0;
  view->sequence = 0;
  view->generation = 0;
  view->metadata_present = false;
  view->trial_required = false;
}

int bk7258_boot_ota_rotation_select(
  const struct bk7258_boot_ota_rotation_bank_s banks[2],
  struct bk7258_boot_ota_rotation_view_s *view)
{
  const struct bk7258_boot_ota_rotation_bank_s *selected;
  uint32_t selected_bank;

  if (banks == NULL || view == NULL)
    {
      return -EINVAL;
    }

  baseline_view(view);
  if (banks[0].trusted && banks[1].trusted)
    {
      if (banks[0].generation == banks[1].generation)
        {
          return -EBADMSG;
        }

      selected_bank = banks[0].generation > banks[1].generation ? 0u : 1u;
    }
  else if (banks[0].trusted)
    {
      selected_bank = 0;
    }
  else if (banks[1].trusted)
    {
      selected_bank = 1;
    }
  else
    {
      return banks[0].erased && banks[1].erased ? 0 : -EBADMSG;
    }

  selected = &banks[selected_bank];
  view->state = selected->state;
  view->stable_slot = confirmed_state(selected->state) ?
                      selected->target_slot : selected->base_slot;
  view->target_slot = selected->target_slot;
  view->selected_bank = selected_bank;
  view->valid_records = selected->valid_records;
  view->sequence = selected->sequence;
  view->generation = selected->generation;
  view->metadata_present = true;
  view->trial_required = pending_state(selected->state);

  if (!pending_state(selected->state) && !trial_state(selected->state) &&
      !confirmed_state(selected->state) && !rollback_state(selected->state))
    {
      baseline_view(view);
      return -EBADMSG;
    }

  return 0;
}

int bk7258_boot_ota_rotation_latest(
  const uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  struct bk7258_boot_ota_rotation_identity_s *identity)
{
  struct bk7258_boot_ota_rotation_bank_s info;
  struct rotation_record_s parsed;
  const uint8_t *record;
  int ret;

  if (bank == NULL || identity == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_boot_ota_rotation_inspect(bank, &info);
  if (ret < 0)
    {
      return ret;
    }

  if (info.erased || !info.trusted || info.valid_records == 0)
    {
      return -ENOENT;
    }

  record = bank + info.last_record_index *
           BK7258_BOOT_OTA_ROTATION_RECORD_SIZE;
  ret = parse_record(record, &parsed);
  if (ret < 0 || parsed.state != info.state ||
      parsed.sequence != info.sequence ||
      parsed.generation != info.generation)
    {
      return ret < 0 ? ret : -EBADMSG;
    }

  identity->state = parsed.state;
  identity->base_slot = parsed.base_slot;
  identity->target_slot = parsed.target_slot;
  identity->sequence = parsed.sequence;
  identity->generation = parsed.generation;
  identity->timestamp = getle32(record + META_TIMESTAMP_OFFSET);
  identity->base_cp_physical_length =
    getle32(record + META_CP_LENGTH_OFFSET);
  identity->base_ap_physical_length =
    getle32(record + META_AP_LENGTH_OFFSET);
  identity->version = record + META_VERSION_OFFSET;
  identity->base_version = record + META_BASE_VERSION_OFFSET;
  identity->base_sha256 = record + META_BASE_SHA_OFFSET;
  identity->descriptor = record + META_DESCRIPTOR_OFFSET;
  return 0;
}

int bk7258_boot_ota_rotation_prepare_transition(
  const uint8_t bank[BK7258_BOOT_OTA_ROTATION_BANK_SIZE],
  uint64_t expected_generation,
  enum bk7258_boot_ota_rotation_state_e expected_state,
  enum bk7258_boot_ota_rotation_state_e next_state,
  uint8_t record[BK7258_BOOT_OTA_ROTATION_RECORD_SIZE],
  struct bk7258_boot_ota_rotation_transition_s *transition)
{
  struct bk7258_boot_ota_rotation_bank_s info;
  const uint8_t *previous;
  enum bk7258_boot_ota_slot_e base_slot;
  enum bk7258_boot_ota_slot_e target_slot;
  uint64_t sequence;
  uint32_t crc;
  int ret;

  if (bank == NULL || record == NULL || transition == NULL ||
      expected_generation == 0 || !valid_transition(expected_state,
                                                     next_state))
    {
      return -EINVAL;
    }

  ret = bk7258_boot_ota_rotation_inspect(bank, &info);
  if (ret < 0 || !info.trusted || info.erased || info.valid_records == 0)
    {
      return ret < 0 ? ret : -EBADMSG;
    }

  if (info.generation != expected_generation)
    {
      return -ESTALE;
    }

  if (info.state != expected_state)
    {
      return -EPERM;
    }

  if (info.valid_records >= BK7258_BOOT_OTA_ROTATION_RECORD_COUNT)
    {
      return -ENOSPC;
    }

  if (info.sequence == UINT64_MAX)
    {
      return -EOVERFLOW;
    }

  ret = bk7258_boot_ota_rotation_state_slots(next_state, &base_slot,
                                              &target_slot);
  if (ret < 0 || base_slot != info.base_slot ||
      target_slot != info.target_slot)
    {
      return -EINVAL;
    }

  previous = bank + info.last_record_index *
             BK7258_BOOT_OTA_ROTATION_RECORD_SIZE;
  bytes_copy(record, previous, BK7258_BOOT_OTA_ROTATION_RECORD_SIZE);
  sequence = info.sequence + 1u;
  putle32(record + META_STATE_OFFSET, (uint32_t)next_state);
  putle64(record + META_SEQUENCE_OFFSET, sequence);
  crc = crc32_bytes(record, META_CRC_OFFSET);
  putle32(record + META_CRC_OFFSET, crc);

  transition->previous_state = expected_state;
  transition->next_state = next_state;
  transition->base_slot = base_slot;
  transition->target_slot = target_slot;
  transition->record_index = info.valid_records;
  transition->record_offset = info.valid_records *
                              BK7258_BOOT_OTA_ROTATION_RECORD_SIZE;
  transition->sequence = sequence;
  transition->generation = expected_generation;
  transition->current_boot_trial = pending_state(expected_state) &&
                                   trial_state(next_state);
  return 0;
}
