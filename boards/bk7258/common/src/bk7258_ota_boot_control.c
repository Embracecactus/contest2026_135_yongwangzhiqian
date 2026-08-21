/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/
 * bk7258_ota_boot_control.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MCUboot pair geometry and health-confirmation adapter.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#include <arch/chip/bk7258_boot_slot.h>
#include <arch/chip/bk7258_image_layout.h>
#include <arch/chip/bk7258_ota.h>

#include <driver/flash.h>

#include "bk7258_flash_guard.h"
#include "bk7258_ota_flash_internal.h"

#define BK7258_OTA_TRAILER_MAGIC_SIZE 16u
#define BK7258_OTA_MAX_ALIGN          8u
#define BK7258_OTA_COPY_DONE_OFFSET(size) \
  ((size) - BK7258_OTA_TRAILER_MAGIC_SIZE - 2u * BK7258_OTA_MAX_ALIGN)
#define BK7258_OTA_IMAGE_OK_OFFSET(size) \
  ((size) - BK7258_OTA_TRAILER_MAGIC_SIZE - BK7258_OTA_MAX_ALIGN)

static const uint8_t g_bk7258_ota_magic[BK7258_OTA_TRAILER_MAGIC_SIZE] =
{
  0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f,
  0x35, 0x52, 0x50, 0x0f, 0x2c, 0xb6, 0x79, 0x80
};

static uint8_t g_bk7258_ota_confirm_sector[BK7258_FLASH_ERASE_SIZE];

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
      packet + BK7258_FLASH_CRC_TOTAL_SIZE >
        sizeof(g_bk7258_ota_confirm_sector) ||
      bk_flash_read_bytes(sector, g_bk7258_ota_confirm_sector,
                          sizeof(g_bk7258_ota_confirm_sector)) != BK_OK)
    {
      return -EIO;
    }

  data = &g_bk7258_ota_confirm_sector[packet];
  crc = ((uint16_t)data[BK7258_FLASH_CRC_DATA_SIZE] << 8) |
        data[BK7258_FLASH_CRC_DATA_SIZE + 1u];
  if (crc != bk7258_ota_flash_crc16(data))
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
  crc = bk7258_ota_flash_crc16(data);
  data[BK7258_FLASH_CRC_DATA_SIZE] = (uint8_t)(crc >> 8);
  data[BK7258_FLASH_CRC_DATA_SIZE + 1u] = (uint8_t)crc;

  if (bk_flash_erase_sector(sector) != BK_OK ||
      bk_flash_write_bytes(sector, g_bk7258_ota_confirm_sector,
                           sizeof(g_bk7258_ota_confirm_sector)) != BK_OK)
    {
      return -EIO;
    }

  return bk7258_ota_flash_verify(sector, g_bk7258_ota_confirm_sector,
                                 sizeof(g_bk7258_ota_confirm_sector));
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
