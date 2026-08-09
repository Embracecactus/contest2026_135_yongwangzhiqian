/*
 * boot_ota_select_core.c - portable N15-C metadata and pair selector.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This file has no NuttX, SDK, MMIO, heap, or libc dependency.  The target
 * adapter and host harness provide raw-Flash and SHA-256 callbacks.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_ota_select_core.h"

#define META_MAGIC                  "BKOTA15C"
#define META_FORMAT                 1u

#define META_MAGIC_OFFSET           0u
#define META_FORMAT_OFFSET          8u
#define META_SIZE_OFFSET            10u
#define META_STATE_OFFSET           12u
#define META_SEQUENCE_OFFSET        16u
#define META_GENERATION_OFFSET      24u
#define META_TIMESTAMP_OFFSET       32u
#define META_CP_PHYSICAL_LENGTH     36u
#define META_AP_PHYSICAL_LENGTH     40u
#define META_VERSION_OFFSET         44u
#define META_BASE_VERSION_OFFSET    68u
#define META_PRIMARY_SHA_OFFSET     92u
#define META_DESCRIPTOR_OFFSET      124u
#define META_CRC_OFFSET             508u

#define STAGE_MAGIC_OFFSET          0u
#define STAGE_FORMAT_OFFSET         8u
#define STAGE_SIZE_OFFSET           10u
#define STAGE_FLAGS_OFFSET          12u
#define STAGE_SCHEMA_OFFSET         16u
#define STAGE_LAYOUT_OFFSET         48u
#define STAGE_GENERATION_OFFSET     96u
#define STAGE_TIMESTAMP_OFFSET      104u
#define STAGE_PHYSICAL_OFFSET       108u
#define STAGE_PHYSICAL_SIZE         112u
#define STAGE_LOGICAL_SIZE          116u
#define STAGE_RBL_OFFSET            120u
#define STAGE_RBL_PHYSICAL_OFFSET   124u
#define STAGE_CP_OFFSET             128u
#define STAGE_CP_CAPACITY           132u
#define STAGE_CP_LENGTH             136u
#define STAGE_CP_XIP                140u
#define STAGE_AP_OFFSET             144u
#define STAGE_AP_CAPACITY           148u
#define STAGE_AP_LENGTH             152u
#define STAGE_AP_XIP                156u
#define STAGE_VERSION_OFFSET        160u
#define STAGE_BASE_VERSION_OFFSET   184u
#define STAGE_PHYSICAL_SHA_OFFSET   208u
#define STAGE_LOGICAL_SHA_OFFSET    240u
#define STAGE_CP_SHA_OFFSET         272u
#define STAGE_AP_SHA_OFFSET         304u
#define STAGE_MANIFEST_SHA_OFFSET   336u
#define STAGE_RESERVED_OFFSET       368u
#define STAGE_CRC_OFFSET            380u

#define PRIMARY_START               BK7258_ROLE_SLOT_A_CP_OFFSET
#define PRIMARY_SIZE                BK7258_ROLE_SLOT_B_PAIR_SIZE
#define CP_PHYSICAL_SIZE            BK7258_ROLE_SLOT_A_CP_SIZE
#define AP_PHYSICAL_SIZE            BK7258_ROLE_SLOT_A_AP_SIZE
#define SECONDARY_START             BK7258_ROLE_SLOT_B_PAIR_OFFSET
#define SECONDARY_SIZE              BK7258_ROLE_SLOT_B_PAIR_SIZE
#define CP_LOGICAL_SIZE             BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE
#define AP_LOGICAL_SIZE             BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE
#define PAIR_LOGICAL_SIZE           (CP_LOGICAL_SIZE + AP_LOGICAL_SIZE)
#define CP_XIP                      BK7258_ROLE_SLOT_A_CP_XIP_START
#define AP_XIP                      BK7258_ROLE_SLOT_A_AP_XIP_START
#define RBL_LOGICAL_OFFSET          \
  (PAIR_LOGICAL_SIZE - BK7258_FLASH_ERASE_SIZE)
#define RBL_PHYSICAL_OFFSET         \
  (RBL_LOGICAL_OFFSET / BK7258_FLASH_CRC_DATA_SIZE * \
   BK7258_FLASH_CRC_TOTAL_SIZE)
#define RBL_HEADER_SIZE             96u

#define PACKET_DATA                 BK7258_FLASH_CRC_DATA_SIZE
#define PACKET_SIZE                 BK7258_FLASH_CRC_TOTAL_SIZE
#define PRIMARY_CHUNK               4080u
#define HASH_CONTEXT_OFFSET         4096u
#define SHA256_SIZE                 32u
#define VERSION_SIZE                24u
#define SRAM_START                  0x28000000u
#define SRAM_END                    0x280a0000u

struct metadata_identity_s
{
  uint64_t sequence;
  struct bk7258_ota_expected_s expected;
  uint32_t cp_physical_length;
  uint32_t ap_physical_length;
  uint8_t primary_sha[SHA256_SIZE];
  const uint8_t *descriptor;
  enum bk7258_boot_ota_metadata_state_e state;
};

struct metadata_scan_s
{
  struct metadata_identity_s identity;
  uint32_t valid_records;
  bool erased;
  bool valid;
  bool trusted;
};

struct region_source_s
{
  const struct bk7258_boot_ota_raw_ops_s *raw_ops;
  uint32_t start;
  uint32_t size;
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

static bool digest_nonzero(const uint8_t *digest)
{
  return !bytes_value(digest, SHA256_SIZE, 0);
}

static uint32_t crc32_update(uint32_t state, const uint8_t *data, size_t len)
{
  size_t index;
  unsigned int bit;

  for (index = 0; index < len; index++)
    {
      state ^= data[index];
      for (bit = 0; bit < 8; bit++)
        {
          state = (state >> 1) ^
                  ((state & 1u) != 0 ? 0xedb88320u : 0u);
        }
    }

  return state;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t len)
{
  return crc32_update(0xffffffffu, data, len) ^ 0xffffffffu;
}

static uint16_t crc16_packet(const uint8_t *data)
{
  uint16_t crc = 0xffffu;
  size_t index;
  unsigned int bit;

  for (index = 0; index < PACKET_DATA; index++)
    {
      crc ^= (uint16_t)data[index] << 8;
      for (bit = 0; bit < 8; bit++)
        {
          crc = (uint16_t)((crc << 1) ^
                           ((crc & 0x8000u) != 0 ? 0x8005u : 0u));
        }
    }

  return crc;
}

static bool canonical_version(const uint8_t field[VERSION_SIZE])
{
  size_t index;
  size_t length = VERSION_SIZE;

  for (index = 0; index < VERSION_SIZE; index++)
    {
      uint8_t value = field[index];

      if (value == 0)
        {
          length = index;
          break;
        }

      if (!((value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9') ||
            (index != 0 && (value == '.' || value == '_' || value == '+' ||
                            value == '-'))))
        {
          return false;
        }
    }

  if (length == 0 || length == VERSION_SIZE)
    {
      return false;
    }

  return bytes_value(field + length, VERSION_SIZE - length, 0);
}

static bool padded_string(const uint8_t *field, size_t field_size,
                          const char *expected)
{
  size_t index = 0;

  while (expected[index] != '\0')
    {
      if (index >= field_size || field[index] != (uint8_t)expected[index])
        {
          return false;
        }

      index++;
    }

  if (index >= field_size || field[index] != 0)
    {
      return false;
    }

  return bytes_value(field + index, field_size - index, 0);
}

static bool descriptor_light_valid(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct metadata_identity_s *identity)
{
  const uint8_t *version = descriptor + STAGE_VERSION_OFFSET;
  const uint8_t *base_version = descriptor + STAGE_BASE_VERSION_OFFSET;
  uint32_t cp_length = getle32(descriptor + STAGE_CP_LENGTH);
  uint32_t ap_length = getle32(descriptor + STAGE_AP_LENGTH);

  return bytes_equal(descriptor + STAGE_MAGIC_OFFSET,
                     (const uint8_t *)"BKOTA15B", 8) &&
         getle16(descriptor + STAGE_FORMAT_OFFSET) == 1 &&
         getle16(descriptor + STAGE_SIZE_OFFSET) ==
           BK7258_OTA_STAGE_DESCRIPTOR_SIZE &&
         getle32(descriptor + STAGE_FLAGS_OFFSET) == 0 &&
         padded_string(descriptor + STAGE_SCHEMA_OFFSET, 32,
                       "bk7258-cp-ap-pair-v1") &&
         padded_string(descriptor + STAGE_LAYOUT_OFFSET, 48,
                       BK7258_PARTITION_LAYOUT_ID) &&
         getle64(descriptor + STAGE_GENERATION_OFFSET) ==
           identity->expected.generation &&
         getle32(descriptor + STAGE_TIMESTAMP_OFFSET) ==
           identity->expected.timestamp &&
         getle32(descriptor + STAGE_PHYSICAL_OFFSET) == SECONDARY_START &&
         getle32(descriptor + STAGE_PHYSICAL_SIZE) == SECONDARY_SIZE &&
         getle32(descriptor + STAGE_LOGICAL_SIZE) == PAIR_LOGICAL_SIZE &&
         getle32(descriptor + STAGE_RBL_OFFSET) == RBL_LOGICAL_OFFSET &&
         getle32(descriptor + STAGE_RBL_PHYSICAL_OFFSET) ==
           RBL_PHYSICAL_OFFSET &&
         getle32(descriptor + STAGE_CP_OFFSET) == 0 &&
         getle32(descriptor + STAGE_CP_CAPACITY) == CP_LOGICAL_SIZE &&
         cp_length >= 0x108u && cp_length <= CP_LOGICAL_SIZE &&
         getle32(descriptor + STAGE_CP_XIP) == CP_XIP &&
         getle32(descriptor + STAGE_AP_OFFSET) == CP_LOGICAL_SIZE &&
         getle32(descriptor + STAGE_AP_CAPACITY) == AP_LOGICAL_SIZE &&
         ap_length >= 8u && ap_length <= AP_LOGICAL_SIZE &&
         getle32(descriptor + STAGE_AP_XIP) == AP_XIP &&
         canonical_version(version) && canonical_version(base_version) &&
         bytes_equal(version, (const uint8_t *)identity->expected.version,
                     VERSION_SIZE) &&
         bytes_equal(base_version,
                     (const uint8_t *)identity->expected.base_version,
                     VERSION_SIZE) &&
         !bytes_equal(version, base_version, VERSION_SIZE) &&
         digest_nonzero(descriptor + STAGE_PHYSICAL_SHA_OFFSET) &&
         digest_nonzero(descriptor + STAGE_LOGICAL_SHA_OFFSET) &&
         digest_nonzero(descriptor + STAGE_CP_SHA_OFFSET) &&
         digest_nonzero(descriptor + STAGE_AP_SHA_OFFSET) &&
         digest_nonzero(descriptor + STAGE_MANIFEST_SHA_OFFSET) &&
         bytes_value(descriptor + STAGE_RESERVED_OFFSET, 12, 0) &&
         crc32_bytes(descriptor, STAGE_CRC_OFFSET) ==
           getle32(descriptor + STAGE_CRC_OFFSET);
}

static int parse_record(const uint8_t *record,
                        struct metadata_identity_s *identity)
{
  uint32_t cp_physical_length;
  uint32_t ap_physical_length;
  uint32_t state;

  cp_physical_length = getle32(record + META_CP_PHYSICAL_LENGTH);
  ap_physical_length = getle32(record + META_AP_PHYSICAL_LENGTH);

  if (crc32_bytes(record, META_CRC_OFFSET) !=
        getle32(record + META_CRC_OFFSET) ||
      !bytes_equal(record + META_MAGIC_OFFSET,
                   (const uint8_t *)META_MAGIC, 8) ||
      getle16(record + META_FORMAT_OFFSET) != META_FORMAT ||
      getle16(record + META_SIZE_OFFSET) != BK7258_BOOT_OTA_RECORD_SIZE ||
      getle64(record + META_SEQUENCE_OFFSET) == 0 ||
      getle64(record + META_GENERATION_OFFSET) == 0 ||
      cp_physical_length < 9u * PACKET_SIZE ||
      cp_physical_length > CP_PHYSICAL_SIZE ||
      cp_physical_length % PACKET_SIZE != 0 ||
      ap_physical_length < PACKET_SIZE ||
      ap_physical_length > AP_PHYSICAL_SIZE ||
      ap_physical_length % PACKET_SIZE != 0 ||
      !canonical_version(record + META_VERSION_OFFSET) ||
      !canonical_version(record + META_BASE_VERSION_OFFSET) ||
      bytes_equal(record + META_VERSION_OFFSET,
                  record + META_BASE_VERSION_OFFSET, VERSION_SIZE) ||
      !digest_nonzero(record + META_PRIMARY_SHA_OFFSET))
    {
      return -EBADMSG;
    }

  state = getle32(record + META_STATE_OFFSET);
  if (state < BK7258_BOOT_OTA_META_PENDING_B ||
      state > BK7258_BOOT_OTA_META_ROLLBACK_A)
    {
      return -EBADMSG;
    }

  identity->sequence = getle64(record + META_SEQUENCE_OFFSET);
  identity->expected.generation =
    getle64(record + META_GENERATION_OFFSET);
  identity->expected.timestamp = getle32(record + META_TIMESTAMP_OFFSET);
  identity->cp_physical_length = cp_physical_length;
  identity->ap_physical_length = ap_physical_length;
  bytes_copy((uint8_t *)identity->expected.version,
             record + META_VERSION_OFFSET, VERSION_SIZE);
  bytes_copy((uint8_t *)identity->expected.base_version,
             record + META_BASE_VERSION_OFFSET, VERSION_SIZE);
  bytes_copy(identity->primary_sha, record + META_PRIMARY_SHA_OFFSET,
             SHA256_SIZE);
  identity->descriptor = record + META_DESCRIPTOR_OFFSET;
  identity->state = (enum bk7258_boot_ota_metadata_state_e)state;

  return descriptor_light_valid(identity->descriptor, identity) ?
         0 : -EBADMSG;
}

static bool same_identity(const struct metadata_identity_s *left,
                          const struct metadata_identity_s *right)
{
  return left->expected.generation == right->expected.generation &&
         left->expected.timestamp == right->expected.timestamp &&
         left->cp_physical_length == right->cp_physical_length &&
         left->ap_physical_length == right->ap_physical_length &&
         bytes_equal((const uint8_t *)left->expected.version,
                     (const uint8_t *)right->expected.version,
                     VERSION_SIZE) &&
         bytes_equal((const uint8_t *)left->expected.base_version,
                     (const uint8_t *)right->expected.base_version,
                     VERSION_SIZE) &&
         bytes_equal(left->primary_sha, right->primary_sha, SHA256_SIZE) &&
         bytes_equal(left->descriptor, right->descriptor,
                     BK7258_OTA_STAGE_DESCRIPTOR_SIZE);
}

static bool valid_transition(enum bk7258_boot_ota_metadata_state_e previous,
                             enum bk7258_boot_ota_metadata_state_e next)
{
  if (previous == BK7258_BOOT_OTA_META_PENDING_B)
    {
      return next == BK7258_BOOT_OTA_META_TRIAL_STARTED;
    }

  if (previous == BK7258_BOOT_OTA_META_TRIAL_STARTED)
    {
      return next == BK7258_BOOT_OTA_META_CONFIRMED_B ||
             next == BK7258_BOOT_OTA_META_ROLLBACK_A;
    }

  return false;
}

static void scan_metadata(
  const uint8_t metadata[BK7258_BOOT_OTA_METADATA_SIZE],
  struct metadata_scan_s *scan)
{
  struct metadata_identity_s first;
  struct metadata_identity_s current;
  struct metadata_identity_s previous;
  uint32_t index;

  scan->valid_records = 0;
  scan->erased = false;
  scan->valid = false;
  scan->trusted = false;

  for (index = 0; index < BK7258_BOOT_OTA_RECORD_COUNT; index++)
    {
      const uint8_t *record = metadata + index * BK7258_BOOT_OTA_RECORD_SIZE;
      uint32_t later_size;

      if (bytes_value(record, BK7258_BOOT_OTA_RECORD_SIZE, 0xff))
        {
          later_size = BK7258_BOOT_OTA_METADATA_SIZE -
                       (index + 1u) * BK7258_BOOT_OTA_RECORD_SIZE;
          if (!bytes_value(record + BK7258_BOOT_OTA_RECORD_SIZE,
                           later_size, 0xff))
            {
              return;
            }

          if (index == 0)
            {
              scan->erased = true;
              scan->valid = true;
            }
          else
            {
              scan->valid = true;
              scan->identity = previous;
            }

          return;
        }

      if (parse_record(record, &current) < 0)
        {
          return;
        }

      if (index == 0)
        {
          if (current.state != BK7258_BOOT_OTA_META_PENDING_B)
            {
              return;
            }

          first = current;
          scan->trusted = true;
          scan->identity = current;
        }
      else if (!same_identity(&first, &current) ||
               previous.sequence == UINT64_MAX ||
               current.sequence != previous.sequence + 1u ||
               !valid_transition(previous.state, current.state))
        {
          return;
        }

      previous = current;
      scan->identity = current;
      scan->valid_records = index + 1u;
    }

  scan->valid = true;
}

int bk7258_boot_ota_metadata_inspect(
  const uint8_t metadata[BK7258_BOOT_OTA_METADATA_SIZE],
  struct bk7258_boot_ota_metadata_info_s *info)
{
  struct metadata_scan_s scan;

  if (metadata == NULL || info == NULL)
    {
      return -EINVAL;
    }

  info->state = BK7258_BOOT_OTA_META_ERASED;
  info->valid_records = 0;
  info->sequence = 0;
  info->generation = 0;
  info->erased = false;
  info->trusted = false;

  scan_metadata(metadata, &scan);
  if (!scan.valid)
    {
      return -EBADMSG;
    }

  info->erased = scan.erased;
  info->trusted = scan.trusted;
  info->valid_records = scan.valid_records;
  if (scan.trusted)
    {
      info->state = scan.identity.state;
      info->sequence = scan.identity.sequence;
      info->generation = scan.identity.expected.generation;
    }

  return 0;
}

int bk7258_boot_ota_prepare_transition(
  const uint8_t metadata[BK7258_BOOT_OTA_METADATA_SIZE],
  uint64_t expected_generation,
  enum bk7258_boot_ota_metadata_state_e expected_state,
  enum bk7258_boot_ota_metadata_state_e next_state,
  uint8_t record[BK7258_BOOT_OTA_RECORD_SIZE],
  struct bk7258_boot_ota_transition_s *transition)
{
  struct metadata_scan_s scan;
  const uint8_t *previous;
  uint64_t sequence;
  uint32_t crc;

  if (metadata == NULL || record == NULL || transition == NULL ||
      expected_generation == 0 || !valid_transition(expected_state,
                                                     next_state))
    {
      return -EINVAL;
    }

  scan_metadata(metadata, &scan);
  if (!scan.valid || !scan.trusted || scan.erased ||
      scan.valid_records == 0)
    {
      return -EBADMSG;
    }

  if (scan.identity.expected.generation != expected_generation)
    {
      return -ESTALE;
    }

  if (scan.identity.state != expected_state)
    {
      return -EPERM;
    }

  if (scan.valid_records >= BK7258_BOOT_OTA_RECORD_COUNT)
    {
      return -ENOSPC;
    }

  if (scan.identity.sequence == UINT64_MAX)
    {
      return -EOVERFLOW;
    }

  previous = metadata +
             (scan.valid_records - 1u) * BK7258_BOOT_OTA_RECORD_SIZE;
  bytes_copy(record, previous, BK7258_BOOT_OTA_RECORD_SIZE);
  sequence = scan.identity.sequence + 1u;
  putle32(record + META_STATE_OFFSET, (uint32_t)next_state);
  putle64(record + META_SEQUENCE_OFFSET, sequence);
  crc = crc32_bytes(record, META_CRC_OFFSET);
  putle32(record + META_CRC_OFFSET, crc);

  transition->previous_state = expected_state;
  transition->next_state = next_state;
  transition->record_index = scan.valid_records;
  transition->record_offset =
    scan.valid_records * BK7258_BOOT_OTA_RECORD_SIZE;
  transition->sequence = sequence;
  transition->generation = expected_generation;
  return 0;
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

static void capture(uint8_t *destination, uint32_t wanted_start,
                    uint32_t wanted_size, uint32_t block_start,
                    const uint8_t *block)
{
  uint32_t wanted_end = wanted_start + wanted_size;
  uint32_t block_end = block_start + PACKET_DATA;
  uint32_t start = wanted_start > block_start ? wanted_start : block_start;
  uint32_t end = wanted_end < block_end ? wanted_end : block_end;

  if (start < end)
    {
      bytes_copy(destination + start - wanted_start,
                 block + start - block_start, end - start);
    }
}

static int validate_vectors(const uint8_t cp_vector[8],
                            const uint8_t cp_magic[8],
                            const uint8_t ap_vector[8],
                            uint32_t cp_logical_size,
                            uint32_t ap_logical_size)
{
  uint32_t cp_msp = getle32(cp_vector);
  uint32_t cp_reset = getle32(cp_vector + 4);
  uint32_t ap_msp = getle32(ap_vector);
  uint32_t ap_reset = getle32(ap_vector + 4);

  if (cp_msp < SRAM_START || cp_msp >= SRAM_END ||
      ap_msp < SRAM_START || ap_msp >= SRAM_END ||
      (cp_reset & 1u) == 0 || (ap_reset & 1u) == 0 ||
      (cp_reset & ~1u) < CP_XIP ||
      (cp_reset & ~1u) >= CP_XIP + cp_logical_size ||
      (ap_reset & ~1u) < AP_XIP ||
      (ap_reset & ~1u) >= AP_XIP + ap_logical_size ||
      !bytes_equal(cp_magic, (const uint8_t *)"BK7236\0\0", 8))
    {
      return -EBADMSG;
    }

  return 0;
}

static int validate_primary_headers(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops, uint32_t physical_start,
  uint8_t *scratch)
{
  uint8_t cp_vector[8] = {0};
  uint8_t cp_magic[8] = {0};
  uint8_t ap_vector[8] = {0};
  uint32_t physical;

  for (physical = 0; physical < 9u * PACKET_SIZE;
       physical += PACKET_SIZE)
    {
      uint32_t logical = physical / PACKET_SIZE * PACKET_DATA;
      uint16_t stored;
      int ret = raw_read(raw_ops, physical_start + physical, scratch,
                         PACKET_SIZE);

      if (ret < 0)
        {
          return ret;
        }

      stored = ((uint16_t)scratch[32] << 8) | scratch[33];
      if (crc16_packet(scratch) != stored)
        {
          return -EBADMSG;
        }

      capture(cp_vector, 0, sizeof(cp_vector), logical, scratch);
      capture(cp_magic, 0x100u, sizeof(cp_magic), logical, scratch);
    }

  if (raw_read(raw_ops, physical_start + CP_PHYSICAL_SIZE, scratch,
               PACKET_SIZE) < 0)
    {
      return -EIO;
    }

  if (crc16_packet(scratch) !=
      (((uint16_t)scratch[32] << 8) | scratch[33]))
    {
      return -EBADMSG;
    }

  bytes_copy(ap_vector, scratch, sizeof(ap_vector));
  return validate_vectors(cp_vector, cp_magic, ap_vector, CP_LOGICAL_SIZE,
                          AP_LOGICAL_SIZE);
}

static int validate_primary_component(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops, void *hash_context,
  uint32_t physical_start, uint32_t physical_length,
  uint32_t physical_capacity, uint8_t vector[8], uint8_t *magic,
  uint8_t *scratch)
{
  uint32_t physical = 0;
  uint32_t logical = 0;

  while (physical < physical_length)
    {
      uint32_t chunk = physical_length - physical;
      uint32_t index;
      int ret;

      if (chunk > PRIMARY_CHUNK)
        {
          chunk = PRIMARY_CHUNK;
        }

      ret = raw_read(raw_ops, physical_start + physical, scratch, chunk);
      if (ret < 0)
        {
          return ret;
        }

      hash_ops->update(hash_context, scratch, chunk);
      for (index = 0; index < chunk; index += PACKET_SIZE)
        {
          const uint8_t *packet = scratch + index;
          uint16_t stored = ((uint16_t)packet[32] << 8) | packet[33];

          if (crc16_packet(packet) != stored)
            {
              return -EBADMSG;
            }

          capture(vector, 0, 8, logical, packet);
          if (magic != NULL)
            {
              capture(magic, 0x100u, 8, logical, packet);
            }

          logical += PACKET_DATA;
        }

      physical += chunk;
    }

  while (physical < physical_capacity)
    {
      uint32_t chunk = physical_capacity - physical;
      int ret;

      if (chunk > PRIMARY_CHUNK)
        {
          chunk = PRIMARY_CHUNK;
        }

      ret = raw_read(raw_ops, physical_start + physical, scratch, chunk);
      if (ret < 0)
        {
          return ret;
        }

      hash_ops->update(hash_context, scratch, chunk);
      if (!bytes_value(scratch, chunk, 0xff))
        {
          return -EBADMSG;
        }

      physical += chunk;
    }

  return 0;
}

/* A factory slot is assembled from independently encoded CP/AP images and
 * has raw 0xff capacity padding.  Once an OTA candidate becomes stable the
 * same slot instead contains one CRC-expanded pair container, including the
 * official 96-byte RBL tail header.  Validate that second representation
 * packet-by-packet; accepting both forms is required for B -> A rotation.
 */

static int validate_container_pair(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct metadata_identity_s *identity, uint32_t physical_start,
  uint8_t *scratch, size_t scratch_size)
{
  uint8_t cp_vector[8] = {0};
  uint8_t cp_magic[8] = {0};
  uint8_t ap_vector[8] = {0};
  uint8_t digest[SHA256_SIZE];
  uint8_t *context;
  uint32_t cp_logical_size;
  uint32_t ap_logical_size;
  uint32_t physical = 0;
  uint32_t logical = 0;

  if (hash_ops == NULL || hash_ops->init == NULL ||
      hash_ops->update == NULL || hash_ops->final == NULL ||
      hash_ops->context_size == 0 ||
      HASH_CONTEXT_OFFSET + hash_ops->context_size > scratch_size)
    {
      return -EINVAL;
    }

  cp_logical_size = identity->cp_physical_length /
                    PACKET_SIZE * PACKET_DATA;
  ap_logical_size = identity->ap_physical_length /
                    PACKET_SIZE * PACKET_DATA;
  context = scratch + HASH_CONTEXT_OFFSET;
  hash_ops->init(context);
  while (physical < PRIMARY_SIZE)
    {
      uint32_t chunk = PRIMARY_SIZE - physical;
      uint32_t index;
      int ret;

      if (chunk > PRIMARY_CHUNK)
        {
          chunk = PRIMARY_CHUNK;
        }

      ret = raw_read(raw_ops, physical_start + physical, scratch, chunk);
      if (ret < 0)
        {
          return ret;
        }

      hash_ops->update(context, scratch, chunk);
      for (index = 0; index < chunk; index += PACKET_SIZE)
        {
          const uint8_t *packet = scratch + index;
          uint16_t stored = ((uint16_t)packet[32] << 8) | packet[33];
          uint32_t byte;

          if (crc16_packet(packet) != stored)
            {
              return -EBADMSG;
            }

          capture(cp_vector, 0, sizeof(cp_vector), logical, packet);
          capture(cp_magic, 0x100u, sizeof(cp_magic), logical, packet);
          capture(ap_vector, CP_LOGICAL_SIZE, sizeof(ap_vector), logical,
                  packet);
          for (byte = 0; byte < PACKET_DATA; byte++)
            {
              uint32_t position = logical + byte;
              bool payload = position < cp_logical_size ||
                (position >= CP_LOGICAL_SIZE &&
                 position < CP_LOGICAL_SIZE + ap_logical_size) ||
                (position >= RBL_LOGICAL_OFFSET &&
                 position < RBL_LOGICAL_OFFSET + RBL_HEADER_SIZE);

              if (!payload && packet[byte] != 0xffu)
                {
                  return -EBADMSG;
                }
            }

          logical += PACKET_DATA;
        }

      physical += chunk;
    }

  hash_ops->final(context, digest);
  if (!bytes_equal(digest, identity->primary_sha, SHA256_SIZE))
    {
      return -EBADMSG;
    }

  return validate_vectors(cp_vector, cp_magic, ap_vector, cp_logical_size,
                          ap_logical_size);
}

static int validate_primary(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct metadata_identity_s *identity, uint32_t physical_start,
  uint8_t *scratch,
  size_t scratch_size)
{
  uint8_t cp_vector[8] = {0};
  uint8_t cp_magic[8] = {0};
  uint8_t ap_vector[8] = {0};
  uint8_t digest[SHA256_SIZE];
  uint8_t *context;
  uint32_t cp_logical_size;
  uint32_t ap_logical_size;
  int ret;

  if (identity == NULL)
    {
      return validate_primary_headers(raw_ops, physical_start, scratch);
    }

  if (hash_ops == NULL || hash_ops->init == NULL ||
      hash_ops->update == NULL || hash_ops->final == NULL ||
      hash_ops->context_size == 0 ||
      HASH_CONTEXT_OFFSET + hash_ops->context_size > scratch_size)
    {
      return -EINVAL;
    }

  context = scratch + HASH_CONTEXT_OFFSET;
  hash_ops->init(context);

  ret = validate_primary_component(
    raw_ops, hash_ops, context, physical_start,
    identity->cp_physical_length, CP_PHYSICAL_SIZE, cp_vector, cp_magic,
    scratch);
  if (ret < 0)
    {
      if (ret == -EBADMSG)
        {
          hash_ops->final(context, digest);
          return validate_container_pair(raw_ops, hash_ops, identity,
                                         physical_start, scratch,
                                         scratch_size);
        }

      return ret;
    }

  ret = validate_primary_component(
    raw_ops, hash_ops, context, physical_start + CP_PHYSICAL_SIZE,
    identity->ap_physical_length, AP_PHYSICAL_SIZE, ap_vector, NULL,
    scratch);
  if (ret < 0)
    {
      if (ret == -EBADMSG)
        {
          hash_ops->final(context, digest);
          return validate_container_pair(raw_ops, hash_ops, identity,
                                         physical_start, scratch,
                                         scratch_size);
        }

      return ret;
    }

  hash_ops->final(context, digest);
  if (!bytes_equal(digest, identity->primary_sha, SHA256_SIZE))
    {
      return validate_container_pair(raw_ops, hash_ops, identity,
                                     physical_start, scratch, scratch_size);
    }

  cp_logical_size = identity->cp_physical_length / PACKET_SIZE * PACKET_DATA;
  ap_logical_size = identity->ap_physical_length / PACKET_SIZE * PACKET_DATA;
  return validate_vectors(cp_vector, cp_magic, ap_vector, cp_logical_size,
                          ap_logical_size);
}

static int region_read(void *arg, uint32_t offset, uint8_t *buffer,
                       size_t len)
{
  struct region_source_s *region = arg;

  if (offset > region->size || len > region->size - offset)
    {
      return -EINVAL;
    }

  return raw_read(region->raw_ops, region->start + offset, buffer, len);
}

static int validate_secondary(
  const struct metadata_identity_s *identity,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size)
{
  struct region_source_s region;
  struct bk7258_ota_source_s source;
  struct bk7258_ota_stage_result_s stage_result;

  region.raw_ops = raw_ops;
  region.start = SECONDARY_START;
  region.size = SECONDARY_SIZE;
  source.read = region_read;
  source.arg = &region;
  source.size = SECONDARY_SIZE;

  return bk7258_ota_core_validate_at(SECONDARY_START, identity->descriptor,
                                     &identity->expected, &source, hash_ops,
                                     scratch, scratch_size, &stage_result);
}

static bool pair_start_valid(uint32_t physical_start)
{
  return physical_start == PRIMARY_START ||
         physical_start == SECONDARY_START;
}

int bk7258_boot_ota_validate_pair_headers(
  uint32_t physical_start,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  uint8_t *scratch, size_t scratch_size)
{
  if (!pair_start_valid(physical_start) || raw_ops == NULL ||
      scratch == NULL || scratch_size < PACKET_SIZE)
    {
      return -EINVAL;
    }

  return validate_primary_headers(raw_ops, physical_start, scratch);
}

int bk7258_boot_ota_validate_base_pair(
  uint32_t physical_start,
  const struct bk7258_boot_ota_base_pair_s *base,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size)
{
  struct metadata_identity_s identity;

  if (!pair_start_valid(physical_start) || base == NULL ||
      base->sha256 == NULL ||
      base->cp_physical_length < 9u * PACKET_SIZE ||
      base->cp_physical_length > CP_PHYSICAL_SIZE ||
      base->cp_physical_length % PACKET_SIZE != 0 ||
      base->ap_physical_length < PACKET_SIZE ||
      base->ap_physical_length > AP_PHYSICAL_SIZE ||
      base->ap_physical_length % PACKET_SIZE != 0 ||
      !digest_nonzero(base->sha256))
    {
      return -EINVAL;
    }

  identity.cp_physical_length = base->cp_physical_length;
  identity.ap_physical_length = base->ap_physical_length;
  bytes_copy(identity.primary_sha, base->sha256, SHA256_SIZE);
  return validate_primary(raw_ops, hash_ops, &identity, physical_start,
                          scratch, scratch_size);
}

int bk7258_boot_ota_validate_candidate_pair(
  uint32_t physical_start,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size)
{
  struct region_source_s region;
  struct bk7258_ota_source_s source;
  struct bk7258_ota_stage_result_s stage_result;

  if (!pair_start_valid(physical_start) || raw_ops == NULL)
    {
      return -EINVAL;
    }

  region.raw_ops = raw_ops;
  region.start = physical_start;
  region.size = SECONDARY_SIZE;
  source.read = region_read;
  source.arg = &region;
  source.size = SECONDARY_SIZE;
  return bk7258_ota_core_validate_at(physical_start, descriptor, expected,
                                     &source, hash_ops, scratch,
                                     scratch_size, &stage_result);
}

int bk7258_boot_ota_select_core(
  const uint8_t metadata[BK7258_BOOT_OTA_METADATA_SIZE],
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_boot_ota_result_s *result)
{
  struct metadata_scan_s scan;
  const struct metadata_identity_s *primary_identity = NULL;
  int ret;

  if (metadata == NULL || raw_ops == NULL || hash_ops == NULL ||
      scratch == NULL || scratch_size < BK7258_OTA_STAGE_SCRATCH_SIZE ||
      result == NULL)
    {
      return -EINVAL;
    }

  result->status = -EINPROGRESS;
  result->decision = BK7258_BOOT_OTA_DECISION_A_FAILSAFE;
  result->reason = BK7258_BOOT_OTA_REASON_METADATA_INVALID;
  result->metadata_state = BK7258_BOOT_OTA_META_ERASED;
  result->valid_records = 0;
  result->generation = 0;
  result->metadata_valid = false;
  result->primary_verified = false;
  result->primary_full_verified = false;
  result->secondary_verified = false;

  scan_metadata(metadata, &scan);
  if (scan.trusted)
    {
      primary_identity = &scan.identity;
      result->generation = scan.identity.expected.generation;
      result->metadata_state = scan.identity.state;
      result->valid_records = scan.valid_records;
    }

  ret = validate_primary(raw_ops, hash_ops, primary_identity, PRIMARY_START,
                         scratch,
                         scratch_size);
  if (ret < 0)
    {
      result->status = ret;
      return ret;
    }

  result->primary_verified = true;
  result->primary_full_verified = scan.trusted;

  if (!scan.valid)
    {
      result->decision = BK7258_BOOT_OTA_DECISION_A_FAILSAFE;
      result->reason = BK7258_BOOT_OTA_REASON_METADATA_INVALID;
      result->status = 0;
      return 0;
    }

  result->metadata_valid = true;
  if (scan.erased)
    {
      result->decision = BK7258_BOOT_OTA_DECISION_A_BASELINE;
      result->reason = BK7258_BOOT_OTA_REASON_METADATA_ERASED;
      result->status = 0;
      return 0;
    }

  result->generation = scan.identity.expected.generation;
  result->metadata_state = scan.identity.state;
  result->valid_records = scan.valid_records;

  if (scan.identity.state == BK7258_BOOT_OTA_META_TRIAL_STARTED)
    {
      result->decision = BK7258_BOOT_OTA_DECISION_A_ROLLBACK;
      result->reason = BK7258_BOOT_OTA_REASON_TRIAL_CONSUMED;
      result->status = 0;
      return 0;
    }

  if (scan.identity.state == BK7258_BOOT_OTA_META_ROLLBACK_A)
    {
      result->decision = BK7258_BOOT_OTA_DECISION_A_ROLLBACK;
      result->reason = BK7258_BOOT_OTA_REASON_ROLLBACK_REQUESTED;
      result->status = 0;
      return 0;
    }

  ret = validate_secondary(&scan.identity, raw_ops, hash_ops, scratch,
                           scratch_size);
  if (ret < 0)
    {
      result->decision = BK7258_BOOT_OTA_DECISION_A_FAILSAFE;
      result->reason = BK7258_BOOT_OTA_REASON_CANDIDATE_INVALID;
      result->status = 0;
      return 0;
    }

  result->secondary_verified = true;
  if (scan.identity.state == BK7258_BOOT_OTA_META_PENDING_B)
    {
      result->decision = BK7258_BOOT_OTA_DECISION_B_TRIAL_CANDIDATE;
      result->reason = BK7258_BOOT_OTA_REASON_PENDING_VALID;
    }
  else
    {
      result->decision = BK7258_BOOT_OTA_DECISION_B_CONFIRMED;
      result->reason = BK7258_BOOT_OTA_REASON_CONFIRMED_VALID;
    }

  result->status = 0;
  return 0;
}
