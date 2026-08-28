/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/cp/bk7258_ota_flash.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Raw-Flash verification and BK7258 32+2 CRC primitives.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <arch/chip/bk7258_flash.h>

#include "bk7258_ota_flash_internal.h"

int bk7258_ota_flash_initialize(void)
{
  return bk7258_flash_initialize();
}

int bk7258_ota_flash_verify(uint32_t address,
                            FAR const uint8_t *expected,
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

      if (bk7258_flash_read(address + offset, observed, count) < 0 ||
          memcmp(observed, expected + offset, count) != 0)
        {
          return -EIO;
        }
    }

  return 0;
}

uint16_t bk7258_ota_flash_crc16(FAR const uint8_t *data)
{
  uint16_t crc = 0xffffu;
  uint32_t index;
  uint32_t bit;

  for (index = 0; index < BK7258_OTA_CRC_DATA_SIZE; index++)
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
