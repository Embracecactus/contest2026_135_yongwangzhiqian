/****************************************************************************
 * chips/bk7258/cp/bk7258_ota_flash.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Raw-Flash verification and BK7258 32+2 CRC primitives.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

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
  int ret;

  for (offset = 0; offset < nbytes; offset += sizeof(observed))
    {
      uint32_t count = nbytes - offset;
      if (count > sizeof(observed))
        {
          count = sizeof(observed);
        }

      ret = bk7258_flash_read(address + offset, observed, count);
      if (ret < 0)
        {
          syslog(LOG_ERR,
                 "BKOTA verify read address=%08lx size=%lu error=%d\n",
                 (unsigned long)(address + offset),
                 (unsigned long)count, ret);
          return ret;
        }

      if (memcmp(observed, expected + offset, count) != 0)
        {
          uint8_t reread[32];
          uint32_t first = 0;
          uint32_t missing_zero_bits = 0;
          uint32_t extra_zero_bits = 0;

          while (first < count && observed[first] == expected[offset + first])
            {
              first++;
            }

          /* Classify a failed write without logging image or persistent-data
           * contents. One bounded reread distinguishes unstable observation;
           * it never turns a failed verify into success or retries a write.
           */

          for (uint32_t i = 0; i < count; i++)
            {
              uint8_t missing = observed[i] & ~expected[offset + i];
              uint8_t extra = expected[offset + i] & ~observed[i];
              for (unsigned int bit = 0; bit < 8; bit++)
                {
                  missing_zero_bits += (missing >> bit) & 1u;
                  extra_zero_bits += (extra >> bit) & 1u;
                }
            }

          ret = bk7258_flash_read(address + offset, reread, count);

          syslog(LOG_ERR, "BKOTA verify mismatch address=%08lx "
                 "missing_zero_bits=%lu extra_zero_bits=%lu reread=%d stable=%d\n",
                 (unsigned long)(address + offset + first),
                 (unsigned long)missing_zero_bits,
                 (unsigned long)extra_zero_bits, ret,
                 ret == 0 && memcmp(observed, reread, count) == 0);
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
