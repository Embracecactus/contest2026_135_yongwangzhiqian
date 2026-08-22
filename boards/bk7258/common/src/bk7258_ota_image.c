/****************************************************************************
 * boards/bk7258/common/src/bk7258_ota_image.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Read-only MCUboot metadata preflight for active XIP images and finalized
 * BK7258 32+2 CRC physical objects.  BL2 still performs the authoritative
 * signature, protected-counter and boot-state validation.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <arch/chip/bk7258_image_layout.h>
#include <arch/chip/bk7258_ota.h>

#include "bk7258_ota_flash_internal.h"
#include "bk7258_ota_image.h"

typedef int (*bk7258_ota_logical_read_t)(void *context, uint32_t offset,
                                        uint8_t *buffer, size_t nbytes);

struct bk7258_ota_source_reader_s
{
  const struct bk7258_ota_source_ops_s *ops;
  void *context;
  enum bk7258_ota_image_e image;
  uint32_t physical_size;
  uint32_t logical_size;
};

struct bk7258_ota_xip_reader_s
{
  uint32_t xip;
  uint32_t logical_size;
};

static const uint8_t
  g_bk7258_ota_candidate_magic[BK7258_MCUBOOT_TRAILER_MAGIC_SIZE] =
    BK7258_MCUBOOT_TRAILER_MAGIC_INIT;

static int bk7258_ota_source_logical_read(void *context, uint32_t offset,
                                         uint8_t *buffer, size_t nbytes)
{
  struct bk7258_ota_source_reader_s *reader = context;
  uint8_t packet[BK7258_FLASH_CRC_TOTAL_SIZE];
  size_t completed = 0u;

  if (offset > reader->logical_size ||
      nbytes > reader->logical_size - offset)
    {
      return -EOVERFLOW;
    }

  while (completed < nbytes)
    {
      uint32_t logical = offset + (uint32_t)completed;
      uint32_t group = logical / BK7258_FLASH_CRC_DATA_SIZE;
      uint32_t in_group = logical % BK7258_FLASH_CRC_DATA_SIZE;
      uint32_t raw = group * BK7258_FLASH_CRC_TOTAL_SIZE;
      size_t count = BK7258_FLASH_CRC_DATA_SIZE - in_group;
      uint16_t crc;
      int ret;

      if (count > nbytes - completed)
        {
          count = nbytes - completed;
        }
      if (raw > reader->physical_size ||
          BK7258_FLASH_CRC_TOTAL_SIZE > reader->physical_size - raw)
        {
          return -EOVERFLOW;
        }

      ret = reader->ops->read_at(reader->context, reader->image, raw,
                                 packet, sizeof(packet));
      if (ret != 0)
        {
          return ret < 0 ? ret : -EIO;
        }

      crc = ((uint16_t)packet[BK7258_FLASH_CRC_DATA_SIZE] << 8) |
            packet[BK7258_FLASH_CRC_DATA_SIZE + 1u];
      if (crc != bk7258_ota_flash_crc16(packet))
        {
          return -EILSEQ;
        }

      memcpy(buffer + completed, packet + in_group, count);
      completed += count;
    }

  return 0;
}

static int bk7258_ota_xip_logical_read(void *context, uint32_t offset,
                                      uint8_t *buffer, size_t nbytes)
{
  struct bk7258_ota_xip_reader_s *reader = context;

  if (offset > reader->logical_size ||
      nbytes > reader->logical_size - offset)
    {
      return -EOVERFLOW;
    }

  memcpy(buffer, (const void *)(uintptr_t)(reader->xip + offset), nbytes);
  return 0;
}

static int bk7258_ota_image_metadata(
  bk7258_ota_logical_read_t read, void *context, uint32_t logical_size,
  struct bk7258_ota_image_metadata_s *metadata)
{
  struct bk7258_mcuboot_image_header_s header;
  struct bk7258_mcuboot_tlv_info_s info;
  struct bk7258_mcuboot_tlv_s tlv;
  uint32_t protected_offset;
  uint32_t protected_end;
  uint32_t copy_done_offset;
  uint32_t offset;
  bool counter_seen = false;
  int ret;

  if (read == NULL || metadata == NULL ||
      logical_size <= BK7258_MCUBOOT_TRAILER_MAGIC_SIZE +
                      2u * BK7258_MCUBOOT_TRAILER_ALIGN)
    {
      return -EINVAL;
    }

  copy_done_offset = BK7258_MCUBOOT_COPY_DONE_OFFSET(logical_size);
  if (copy_done_offset < sizeof(info))
    {
      return -EINVAL;
    }

  ret = read(context, 0u, (uint8_t *)&header, sizeof(header));
  if (ret < 0)
    {
      return ret;
    }

  if (header.magic != BK7258_MCUBOOT_IMAGE_MAGIC ||
      header.header_size < BK7258_MCUBOOT_IMAGE_HEADER_SIZE ||
      header.header_size > logical_size || header.image_size < 8u ||
      header.image_size > logical_size - header.header_size)
    {
      return -EILSEQ;
    }

  protected_offset = header.header_size + header.image_size;
  if (protected_offset > logical_size ||
      header.protected_tlv_size > logical_size - protected_offset)
    {
      return -EILSEQ;
    }

  protected_end = protected_offset + header.protected_tlv_size;
  if (header.protected_tlv_size != 0u)
    {
      if (header.protected_tlv_size < sizeof(info))
        {
          return -EILSEQ;
        }

      ret = read(context, protected_offset, (uint8_t *)&info,
                 sizeof(info));
      if (ret < 0)
        {
          return ret;
        }
      if (info.magic != BK7258_MCUBOOT_TLV_PROT_INFO_MAGIC ||
          info.total != header.protected_tlv_size)
        {
          return -EILSEQ;
        }

      offset = protected_offset + sizeof(info);
      while (offset < protected_end)
        {
          if (sizeof(tlv) > protected_end - offset)
            {
              return -EILSEQ;
            }
          ret = read(context, offset, (uint8_t *)&tlv, sizeof(tlv));
          if (ret < 0)
            {
              return ret;
            }
          offset += sizeof(tlv);
          if (tlv.length > protected_end - offset)
            {
              return -EILSEQ;
            }

          if (tlv.type == BK7258_MCUBOOT_TLV_SECURITY_COUNTER)
            {
              if (counter_seen || tlv.length != sizeof(uint32_t))
                {
                  return -EILSEQ;
                }
              ret = read(context, offset,
                         (uint8_t *)&metadata->security_counter,
                         sizeof(metadata->security_counter));
              if (ret < 0)
                {
                  return ret;
                }
              counter_seen = true;
            }

          offset += tlv.length;
        }
    }

  metadata->security_counter_present = counter_seen;
  if (protected_end > copy_done_offset - sizeof(info))
    {
      return -EILSEQ;
    }

  ret = read(context, protected_end, (uint8_t *)&info, sizeof(info));
  if (ret < 0)
    {
      return ret;
    }
  if (info.magic != BK7258_MCUBOOT_TLV_INFO_MAGIC ||
      info.total < sizeof(info) ||
      info.total > copy_done_offset - protected_end)
    {
      return -EILSEQ;
    }

  metadata->version = header.version;
  return 0;
}

int bk7258_ota_source_image_metadata(
  const struct bk7258_ota_source_ops_s *ops, void *context,
  enum bk7258_ota_image_e image, uint32_t physical_size,
  uint32_t logical_size, struct bk7258_ota_image_metadata_s *metadata)
{
  struct bk7258_ota_source_reader_s reader;
  uint8_t magic[BK7258_MCUBOOT_TRAILER_MAGIC_SIZE];
  uint8_t copy_done;
  uint8_t image_ok;
  uint32_t expected_physical;
  int ret;

  if (ops == NULL || ops->read_at == NULL || metadata == NULL ||
      image < BK7258_OTA_IMAGE_CP || image > BK7258_OTA_IMAGE_AP ||
      logical_size % BK7258_FLASH_CRC_DATA_SIZE != 0u)
    {
      return -EINVAL;
    }

  expected_physical = logical_size / BK7258_FLASH_CRC_DATA_SIZE *
                      BK7258_FLASH_CRC_TOTAL_SIZE;
  if (physical_size != expected_physical)
    {
      return -EINVAL;
    }

  memset(metadata, 0, sizeof(*metadata));
  reader.ops = ops;
  reader.context = context;
  reader.image = image;
  reader.physical_size = physical_size;
  reader.logical_size = logical_size;
  ret = bk7258_ota_image_metadata(bk7258_ota_source_logical_read, &reader,
                                  logical_size, metadata);
  if (ret == 0)
    {
      ret = bk7258_ota_source_logical_read(
              &reader, BK7258_MCUBOOT_COPY_DONE_OFFSET(logical_size),
              &copy_done, sizeof(copy_done));
    }
  if (ret == 0)
    {
      ret = bk7258_ota_source_logical_read(
              &reader, BK7258_MCUBOOT_IMAGE_OK_OFFSET(logical_size),
              &image_ok, sizeof(image_ok));
    }
  if (ret == 0)
    {
      ret = bk7258_ota_source_logical_read(
              &reader,
              logical_size - BK7258_MCUBOOT_TRAILER_MAGIC_SIZE,
              magic, sizeof(magic));
    }
  if (ret == 0 &&
      (copy_done != 0xffu || image_ok != 0xffu ||
       memcmp(magic, g_bk7258_ota_candidate_magic, sizeof(magic)) != 0))
    {
      ret = -EILSEQ;
    }

  return ret;
}

int bk7258_ota_xip_image_metadata(
  uint32_t xip, uint32_t logical_size,
  struct bk7258_ota_image_metadata_s *metadata)
{
  struct bk7258_ota_xip_reader_s reader;

  if (xip == 0u || metadata == NULL ||
      logical_size % BK7258_FLASH_CRC_DATA_SIZE != 0u)
    {
      return -EINVAL;
    }

  memset(metadata, 0, sizeof(*metadata));
  reader.xip = xip;
  reader.logical_size = logical_size;
  return bk7258_ota_image_metadata(bk7258_ota_xip_logical_read, &reader,
                                   logical_size, metadata);
}
