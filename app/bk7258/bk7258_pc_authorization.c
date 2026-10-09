/****************************************************************************
 * app/bk7258/bk7258_pc_authorization.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_pc_authorization.h"
#include "bk7258_pc_grants.h"
#include "bk7258_provision_storage.h"
#include <errno.h>
#include <string.h>
#include <mbedtls/platform_util.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t get64(const uint8_t *p)
{
  uint64_t value = 0;
  for (unsigned int i = 0; i < 8; i++)
    {
      value = (value << 8) | p[i];
    }

  return value;
}

static uint32_t get32(const uint8_t *p)
{
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 | p[3];
}

static void put64(uint8_t *p, uint64_t value)
{
  for (int i = 7; i >= 0; i--)
    {
      p[i] = value;
      value >>= 8;
    }
}

static void put32(uint8_t *p, uint32_t value)
{
  for (int i = 3; i >= 0; i--)
    {
      p[i] = value;
      value >>= 8;
    }
}

static bool zero(const uint8_t *p, size_t size)
{
  uint8_t bits = 0;
  for (size_t i = 0; i < size; i++)
    {
      bits |= p[i];
    }

  return bits == 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkpc_authorization_control(uint64_t revision,
                               enum bkcontrol_command_e command,
                               uint32_t offset, const uint8_t *record,
                               size_t size,
                               struct bkcontrol_status_s *status)
{
  if (!status)
    {
      return -EINVAL;
    }

  if (command == BKCONTROL_CONFIG_READ)
    {
      uint8_t data[64];
      size_t total = sizeof(data);

      memset(data, 0, sizeof(data));
      memcpy(data, "PCS1", 4);
      if (size)
        {
          if (!record || size != 16 || zero(record, 16))
            {
              return -EINVAL;
            }

          if (offset % 16 || offset >= 32)
            {
              return -ERANGE;
            }

          memcpy(data, "PCR1", 4);
          int result;
          int phase = bkprov_storage_pc_receipt(revision, record, &result);
          put32(data + 4, (uint32_t)(phase < 0 ? phase : result));
          put32(data + 8, phase < 0 ? 0 : (uint32_t)phase);
          memcpy(data + 16, record, 16);
          total = 32;
        }

      else
        {
          struct bkprov_pc_snapshot_s view;
          if (record)
            {
              return -EINVAL;
            }

          if (offset % 16 || offset >= sizeof(data))
            {
              return -ERANGE;
            }

          int ret = bkprov_storage_pc_snapshot(revision, &view);
          if (ret < 0)
            {
              return ret;
            }

          put32(data + 4, view.capabilities ? 1 : 0);
          put64(data + 8, revision);
          put64(data + 16, view.revision);
          put32(data + 24, view.capabilities);
          memcpy(data + 32, view.client, 16);
          memcpy(data + 48, view.transaction, 16);
          mbedtls_platform_zeroize(&view, sizeof(view));
        }

      status->config_total = total;
      memcpy(status->config_chunk, data + offset, 16);
      return 0;
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return offset || record ? -EINVAL : size == 88 ? 0 : -EMSGSIZE;
    }

  if (command != BKCONTROL_CONFIG_APPLY || offset || !record || size != 88 ||
      memcmp(record, "PCW1", 4))
    {
      return -EINVAL;
    }

  if (get64(record + 4) != revision)
    {
      return -ESTALE;
    }

  uint32_t caps = get32(record + 84);
  if ((caps & ~BKPC_CAP_ALL) || zero(record + 20, 16) ||
      (caps && (zero(record + 36, 16) || zero(record + 52, 32))) ||
      (!caps && (!zero(record + 36, 16) || !zero(record + 52, 32))))
    {
      return -EINVAL;
    }

  return bkprov_storage_pc_set(revision, get64(record + 12), record + 20,
                               caps ? record + 36 : NULL,
                               caps ? record + 52 : NULL, caps);
}
