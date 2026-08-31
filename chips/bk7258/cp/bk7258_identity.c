/****************************************************************************
 * chips/bk7258/cp/bk7258_identity.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned, read-only adapter for the v3.1.1.9 UID service.  The immutable
 * SDK reads eight OTP bytes and hashes its normalized 16-byte input with
 * SHA-256.  Applications receive only that 32-byte derived identity; raw OTP
 * data and all OTP/eFuse write paths remain outside this API.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_IDENTITY) && !defined(CONFIG_BK7258_AP_CORE)

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_identity.h>

#include <common/bk_err.h>
#include <components/bk_uid.h>

static mutex_t g_bk7258_identity_lock = NXMUTEX_INITIALIZER;
static uint8_t g_bk7258_identity[BK7258_IDENTITY_BYTES];
static bool g_bk7258_identity_ready;

int bk7258_identity_read(FAR uint8_t identity[BK7258_IDENTITY_BYTES])
{
  uint8_t candidate[BK7258_IDENTITY_BYTES];
  bk_err_t result;
  int ret;

  if (identity == NULL)
    {
      return -EINVAL;
    }

  memset(candidate, 0, sizeof(candidate));

  ret = nxmutex_lock(&g_bk7258_identity_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_bk7258_identity_ready)
    {
      result = bk_uid_driver_init();
      if (result != BK_OK)
        {
          ret = -EIO;
          goto out;
        }

      result = bk_uid_get_data(candidate);
      if (result != BK_OK)
        {
          ret = -EIO;
          goto out;
        }

      memcpy(g_bk7258_identity, candidate, sizeof(candidate));
      g_bk7258_identity_ready = true;
    }

  memcpy(identity, g_bk7258_identity, BK7258_IDENTITY_BYTES);
  ret = OK;

out:
  explicit_bzero(candidate, sizeof(candidate));
  nxmutex_unlock(&g_bk7258_identity_lock);
  return ret;
}

#endif
