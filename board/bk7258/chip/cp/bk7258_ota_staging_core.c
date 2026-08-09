/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_ota_staging_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable N15-B validation/staging core.  It has no NuttX or SDK calls;
 * production and host tests provide separate hash/Flash operation tables.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "bk7258_ota_staging_core.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_OTA_DESCRIPTOR_MAGIC       "BKOTA15B"
#define BK7258_OTA_DESCRIPTOR_FORMAT      1u
#define BK7258_OTA_DESCRIPTOR_FLAGS       0u
#define BK7258_OTA_PAIR_SCHEMA            "bk7258-cp-ap-pair-v1"
#define BK7258_OTA_LAYOUT_ID               BK7258_PARTITION_LAYOUT_ID

#define BK7258_OTA_PHYSICAL_START          BK7258_ROLE_SLOT_B_PAIR_OFFSET
#define BK7258_OTA_PHYSICAL_SIZE           BK7258_ROLE_SLOT_B_PAIR_SIZE
#define BK7258_OTA_CP_LOGICAL_OFFSET       0x00000000u
#define BK7258_OTA_CP_CAPACITY             \
  BK7258_ROLE_SLOT_A_CP_LOGICAL_SIZE
#define BK7258_OTA_CP_XIP                  BK7258_ROLE_SLOT_A_CP_XIP_START
#define BK7258_OTA_AP_LOGICAL_OFFSET       BK7258_OTA_CP_CAPACITY
#define BK7258_OTA_AP_CAPACITY             \
  BK7258_ROLE_SLOT_A_AP_LOGICAL_SIZE
#define BK7258_OTA_AP_XIP                  BK7258_ROLE_SLOT_A_AP_XIP_START
#define BK7258_OTA_LOGICAL_SIZE            \
  (BK7258_OTA_CP_CAPACITY + BK7258_OTA_AP_CAPACITY)
#define BK7258_OTA_RBL_HEADER_OFFSET       \
  (BK7258_OTA_LOGICAL_SIZE - BK7258_FLASH_ERASE_SIZE)
#define BK7258_OTA_SRAM_START              0x28000000u
#define BK7258_OTA_SRAM_END                0x280a0000u

#define BK7258_OTA_PACKET_DATA             BK7258_FLASH_CRC_DATA_SIZE
#define BK7258_OTA_PACKET_SIZE             BK7258_FLASH_CRC_TOTAL_SIZE
#define BK7258_OTA_SECTOR_SIZE             BK7258_FLASH_ERASE_SIZE
#define BK7258_OTA_PROGRAM_SIZE            256u
#define BK7258_OTA_BODY_ALIGNMENT          64u
#define BK7258_OTA_RBL_HEADER_SIZE         96u
#define BK7258_OTA_RBL_HEADER_CRC_SIZE     92u
#define BK7258_OTA_FNV_OFFSET              0x811c9dc5u
#define BK7258_OTA_FNV_PRIME               0x01000193u
#define BK7258_OTA_DATA_BUFFER_SIZE        4096u
#define BK7258_OTA_VERIFY_BUFFER_SIZE      256u
#define BK7258_OTA_HASH_BUFFER_OFFSET      4352u
#define BK7258_OTA_HASH_ALIGNMENT          16u

/* Descriptor byte offsets.  The descriptor is serialized explicitly and is
 * never cast to a C struct, so host/target alignment cannot change the ABI.
 */

#define D_MAGIC                0u
#define D_FORMAT               8u
#define D_HEADER_SIZE          10u
#define D_FLAGS                12u
#define D_SCHEMA               16u
#define D_SCHEMA_SIZE          32u
#define D_LAYOUT               48u
#define D_LAYOUT_SIZE          48u
#define D_GENERATION           96u
#define D_TIMESTAMP            104u
#define D_PHYSICAL_OFFSET      108u
#define D_PHYSICAL_SIZE        112u
#define D_LOGICAL_SIZE         116u
#define D_RBL_HEADER_OFFSET    120u
#define D_RBL_PHYSICAL_OFFSET  124u
#define D_CP_OFFSET            128u
#define D_CP_CAPACITY          132u
#define D_CP_LENGTH            136u
#define D_CP_XIP               140u
#define D_AP_OFFSET            144u
#define D_AP_CAPACITY          148u
#define D_AP_LENGTH            152u
#define D_AP_XIP               156u
#define D_VERSION              160u
#define D_BASE_VERSION         184u
#define D_VERSION_SIZE         24u
#define D_PHYSICAL_SHA         208u
#define D_LOGICAL_SHA          240u
#define D_CP_SHA               272u
#define D_AP_SHA               304u
#define D_MANIFEST_SHA         336u
#define D_RESERVED             368u
#define D_RESERVED_SIZE        12u
#define D_HEADER_CRC           380u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_ota_descriptor_s
{
  uint64_t generation;
  uint32_t timestamp;
  uint32_t cp_length;
  uint32_t ap_length;
  uint32_t body_size;
  char version[D_VERSION_SIZE];
  char base_version[D_VERSION_SIZE];
  uint8_t physical_sha[BK7258_OTA_STAGE_SHA256_SIZE];
  uint8_t logical_sha[BK7258_OTA_STAGE_SHA256_SIZE];
  uint8_t cp_sha[BK7258_OTA_STAGE_SHA256_SIZE];
  uint8_t ap_sha[BK7258_OTA_STAGE_SHA256_SIZE];
  uint32_t body_crc32;
  uint32_t body_fnv1a;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

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
                  ((state & 1u) ? 0xedb88320u : 0u);
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

  for (index = 0; index < BK7258_OTA_PACKET_DATA; index++)
    {
      crc ^= (uint16_t)data[index] << 8;
      for (bit = 0; bit < 8; bit++)
        {
          crc = (uint16_t)((crc << 1) ^
                           ((crc & 0x8000u) ? 0x8005u : 0u));
        }
    }

  return crc;
}

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
  return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t rbl_physical_offset(uint32_t logical)
{
  return (logical / BK7258_OTA_PACKET_DATA) * BK7258_OTA_PACKET_SIZE;
}

static bool version_character(uint8_t value, bool first)
{
  if ((value >= 'A' && value <= 'Z') ||
      (value >= 'a' && value <= 'z') ||
      (value >= '0' && value <= '9'))
    {
      return true;
    }

  return !first &&
         (value == '.' || value == '_' || value == '+' || value == '-');
}

static int parse_string(const uint8_t *raw, size_t raw_size, char *output,
                        size_t output_size, bool version)
{
  size_t length;
  size_t index;

  for (length = 0; length < raw_size && raw[length] != 0; length++)
    {
      if (raw[length] > 0x7fu ||
          (version && !version_character(raw[length], length == 0)))
        {
          return -EINVAL;
        }
    }

  if (length == 0 || length == raw_size || length >= output_size)
    {
      return -EINVAL;
    }

  for (index = length; index < raw_size; index++)
    {
      if (raw[index] != 0)
        {
          return -EINVAL;
        }
    }

  memcpy(output, raw, length);
  output[length] = '\0';
  return 0;
}

static int validate_expected_string(const char *value)
{
  size_t length;

  for (length = 0; length < D_VERSION_SIZE && value[length] != '\0';
       length++)
    {
      if (!version_character((uint8_t)value[length], length == 0))
        {
          return -EINVAL;
        }
    }

  return length > 0 && length < D_VERSION_SIZE ? 0 : -EINVAL;
}

static bool digest_nonzero(const uint8_t *digest)
{
  size_t index;

  for (index = 0; index < BK7258_OTA_STAGE_SHA256_SIZE; index++)
    {
      if (digest[index] != 0)
        {
          return true;
        }
    }

  return false;
}

static int source_read(const struct bk7258_ota_source_s *source,
                       uint32_t offset, uint8_t *buffer, size_t len)
{
  int ret;

  if (offset > source->size || len > source->size - offset)
    {
      return -EINVAL;
    }

  ret = source->read(source->arg, offset, buffer, len);
  if (ret > 0)
    {
      return -EIO;
    }

  return ret;
}

static void copy_overlap(uint8_t *destination, uint32_t range_start,
                         uint32_t range_size, uint32_t block_start,
                         const uint8_t *block, uint32_t block_size)
{
  uint32_t start = block_start > range_start ? block_start : range_start;
  uint32_t block_end = block_start + block_size;
  uint32_t range_end = range_start + range_size;
  uint32_t end = block_end < range_end ? block_end : range_end;

  if (start < end)
    {
      memcpy(destination + start - range_start, block + start - block_start,
             end - start);
    }
}

static void hash_overlap(const struct bk7258_ota_hash_ops_s *ops,
                         void *context, uint32_t range_start,
                         uint32_t range_size, uint32_t block_start,
                         const uint8_t *block, uint32_t block_size)
{
  uint32_t start = block_start > range_start ? block_start : range_start;
  uint32_t block_end = block_start + block_size;
  uint32_t range_end = range_start + range_size;
  uint32_t end = block_end < range_end ? block_end : range_end;

  if (start < end)
    {
      ops->update(context, block + start - block_start, end - start);
    }
}

static int parse_descriptor(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  uint32_t expected_physical_start,
  struct bk7258_ota_descriptor_s *parsed)
{
  char schema[D_SCHEMA_SIZE];
  char layout[D_LAYOUT_SIZE];
  uint32_t expected_crc;
  uint32_t observed_crc;
  uint32_t body_size;
  size_t index;

  if (descriptor == NULL || expected == NULL || parsed == NULL ||
      (expected_physical_start != BK7258_ROLE_SLOT_A_CP_OFFSET &&
       expected_physical_start != BK7258_ROLE_SLOT_B_PAIR_OFFSET) ||
      expected->generation == 0 ||
      validate_expected_string(expected->version) < 0 ||
      validate_expected_string(expected->base_version) < 0)
    {
      return -EINVAL;
    }

  expected_crc = getle32(descriptor + D_HEADER_CRC);
  observed_crc = crc32_bytes(descriptor, D_HEADER_CRC);
  if (expected_crc != observed_crc)
    {
      return -EBADMSG;
    }

  if (memcmp(descriptor + D_MAGIC, BK7258_OTA_DESCRIPTOR_MAGIC, 8) != 0 ||
      getle16(descriptor + D_FORMAT) != BK7258_OTA_DESCRIPTOR_FORMAT ||
      getle16(descriptor + D_HEADER_SIZE) !=
        BK7258_OTA_STAGE_DESCRIPTOR_SIZE ||
      getle32(descriptor + D_FLAGS) != BK7258_OTA_DESCRIPTOR_FLAGS ||
      parse_string(descriptor + D_SCHEMA, D_SCHEMA_SIZE, schema,
                   sizeof(schema), false) < 0 ||
      strcmp(schema, BK7258_OTA_PAIR_SCHEMA) != 0 ||
      parse_string(descriptor + D_LAYOUT, D_LAYOUT_SIZE, layout,
                   sizeof(layout), false) < 0 ||
      strcmp(layout, BK7258_OTA_LAYOUT_ID) != 0)
    {
      return -EINVAL;
    }

  for (index = 0; index < D_RESERVED_SIZE; index++)
    {
      if (descriptor[D_RESERVED + index] != 0)
        {
          return -EINVAL;
        }
    }

  parsed->generation = getle64(descriptor + D_GENERATION);
  parsed->timestamp = getle32(descriptor + D_TIMESTAMP);
  parsed->cp_length = getle32(descriptor + D_CP_LENGTH);
  parsed->ap_length = getle32(descriptor + D_AP_LENGTH);

  if (parsed->generation == 0 ||
      parsed->generation != expected->generation ||
      parsed->timestamp != expected->timestamp ||
      getle32(descriptor + D_PHYSICAL_OFFSET) != expected_physical_start ||
      getle32(descriptor + D_PHYSICAL_SIZE) != BK7258_OTA_PHYSICAL_SIZE ||
      getle32(descriptor + D_LOGICAL_SIZE) != BK7258_OTA_LOGICAL_SIZE ||
      getle32(descriptor + D_RBL_HEADER_OFFSET) !=
        BK7258_OTA_RBL_HEADER_OFFSET ||
      getle32(descriptor + D_RBL_PHYSICAL_OFFSET) !=
        rbl_physical_offset(BK7258_OTA_RBL_HEADER_OFFSET) ||
      getle32(descriptor + D_CP_OFFSET) != BK7258_OTA_CP_LOGICAL_OFFSET ||
      getle32(descriptor + D_CP_CAPACITY) != BK7258_OTA_CP_CAPACITY ||
      getle32(descriptor + D_CP_XIP) != BK7258_OTA_CP_XIP ||
      getle32(descriptor + D_AP_OFFSET) != BK7258_OTA_AP_LOGICAL_OFFSET ||
      getle32(descriptor + D_AP_CAPACITY) != BK7258_OTA_AP_CAPACITY ||
      getle32(descriptor + D_AP_XIP) != BK7258_OTA_AP_XIP ||
      parsed->cp_length < 0x108u ||
      parsed->cp_length > BK7258_OTA_CP_CAPACITY ||
      parsed->ap_length < 8u ||
      parsed->ap_length > BK7258_OTA_AP_CAPACITY)
    {
      return -EINVAL;
    }

  body_size = align_up(BK7258_OTA_AP_LOGICAL_OFFSET + parsed->ap_length,
                       BK7258_OTA_BODY_ALIGNMENT);
  if (body_size >= BK7258_OTA_RBL_HEADER_OFFSET)
    {
      return -EINVAL;
    }

  parsed->body_size = body_size;
  if (parse_string(descriptor + D_VERSION, D_VERSION_SIZE, parsed->version,
                   sizeof(parsed->version), true) < 0 ||
      parse_string(descriptor + D_BASE_VERSION, D_VERSION_SIZE,
                   parsed->base_version, sizeof(parsed->base_version),
                   true) < 0 ||
      strcmp(parsed->version, expected->version) != 0 ||
      strcmp(parsed->base_version, expected->base_version) != 0 ||
      strcmp(parsed->version, parsed->base_version) == 0)
    {
      return -EINVAL;
    }

  memcpy(parsed->physical_sha, descriptor + D_PHYSICAL_SHA,
         BK7258_OTA_STAGE_SHA256_SIZE);
  memcpy(parsed->logical_sha, descriptor + D_LOGICAL_SHA,
         BK7258_OTA_STAGE_SHA256_SIZE);
  memcpy(parsed->cp_sha, descriptor + D_CP_SHA,
         BK7258_OTA_STAGE_SHA256_SIZE);
  memcpy(parsed->ap_sha, descriptor + D_AP_SHA,
         BK7258_OTA_STAGE_SHA256_SIZE);

  if (!digest_nonzero(parsed->physical_sha) ||
      !digest_nonzero(parsed->logical_sha) ||
      !digest_nonzero(parsed->cp_sha) ||
      !digest_nonzero(parsed->ap_sha) ||
      !digest_nonzero(descriptor + D_MANIFEST_SHA))
    {
      return -EINVAL;
    }

  return 0;
}

static int validate_rbl_header(
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_descriptor_s *parsed, uint8_t *buffer,
  uint32_t *body_crc32, uint32_t *body_fnv1a)
{
  uint8_t header[BK7258_OTA_RBL_HEADER_SIZE];
  char app_partition[16];
  char download_version[D_VERSION_SIZE];
  char current_version[D_VERSION_SIZE];
  uint32_t physical_offset;
  uint32_t index;
  uint16_t stored_crc;
  int ret;

  physical_offset = rbl_physical_offset(BK7258_OTA_RBL_HEADER_OFFSET);
  ret = source_read(source, physical_offset, buffer,
                    3u * BK7258_OTA_PACKET_SIZE);
  if (ret < 0)
    {
      return ret;
    }

  for (index = 0; index < 3; index++)
    {
      uint8_t *packet = buffer + index * BK7258_OTA_PACKET_SIZE;

      stored_crc = ((uint16_t)packet[32] << 8) | packet[33];
      if (crc16_packet(packet) != stored_crc)
        {
          return -EBADMSG;
        }

      memcpy(header + index * BK7258_OTA_PACKET_DATA, packet,
             BK7258_OTA_PACKET_DATA);
    }

  if (memcmp(header, "RBL\0", 4) != 0 || getle16(header + 4) != 0 ||
      header[6] != 0 || header[7] != 0 ||
      getle32(header + 8) != parsed->timestamp ||
      parse_string(header + 12, 16, app_partition, sizeof(app_partition),
                   false) < 0 ||
      strcmp(app_partition, "app") != 0 ||
      parse_string(header + 28, D_VERSION_SIZE, download_version,
                   sizeof(download_version), true) < 0 ||
      strcmp(download_version, parsed->version) != 0 ||
      parse_string(header + 52, D_VERSION_SIZE, current_version,
                   sizeof(current_version), true) < 0 ||
      strcmp(current_version, "00010203040506070809") != 0 ||
      getle32(header + 84) != parsed->body_size ||
      getle32(header + 88) != parsed->body_size ||
      crc32_bytes(header, BK7258_OTA_RBL_HEADER_CRC_SIZE) !=
        getle32(header + 92))
    {
      return -EBADMSG;
    }

  *body_crc32 = getle32(header + 76);
  *body_fnv1a = getle32(header + 80);
  return 0;
}

static int validate_vectors(const struct bk7258_ota_descriptor_s *parsed,
                            const uint8_t cp_vector[8],
                            const uint8_t ap_vector[8],
                            const uint8_t cp_magic[8])
{
  uint32_t cp_msp = getle32(cp_vector);
  uint32_t cp_reset = getle32(cp_vector + 4);
  uint32_t ap_msp = getle32(ap_vector);
  uint32_t ap_reset = getle32(ap_vector + 4);

  if (cp_msp < BK7258_OTA_SRAM_START || cp_msp >= BK7258_OTA_SRAM_END ||
      ap_msp < BK7258_OTA_SRAM_START || ap_msp >= BK7258_OTA_SRAM_END ||
      (cp_reset & 1u) == 0 || (ap_reset & 1u) == 0 ||
      (cp_reset & ~1u) < BK7258_OTA_CP_XIP ||
      (cp_reset & ~1u) >= BK7258_OTA_CP_XIP + parsed->cp_length ||
      (ap_reset & ~1u) < BK7258_OTA_AP_XIP ||
      (ap_reset & ~1u) >= BK7258_OTA_AP_XIP + parsed->ap_length ||
      memcmp(cp_magic, "BK7236\0\0", 8) != 0)
    {
      return -EBADMSG;
    }

  return 0;
}

static int validate_source(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  uint32_t expected_physical_start,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_descriptor_s *parsed)
{
  uint8_t *data = scratch;
  uint8_t *hash_area = scratch + BK7258_OTA_HASH_BUFFER_OFFSET;
  uint8_t *physical_context;
  uint8_t *logical_context;
  uint8_t *cp_context;
  uint8_t *ap_context;
  uint8_t physical_sha[BK7258_OTA_STAGE_SHA256_SIZE];
  uint8_t logical_sha[BK7258_OTA_STAGE_SHA256_SIZE];
  uint8_t cp_sha[BK7258_OTA_STAGE_SHA256_SIZE];
  uint8_t ap_sha[BK7258_OTA_STAGE_SHA256_SIZE];
  uint8_t cp_vector[8] = {0};
  uint8_t ap_vector[8] = {0};
  uint8_t cp_magic[8] = {0};
  size_t context_stride;
  uint32_t expected_body_crc;
  uint32_t expected_body_fnv;
  uint32_t body_crc = 0xffffffffu;
  uint32_t body_fnv = BK7258_OTA_FNV_OFFSET;
  uint32_t physical_offset;
  uint32_t logical_offset;
  uint32_t chunk_size;
  uint32_t packet_offset;
  uint32_t index;
  int ret;

  if (source == NULL || source->read == NULL || hash_ops == NULL ||
      hash_ops->init == NULL || hash_ops->update == NULL ||
      hash_ops->final == NULL || hash_ops->context_size == 0 ||
      hash_ops->context_size > BK7258_OTA_HASH_CONTEXT_MAX ||
      scratch == NULL || scratch_size < BK7258_OTA_STAGE_SCRATCH_SIZE)
    {
      return -EINVAL;
    }

  ret = parse_descriptor(descriptor, expected, expected_physical_start,
                         parsed);
  if (ret < 0)
    {
      return ret;
    }

  if (source->size != BK7258_OTA_PHYSICAL_SIZE)
    {
      return -EINVAL;
    }

  ret = validate_rbl_header(source, parsed, data, &expected_body_crc,
                            &expected_body_fnv);
  if (ret < 0)
    {
      return ret;
    }

  parsed->body_crc32 = expected_body_crc;
  parsed->body_fnv1a = expected_body_fnv;
  context_stride = (hash_ops->context_size + BK7258_OTA_HASH_ALIGNMENT - 1u) &
                   ~(BK7258_OTA_HASH_ALIGNMENT - 1u);
  if (BK7258_OTA_HASH_BUFFER_OFFSET + 4u * context_stride > scratch_size)
    {
      return -EINVAL;
    }

  physical_context = hash_area;
  logical_context = physical_context + context_stride;
  cp_context = logical_context + context_stride;
  ap_context = cp_context + context_stride;
  hash_ops->init(physical_context);
  hash_ops->init(logical_context);
  hash_ops->init(cp_context);
  hash_ops->init(ap_context);

  for (physical_offset = 0; physical_offset < source->size;
       physical_offset += chunk_size)
    {
      chunk_size = source->size - physical_offset;
      if (chunk_size > BK7258_OTA_DATA_BUFFER_SIZE)
        {
          chunk_size = (BK7258_OTA_DATA_BUFFER_SIZE /
                        BK7258_OTA_PACKET_SIZE) * BK7258_OTA_PACKET_SIZE;
        }

      ret = source_read(source, physical_offset, data, chunk_size);
      if (ret < 0)
        {
          return ret;
        }

      hash_ops->update(physical_context, data, chunk_size);
      for (packet_offset = 0; packet_offset < chunk_size;
           packet_offset += BK7258_OTA_PACKET_SIZE)
        {
          const uint8_t *packet = data + packet_offset;
          uint16_t stored_crc = ((uint16_t)packet[32] << 8) | packet[33];
          uint32_t body_bytes;

          if (crc16_packet(packet) != stored_crc)
            {
              return -EBADMSG;
            }

          logical_offset = ((physical_offset + packet_offset) /
                            BK7258_OTA_PACKET_SIZE) *
                           BK7258_OTA_PACKET_DATA;
          hash_ops->update(logical_context, packet,
                           BK7258_OTA_PACKET_DATA);
          hash_overlap(hash_ops, cp_context, BK7258_OTA_CP_LOGICAL_OFFSET,
                       parsed->cp_length, logical_offset, packet,
                       BK7258_OTA_PACKET_DATA);
          hash_overlap(hash_ops, ap_context, BK7258_OTA_AP_LOGICAL_OFFSET,
                       parsed->ap_length, logical_offset, packet,
                       BK7258_OTA_PACKET_DATA);

          copy_overlap(cp_vector, BK7258_OTA_CP_LOGICAL_OFFSET, 8,
                       logical_offset, packet, BK7258_OTA_PACKET_DATA);
          copy_overlap(ap_vector, BK7258_OTA_AP_LOGICAL_OFFSET, 8,
                       logical_offset, packet, BK7258_OTA_PACKET_DATA);
          copy_overlap(cp_magic, BK7258_OTA_CP_LOGICAL_OFFSET + 0x100u, 8,
                       logical_offset, packet, BK7258_OTA_PACKET_DATA);

          body_bytes = 0;
          if (logical_offset < parsed->body_size)
            {
              body_bytes = parsed->body_size - logical_offset;
              if (body_bytes > BK7258_OTA_PACKET_DATA)
                {
                  body_bytes = BK7258_OTA_PACKET_DATA;
                }

              body_crc = crc32_update(body_crc, packet, body_bytes);
              for (index = 0; index < body_bytes; index++)
                {
                  body_fnv = (body_fnv ^ packet[index]) *
                             BK7258_OTA_FNV_PRIME;
                }
            }

          for (index = 0; index < BK7258_OTA_PACKET_DATA; index++)
            {
              uint32_t position = logical_offset + index;
              bool payload = position < parsed->cp_length ||
                (position >= BK7258_OTA_AP_LOGICAL_OFFSET &&
                 position < BK7258_OTA_AP_LOGICAL_OFFSET +
                            parsed->ap_length) ||
                (position >= BK7258_OTA_RBL_HEADER_OFFSET &&
                 position < BK7258_OTA_RBL_HEADER_OFFSET +
                            BK7258_OTA_RBL_HEADER_SIZE);

              if (!payload && packet[index] != 0xffu)
                {
                  return -EBADMSG;
                }
            }
        }
    }

  hash_ops->final(physical_context, physical_sha);
  hash_ops->final(logical_context, logical_sha);
  hash_ops->final(cp_context, cp_sha);
  hash_ops->final(ap_context, ap_sha);
  body_crc ^= 0xffffffffu;

  if (memcmp(physical_sha, parsed->physical_sha,
             BK7258_OTA_STAGE_SHA256_SIZE) != 0 ||
      memcmp(logical_sha, parsed->logical_sha,
             BK7258_OTA_STAGE_SHA256_SIZE) != 0 ||
      memcmp(cp_sha, parsed->cp_sha, BK7258_OTA_STAGE_SHA256_SIZE) != 0 ||
      memcmp(ap_sha, parsed->ap_sha, BK7258_OTA_STAGE_SHA256_SIZE) != 0 ||
      body_crc != parsed->body_crc32 || body_fnv != parsed->body_fnv1a)
    {
      return -EBADMSG;
    }

  return validate_vectors(parsed, cp_vector, ap_vector, cp_magic);
}

static bool deadline_expired(const struct bk7258_ota_flash_ops_s *ops,
                             uint64_t start_ms, uint32_t timeout_ms)
{
  return ops->now_ms(ops->arg) - start_ms >= timeout_ms;
}

static uint32_t deadline_remaining(
  const struct bk7258_ota_flash_ops_s *ops, uint64_t start_ms,
  uint32_t timeout_ms)
{
  uint64_t elapsed = ops->now_ms(ops->arg) - start_ms;

  if (elapsed >= timeout_ms)
    {
      return 0;
    }

  return timeout_ms - (uint32_t)elapsed;
}

static int flash_result(int ret)
{
  return ret < 0 ? ret : (ret == 0 ? 0 : -EIO);
}

static void set_failure(struct bk7258_ota_stage_result_s *result, int status,
                        enum bk7258_ota_stage_phase_e phase)
{
  result->status = status;
  result->phase = phase;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ota_core_validate(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_stage_result_s *result)
{
  struct bk7258_ota_descriptor_s parsed;
  int ret;

  if (result == NULL)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  result->phase = BK7258_OTA_STAGE_PREFLIGHT;
  ret = validate_source(descriptor, expected, BK7258_OTA_PHYSICAL_START,
                        source, hash_ops, scratch, scratch_size, &parsed);
  if (ret < 0)
    {
      set_failure(result, ret, BK7258_OTA_STAGE_PREFLIGHT);
      return ret;
    }

  result->generation = parsed.generation;
  result->status = 0;
  result->phase = BK7258_OTA_STAGE_DONE;
  return 0;
}

int bk7258_ota_core_validate_at(
  uint32_t physical_start,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_stage_result_s *result)
{
  struct bk7258_ota_descriptor_s parsed;
  int ret;

  if (result == NULL)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  result->phase = BK7258_OTA_STAGE_PREFLIGHT;
  ret = validate_source(descriptor, expected, physical_start, source,
                        hash_ops, scratch, scratch_size, &parsed);
  if (ret < 0)
    {
      set_failure(result, ret, BK7258_OTA_STAGE_PREFLIGHT);
      return ret;
    }

  result->generation = parsed.generation;
  result->status = 0;
  result->phase = BK7258_OTA_STAGE_DONE;
  return 0;
}

static int bk7258_ota_core_stage_at_internal(
  uint32_t physical_start, uint32_t active_physical_start,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct bk7258_ota_flash_ops_s *flash_ops,
  uint32_t timeout_ms, uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_stage_result_s *result)
{
  struct bk7258_ota_descriptor_s parsed;
  uint8_t *data = scratch;
  uint8_t *verify = scratch + BK7258_OTA_DATA_BUFFER_SIZE;
  uint8_t *hash_context = scratch + BK7258_OTA_HASH_BUFFER_OFFSET;
  uint8_t observed_sha[BK7258_OTA_STAGE_SHA256_SIZE];
  uint64_t start_ms;
  uint32_t sector_offset;
  uint32_t chunk_offset;
  uint32_t remaining;
  int ret;
  bool locked;

  if (result == NULL ||
      (physical_start != BK7258_ROLE_SLOT_A_CP_OFFSET &&
       physical_start != BK7258_ROLE_SLOT_B_PAIR_OFFSET) ||
      (active_physical_start != BK7258_ROLE_SLOT_A_CP_OFFSET &&
       active_physical_start != BK7258_ROLE_SLOT_B_PAIR_OFFSET) ||
      physical_start == active_physical_start ||
      flash_ops == NULL || flash_ops->now_ms == NULL ||
      flash_ops->compile_write_enabled == NULL ||
      flash_ops->runtime_write_enabled == NULL || flash_ops->lock == NULL ||
      flash_ops->unlock == NULL || flash_ops->erase_sector == NULL ||
      flash_ops->write == NULL || flash_ops->read == NULL ||
      timeout_ms == 0)
    {
      return -EINVAL;
    }

  memset(result, 0, sizeof(*result));
  result->phase = BK7258_OTA_STAGE_PREFLIGHT;
  ret = validate_source(descriptor, expected, physical_start,
                        source, hash_ops, scratch, scratch_size, &parsed);
  if (ret < 0)
    {
      set_failure(result, ret, BK7258_OTA_STAGE_PREFLIGHT);
      return ret;
    }

  result->generation = parsed.generation;
  if (!flash_ops->compile_write_enabled(flash_ops->arg) ||
      !flash_ops->runtime_write_enabled(flash_ops->arg))
    {
      set_failure(result, -EACCES, BK7258_OTA_STAGE_PREFLIGHT);
      return -EACCES;
    }

  if (hash_ops->context_size > BK7258_OTA_HASH_CONTEXT_MAX ||
      scratch_size < BK7258_OTA_STAGE_SCRATCH_SIZE)
    {
      set_failure(result, -EINVAL, BK7258_OTA_STAGE_PREFLIGHT);
      return -EINVAL;
    }

  start_ms = flash_ops->now_ms(flash_ops->arg);
  for (sector_offset = 0; sector_offset < BK7258_OTA_PHYSICAL_SIZE;
       sector_offset += BK7258_OTA_SECTOR_SIZE)
    {
      if (deadline_expired(flash_ops, start_ms, timeout_ms))
        {
          set_failure(result, -ETIMEDOUT, result->phase);
          return -ETIMEDOUT;
        }

      ret = source_read(source, sector_offset, data,
                        BK7258_OTA_SECTOR_SIZE);
      if (ret < 0)
        {
          set_failure(result, ret, BK7258_OTA_STAGE_PREFLIGHT);
          return ret;
        }

      remaining = deadline_remaining(flash_ops, start_ms, timeout_ms);
      if (remaining == 0)
        {
          set_failure(result, -ETIMEDOUT, result->phase);
          return -ETIMEDOUT;
        }

      ret = flash_ops->lock(flash_ops->arg, remaining);
      if (ret != 0)
        {
          ret = flash_result(ret);
          set_failure(result, ret, result->phase);
          return ret;
        }

      locked = true;
      if (!flash_ops->runtime_write_enabled(flash_ops->arg))
        {
          ret = -EACCES;
          goto sector_failure;
        }

      result->phase = BK7258_OTA_STAGE_ERASE;
      if (!flash_ops->runtime_write_enabled(flash_ops->arg))
        {
          ret = -EACCES;
          goto sector_failure;
        }

      ret = flash_result(flash_ops->erase_sector(
        flash_ops->arg, physical_start + sector_offset));
      if (ret < 0)
        {
          goto sector_failure;
        }

      result->sectors_erased++;
      for (chunk_offset = 0; chunk_offset < BK7258_OTA_SECTOR_SIZE;
           chunk_offset += BK7258_OTA_VERIFY_BUFFER_SIZE)
        {
          if (deadline_expired(flash_ops, start_ms, timeout_ms))
            {
              ret = -ETIMEDOUT;
              goto sector_failure;
            }

          ret = flash_result(flash_ops->read(
            flash_ops->arg,
            physical_start + sector_offset + chunk_offset,
            verify, BK7258_OTA_VERIFY_BUFFER_SIZE));
          if (ret < 0)
            {
              goto sector_failure;
            }

          for (remaining = 0; remaining < BK7258_OTA_VERIFY_BUFFER_SIZE;
               remaining++)
            {
              if (verify[remaining] != 0xffu)
                {
                  ret = -EIO;
                  goto sector_failure;
                }
            }
        }

      result->phase = BK7258_OTA_STAGE_PROGRAM;
      for (chunk_offset = 0; chunk_offset < BK7258_OTA_SECTOR_SIZE;
           chunk_offset += BK7258_OTA_PROGRAM_SIZE)
        {
          if (deadline_expired(flash_ops, start_ms, timeout_ms))
            {
              ret = -ETIMEDOUT;
              goto sector_failure;
            }

          if (!flash_ops->runtime_write_enabled(flash_ops->arg))
            {
              ret = -EACCES;
              goto sector_failure;
            }

          ret = flash_result(flash_ops->write(
            flash_ops->arg,
            physical_start + sector_offset + chunk_offset,
            data + chunk_offset, BK7258_OTA_PROGRAM_SIZE));
          if (ret < 0)
            {
              goto sector_failure;
            }

          result->bytes_programmed += BK7258_OTA_PROGRAM_SIZE;
          result->phase = BK7258_OTA_STAGE_READBACK;
          ret = flash_result(flash_ops->read(
            flash_ops->arg,
            physical_start + sector_offset + chunk_offset,
            verify, BK7258_OTA_PROGRAM_SIZE));
          if (ret < 0 ||
              memcmp(verify, data + chunk_offset,
                     BK7258_OTA_PROGRAM_SIZE) != 0)
            {
              ret = ret < 0 ? ret : -EIO;
              goto sector_failure;
            }

          result->bytes_readback += BK7258_OTA_PROGRAM_SIZE;
          result->phase = BK7258_OTA_STAGE_PROGRAM;
        }

      flash_ops->unlock(flash_ops->arg);
      locked = false;
      continue;

sector_failure:
      if (locked)
        {
          flash_ops->unlock(flash_ops->arg);
        }

      set_failure(result, ret, result->phase);
      return ret;
    }

  result->phase = BK7258_OTA_STAGE_FINAL_DIGEST;
  hash_ops->init(hash_context);
  for (sector_offset = 0; sector_offset < BK7258_OTA_PHYSICAL_SIZE;
       sector_offset += BK7258_OTA_SECTOR_SIZE)
    {
      remaining = deadline_remaining(flash_ops, start_ms, timeout_ms);
      if (remaining == 0)
        {
          set_failure(result, -ETIMEDOUT,
                      BK7258_OTA_STAGE_FINAL_DIGEST);
          return -ETIMEDOUT;
        }

      ret = flash_ops->lock(flash_ops->arg, remaining);
      if (ret != 0)
        {
          ret = flash_result(ret);
          set_failure(result, ret, BK7258_OTA_STAGE_FINAL_DIGEST);
          return ret;
        }

      ret = flash_result(flash_ops->read(
        flash_ops->arg, physical_start + sector_offset,
        data, BK7258_OTA_SECTOR_SIZE));
      flash_ops->unlock(flash_ops->arg);
      if (ret < 0)
        {
          set_failure(result, ret, BK7258_OTA_STAGE_FINAL_DIGEST);
          return ret;
        }

      hash_ops->update(hash_context, data, BK7258_OTA_SECTOR_SIZE);
    }

  hash_ops->final(hash_context, observed_sha);
  memcpy(result->slot_sha256, observed_sha, sizeof(result->slot_sha256));
  if (memcmp(observed_sha, parsed.physical_sha,
             BK7258_OTA_STAGE_SHA256_SIZE) != 0)
    {
      set_failure(result, -EBADMSG, BK7258_OTA_STAGE_FINAL_DIGEST);
      return -EBADMSG;
    }

  result->status = 0;
  result->phase = BK7258_OTA_STAGE_DONE;
  return 0;
}

int bk7258_ota_core_stage(
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct bk7258_ota_flash_ops_s *flash_ops,
  uint32_t timeout_ms, uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_stage_result_s *result)
{
  return bk7258_ota_core_stage_at_internal(
    BK7258_ROLE_SLOT_B_PAIR_OFFSET, BK7258_ROLE_SLOT_A_CP_OFFSET,
    descriptor, expected, source, hash_ops, flash_ops, timeout_ms, scratch,
    scratch_size, result);
}

int bk7258_ota_core_stage_inactive(
  uint32_t physical_start, uint32_t active_physical_start,
  const uint8_t descriptor[BK7258_OTA_STAGE_DESCRIPTOR_SIZE],
  const struct bk7258_ota_expected_s *expected,
  const struct bk7258_ota_source_s *source,
  const struct bk7258_ota_hash_ops_s *hash_ops,
  const struct bk7258_ota_flash_ops_s *flash_ops,
  uint32_t timeout_ms, uint8_t *scratch, size_t scratch_size,
  struct bk7258_ota_stage_result_s *result)
{
  return bk7258_ota_core_stage_at_internal(
    physical_start, active_physical_start, descriptor, expected, source,
    hash_ops, flash_ops, timeout_ms, scratch, scratch_size, result);
}
