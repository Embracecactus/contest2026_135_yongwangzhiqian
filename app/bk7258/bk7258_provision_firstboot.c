/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_provision_firstboot.h"
#include "bk7258_provision_keygen.h"
#include "bk7258_provision_storage.h"

#include <errno.h>
#include <syslog.h>
#include <nuttx/clock.h>
#include <mbedtls/platform_util.h>

/* How long the CP may take to publish its factory transaction mirror before a
 * missing mirror is read as "this device was not prepared by a deployment".
 * The window only covers the boot race between the two cores; it never turns a
 * missing authorization into an accepted one.
 */
#define BKPROV_FIRSTBOOT_MIRROR_WINDOW_MS 30000u

/* Factory states published by the CP record mirror. The names are part of the
 * mirror contract, not of the AP's own state machine.
 */
#define BKPROV_FACTORY_STORAGE_READY 2u

static uint8_t g_candidate[8192];
static size_t g_candidate_size;
static bool g_candidate_valid;
static enum bkprov_firstboot_state_e g_state = BKPROV_FIRSTBOOT_PENDING;
static bool g_reported;
static clock_t g_first_probe;

enum bkprov_firstboot_state_e bkprov_firstboot_state(void)
{
  return g_state;
}

const char *bkprov_firstboot_state_name(enum bkprov_firstboot_state_e state)
{
  switch (state)
    {
      case BKPROV_FIRSTBOOT_PENDING: return "pending";
      case BKPROV_FIRSTBOOT_AUTHORIZED: return "authorized";
      case BKPROV_FIRSTBOOT_GENERATING: return "generating";
      case BKPROV_FIRSTBOOT_READY: return "ready";
      case BKPROV_FIRSTBOOT_UNAUTHORIZED: return "unauthorized";
      default: return "fault";
    }
}

static void firstboot_report(enum bkprov_firstboot_state_e state, int result)
{
  if (g_reported && state == g_state)
    {
      return;
    }

  g_state = state;
  g_reported = true;

  if (state == BKPROV_FIRSTBOOT_UNAUTHORIZED)
    {
      syslog(LOG_WARNING,
             "BKVOICE first boot state=%s result=%d identity=refused\n",
             bkprov_firstboot_state_name(state), result);
    }
  else
    {
      syslog(LOG_INFO, "BKVOICE first boot state=%s result=%d\n",
             bkprov_firstboot_state_name(state), result);
    }
}

int bkprov_firstboot_identity(void *record, size_t capacity, size_t *size)
{
  uint32_t factory_state = 0;
  size_t published = 0;
  int ret;

  if (record == NULL || size == NULL)
    {
      return -EINVAL;
    }

  /* An identity that is already there is always used as-is: a published
   * identity is never replaced, not even by an authorized factory record.
   */
  ret = bkprov_storage_identity(record, capacity, &published);
  if (ret == 0)
    {
      *size = published;
      if (g_state != BKPROV_FIRSTBOOT_READY)
        {
          firstboot_report(BKPROV_FIRSTBOOT_READY, 0);
        }

      return 0;
    }

  if (ret != -ENOENT)
    {
      if (ret != -EAGAIN)
        {
          firstboot_report(BKPROV_FIRSTBOOT_FAULT, ret);
        }

      return ret;
    }

  /* No identity yet: only an explicit factory transaction may create one. */
  ret = bkprov_storage_factory(&factory_state, NULL, NULL, NULL);
  if (ret == -ENOENT)
    {
      /* The two cores race at boot, so a mirror that is still missing inside
       * the bounded window is treated as "not known yet": re-read the store and
       * report busy rather than concluding that this device is unauthorized.
       */
      if (g_first_probe == 0u)
        {
          g_first_probe = clock_systime_ticks();
        }

      if (TICK2MSEC(clock_systime_ticks() - g_first_probe) <
          BKPROV_FIRSTBOOT_MIRROR_WINDOW_MS)
        {
          (void)bkprov_storage_refresh();
          return -EBUSY;
        }
    }

  if (ret == -EAGAIN)
    {
      return -EBUSY;
    }

  if (ret < 0 || factory_state != BKPROV_FACTORY_STORAGE_READY)
    {
      /* Absent mirror outside the window, damaged mirror, or a state that never
       * authorized an identity: refuse and stay refused.
       */
      firstboot_report(BKPROV_FIRSTBOOT_UNAUTHORIZED, ret < 0 ? ret : 0);
      return -ENOENT;
    }

  if (!g_candidate_valid)
    {
      size_t candidate_size = 0;

      firstboot_report(BKPROV_FIRSTBOOT_AUTHORIZED, 0);
      ret = bkprov_identity_generate(g_candidate, sizeof(g_candidate),
                                     &candidate_size);
      if (ret < 0)
        {
          firstboot_report(BKPROV_FIRSTBOOT_FAULT, ret);
          mbedtls_platform_zeroize(g_candidate, sizeof(g_candidate));
          return ret;
        }

      g_candidate_size = candidate_size;
      g_candidate_valid = true;
    }

  /* The candidate is submitted once and then polled: a retry never generates
   * a second key pair for the same device.
   */
  ret = bkprov_storage_identity_install(g_candidate, g_candidate_size);
  if (ret < 0 && ret != -EAGAIN)
    {
      firstboot_report(BKPROV_FIRSTBOOT_FAULT, ret);
      mbedtls_platform_zeroize(g_candidate, sizeof(g_candidate));
      g_candidate_valid = false;
      g_candidate_size = 0;
      return ret;
    }

  published = 0;
  ret = bkprov_storage_identity(record, capacity, &published);
  if (ret == 0)
    {
      *size = published;
      mbedtls_platform_zeroize(g_candidate, sizeof(g_candidate));
      g_candidate_valid = false;
      g_candidate_size = 0;
      firstboot_report(BKPROV_FIRSTBOOT_READY, (int)published);
      return 0;
    }

  if (ret == -ENOENT)
    {
      firstboot_report(BKPROV_FIRSTBOOT_GENERATING, 0);
      return -EBUSY;
    }

  return ret;
}
