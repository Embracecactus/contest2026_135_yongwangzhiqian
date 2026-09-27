/****************************************************************************
 * app/bk7258/bk7258_pc_authorization_owner.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_pc_authorization_owner.h"
#include "bk7258_provision_owner.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_storage.h"
#include <errno.h>
#include <mbedtls/platform_util.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint64_t g_revision;
static int g_error = -ENOKEY;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bkpc_authorization_unbind(void)
{
  g_revision = 0;
  g_error = -ENOKEY;
}

int bkpc_authorization_prepare(uint64_t revision, const void *bundle,
                               size_t size)
{
  struct bkprov_settings_s settings;
  int ret;

  ret = bkprov_settings_decode(&settings, bundle, size);
  if (ret == 0 && !bkprov_owner_control_matches(settings.control_key))
    {
      ret = -EACCES;
    }

  if (ret == 0)
    {
      ret = bkprov_storage_pc_load(revision, settings.control_key);
    }

  mbedtls_platform_zeroize(&settings, sizeof(settings));
  if (ret == 0 || ret == -EAGAIN)
    {
      g_revision = revision;
      g_error = 0;
    }
  else
    {
      g_revision = 0;
      g_error = ret;
    }

  return ret;
}

int bkpc_authorization_current(enum bkcontrol_command_e command,
                               uint32_t offset, const uint8_t *record,
                               size_t size,
                               struct bkcontrol_status_s *status)
{
  if (g_error < 0)
    {
      return g_error;
    }

  return bkpc_authorization_control(g_revision, command, offset,
                                    record, size, status);
}
