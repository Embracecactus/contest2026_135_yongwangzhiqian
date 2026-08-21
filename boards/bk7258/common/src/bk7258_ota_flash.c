/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/src/bk7258_ota_flash.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared fail-closed Flash primitives for the new BK7258 OTA domains.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <arch/chip/bk7258_image_layout.h>

#include <driver/flash.h>

#include "bk7258_ota_flash_internal.h"

#define BK7258_OTA_FLASH_ID_C86517 0x00c86517u

static bool g_bk7258_ota_flash_ready;

int bk7258_ota_flash_initialize(void)
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

int bk7258_ota_flash_verify(uint32_t address, const uint8_t *expected,
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

uint16_t bk7258_ota_flash_crc16(const uint8_t *data)
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
