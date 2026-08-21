/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_ota.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned, pair-atomic BK7258 MCUboot update staging and confirmation.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <arch/board/bk7258_boot_slot.h>
#include <arch/board/bk7258_image_layout.h>
#include <arch/board/bk7258_ota.h>

#include <driver/flash.h>

#include "bk7258_flash_guard.h"

#define BK7258_OTA_FLASH_ID_C86517 0x00c86517u
#define BK7258_OTA_TRAILER_MAGIC_SIZE 16u
#define BK7258_OTA_MAX_ALIGN 8u
#define BK7258_OTA_COPY_DONE_OFFSET(size) \
  ((size) - BK7258_OTA_TRAILER_MAGIC_SIZE - 2u * BK7258_OTA_MAX_ALIGN)
#define BK7258_OTA_IMAGE_OK_OFFSET(size) \
  ((size) - BK7258_OTA_TRAILER_MAGIC_SIZE - BK7258_OTA_MAX_ALIGN)

static const uint8_t g_bk7258_ota_magic[BK7258_OTA_TRAILER_MAGIC_SIZE] =
{
  0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f,
  0x35, 0x52, 0x50, 0x0f, 0x2c, 0xb6, 0x79, 0x80
};

static uint8_t g_bk7258_ota_sector[BK7258_FLASH_ERASE_SIZE];
static bool g_bk7258_ota_flash_ready;

static uint16_t bk7258_ota_crc16(const uint8_t *data)
{
  uint16_t crc = 0xffffu;
  uint32_t index;
  uint32_t bit;

  for (index = 0; index < BK7258_FLASH_CRC_DATA_SIZE; index++)
    {
      crc ^= (uint16_t)data[index] << 8;
      for (bit = 0; bit < 8u; bit++)
        {
          crc = (uint16_t)((crc << 1) ^
            ((crc & 0x8000u) != 0u ? 0x8005u : 0u));
        }
    }

  return crc;
}

static int bk7258_ota_flash_initialize(void)
{
  if (g_bk7258_ota_flash_ready)
    {
      return 0;
    }

  if (bk_flash_driver_init() != BK_OK ||
      (bk_flash_get_id() & 0x00ffffffu) != BK7258_OTA_FLASH_ID_C86517)
    {
      return -ENODEV;
    }

  g_bk7258_ota_flash_ready = true;
  return 0;
}

int bk7258_ota_inactive_geometry(struct bk7258_ota_geometry_s *geometry)
{
  enum bk7258_boot_slot_e active;

  if (geometry == NULL)
    {
      return -EINVAL;
    }

  active = bk7258_boot_active_slot();
  if (active == BK7258_BOOT_SLOT_PRIMARY)
    {
      geometry->active_slot = active;
      geometry->inactive_slot = BK7258_BOOT_SLOT_SECONDARY;
      geometry->cp_raw_offset = BK7258_AB_SECONDARY_CP_RAW_START;
      geometry->ap_raw_offset = BK7258_AB_SECONDARY_AP_RAW_START;
    }
  else if (active == BK7258_BOOT_SLOT_SECONDARY)
    {
      geometry->active_slot = active;
      geometry->inactive_slot = BK7258_BOOT_SLOT_PRIMARY;
      geometry->cp_raw_offset = BK7258_CP_RAW_PHYSICAL_START;
      geometry->ap_raw_offset = BK7258_AP_RAW_PHYSICAL_START;
    }
  else
    {
      return -EIO;
    }

  geometry->cp_raw_size = BK7258_CP_RAW_PHYSICAL_SIZE;
  geometry->ap_raw_size = BK7258_AP_RAW_PHYSICAL_SIZE;
  return 0;
}

static int bk7258_ota_verify(uint32_t address, const uint8_t *expected,
                             uint32_t nbytes)
{
  uint8_t observed[32];
  uint32_t offset;

  for (offset = 0; offset < nbytes; offset += sizeof(observed))
    {
      uint32_t count = nbytes - offset;
      if (count > sizeof(observed))
        {
          count = sizeof(observed);
        }

      if (bk_flash_read_bytes(address + offset, observed, count) != BK_OK ||
          memcmp(observed, expected + offset, count) != 0)
        {
          return -EIO;
        }
    }

  return 0;
}

static int bk7258_ota_erase_image(uint32_t base, uint32_t size)
{
  uint32_t offset;

  for (offset = 0; offset < size; offset += BK7258_FLASH_ERASE_SIZE)
    {
      if (bk_flash_erase_sector(base + offset) != BK_OK)
        {
          return -EIO;
        }
    }

  return 0;
}

static int bk7258_ota_write_sector(bk7258_ota_read_t reader, void *arg,
                                   enum bk7258_ota_image_e image,
                                   uint32_t source_offset,
                                   uint32_t destination)
{
  if (reader(arg, image, source_offset, g_bk7258_ota_sector,
             sizeof(g_bk7258_ota_sector)) != 0 ||
      bk_flash_write_bytes(destination, g_bk7258_ota_sector,
                           sizeof(g_bk7258_ota_sector)) != BK_OK)
    {
      return -EIO;
    }

  return bk7258_ota_verify(destination, g_bk7258_ota_sector,
                           sizeof(g_bk7258_ota_sector));
}

static int bk7258_ota_write_image(bk7258_ota_read_t reader, void *arg,
                                  enum bk7258_ota_image_e image,
                                  uint32_t base, uint32_t size,
                                  uint32_t first_offset)
{
  uint32_t offset;

  for (offset = first_offset; offset < size;
       offset += BK7258_FLASH_ERASE_SIZE)
    {
      int ret = bk7258_ota_write_sector(reader, arg, image, offset,
                                        base + offset);
      if (ret < 0)
        {
          return ret;
        }
    }

  return 0;
}

int bk7258_ota_stage_pair(bk7258_ota_read_t reader, void *arg)
{
  struct bk7258_ota_geometry_s geometry;
  enum bk7258_flash_guard_owner_e owner;
  int ret;

  if (reader == NULL ||
      (ret = bk7258_ota_inactive_geometry(&geometry)) < 0)
    {
      return reader == NULL ? -EINVAL : ret;
    }

  owner = geometry.inactive_slot == BK7258_BOOT_SLOT_PRIMARY ?
          BK7258_FLASH_GUARD_OTA_PRIMARY :
          BK7258_FLASH_GUARD_OTA_SECONDARY;
  ret = bk7258_flash_guard_lock(owner, true, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ota_flash_initialize();

  /* Erasing CP first invalidates the pair header before AP or CP bytes can be
   * changed.  AP is then completed first; CP sector zero is the final commit
   * write, so every reset before that point leaves the old pair bootable and
   * the new pair ineligible for BL2 preselection. */
  if (ret == 0)
    {
      ret = bk7258_ota_erase_image(geometry.cp_raw_offset,
                                   geometry.cp_raw_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_erase_image(geometry.ap_raw_offset,
                                   geometry.ap_raw_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_write_image(reader, arg, BK7258_OTA_IMAGE_AP,
                                   geometry.ap_raw_offset,
                                   geometry.ap_raw_size, 0u);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_write_image(reader, arg, BK7258_OTA_IMAGE_CP,
                                   geometry.cp_raw_offset,
                                   geometry.cp_raw_size,
                                   BK7258_FLASH_ERASE_SIZE);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_write_sector(reader, arg, BK7258_OTA_IMAGE_CP, 0u,
                                    geometry.cp_raw_offset);
    }

  bk7258_flash_guard_unlock();
  return ret;
}

static bool bk7258_ota_trailer_ready(uint32_t xip, uint32_t size)
{
  const volatile uint8_t *image =
    (const volatile uint8_t *)(uintptr_t)xip;
  uint32_t index;

  if (image[BK7258_OTA_COPY_DONE_OFFSET(size)] != 1u)
    {
      return false;
    }

  for (index = 0; index < sizeof(g_bk7258_ota_magic); index++)
    {
      if (image[size - sizeof(g_bk7258_ota_magic) + index] !=
          g_bk7258_ota_magic[index])
        {
          return false;
        }
    }

  return true;
}

static int bk7258_ota_confirm_image(uint32_t raw_base, uint32_t raw_size,
                                    uint32_t logical_size)
{
  uint32_t logical_offset = BK7258_OTA_IMAGE_OK_OFFSET(logical_size);
  uint32_t group = logical_offset / BK7258_FLASH_CRC_DATA_SIZE;
  uint32_t in_group = logical_offset % BK7258_FLASH_CRC_DATA_SIZE;
  uint32_t raw_offset = group * BK7258_FLASH_CRC_TOTAL_SIZE;
  uint32_t sector = raw_base + raw_size - BK7258_FLASH_ERASE_SIZE;
  uint32_t packet = raw_base + raw_offset - sector;
  uint8_t *data;
  uint16_t crc;

  if (raw_offset + BK7258_FLASH_CRC_TOTAL_SIZE > raw_size ||
      packet + BK7258_FLASH_CRC_TOTAL_SIZE > sizeof(g_bk7258_ota_sector) ||
      bk_flash_read_bytes(sector, g_bk7258_ota_sector,
                          sizeof(g_bk7258_ota_sector)) != BK_OK)
    {
      return -EIO;
    }

  data = &g_bk7258_ota_sector[packet];
  crc = ((uint16_t)data[BK7258_FLASH_CRC_DATA_SIZE] << 8) |
        data[BK7258_FLASH_CRC_DATA_SIZE + 1u];
  if (crc != bk7258_ota_crc16(data))
    {
      return -EILSEQ;
    }

  if (data[in_group] == 1u)
    {
      return 0;
    }
  if (data[in_group] != 0xffu)
    {
      return -EILSEQ;
    }

  data[in_group] = 1u;
  crc = bk7258_ota_crc16(data);
  data[BK7258_FLASH_CRC_DATA_SIZE] = (uint8_t)(crc >> 8);
  data[BK7258_FLASH_CRC_DATA_SIZE + 1u] = (uint8_t)crc;

  if (bk_flash_erase_sector(sector) != BK_OK ||
      bk_flash_write_bytes(sector, g_bk7258_ota_sector,
                           sizeof(g_bk7258_ota_sector)) != BK_OK)
    {
      return -EIO;
    }

  return bk7258_ota_verify(sector, g_bk7258_ota_sector,
                           sizeof(g_bk7258_ota_sector));
}

int bk7258_ota_confirm_pair(void)
{
  enum bk7258_boot_slot_e active = bk7258_boot_active_slot();
  enum bk7258_flash_guard_owner_e owner;
  uint32_t cp_raw;
  uint32_t ap_raw;
  int ret;

  if (!bk7258_ota_trailer_ready(BK7258_ARTIFACT_CP_XIP_START,
                                 BK7258_ARTIFACT_CP_LOGICAL_SIZE) ||
      !bk7258_ota_trailer_ready(BK7258_ARTIFACT_AP_XIP_START,
                                 BK7258_ARTIFACT_AP_LOGICAL_SIZE))
    {
      return -EPERM;
    }

  if (active == BK7258_BOOT_SLOT_PRIMARY)
    {
      owner = BK7258_FLASH_GUARD_CONFIRM_PRIMARY;
      cp_raw = BK7258_CP_RAW_PHYSICAL_START;
      ap_raw = BK7258_AP_RAW_PHYSICAL_START;
    }
  else if (active == BK7258_BOOT_SLOT_SECONDARY)
    {
      owner = BK7258_FLASH_GUARD_CONFIRM_SECONDARY;
      cp_raw = BK7258_AB_SECONDARY_CP_RAW_START;
      ap_raw = BK7258_AB_SECONDARY_AP_RAW_START;
    }
  else
    {
      return -EIO;
    }

  ret = bk7258_flash_guard_lock(owner, true, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ota_flash_initialize();

  /* Either confirmation order is crash-safe: BL2 rejects a pair
   * with only one confirmed image.  AP-first keeps CP as the final pair-level
   * health commit marker. */
  if (ret == 0)
    {
      ret = bk7258_ota_confirm_image(ap_raw, BK7258_AP_RAW_PHYSICAL_SIZE,
                                     BK7258_ARTIFACT_AP_LOGICAL_SIZE);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_confirm_image(cp_raw, BK7258_CP_RAW_PHYSICAL_SIZE,
                                     BK7258_ARTIFACT_CP_LOGICAL_SIZE);
    }

  bk7258_flash_guard_unlock();
  return ret;
}
