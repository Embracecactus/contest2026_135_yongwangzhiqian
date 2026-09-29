/* SPDX-License-Identifier: Apache-2.0 */
/* Volatile factory-only diagnostics principal.  The durable owner/PC grant
 * stores are deliberately not involved. */

#include "bk7258_factory_diagnostics.h"
#include "bk7258_pc_grants.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <nuttx/mutex.h>

struct bkfactory_diagnostics_state_s
{
  mutex_t lock;
  uint8_t certificate[32];
  uint8_t client[16];
  uint8_t key[32];
  uint8_t transaction[16];
  uint64_t binding;
  uint64_t revision;
  uint64_t deadline_ms;
  uint64_t last_ms;
  bool eligible;
  bool active;
  bool used;
  bool have_time;
  bool expired;
};

static struct bkfactory_diagnostics_state_s g_diagnostics =
{
  .lock = NXMUTEX_INITIALIZER,
};

static bool all_zero(const uint8_t *data, size_t size)
{
  uint8_t bits = 0;

  for (size_t i = 0; i < size; i++)
    {
      bits |= data[i];
    }

  return bits == 0;
}

static int monotonic_ms(uint64_t *now_ms)
{
  struct timespec value;

  if (now_ms == NULL)
    {
      return -EINVAL;
    }

  if (clock_gettime(CLOCK_MONOTONIC, &value) < 0 || value.tv_sec < 0 ||
      value.tv_nsec < 0 || value.tv_nsec >= 1000000000L)
    {
      return -EIO;
    }

  if ((uint64_t)value.tv_sec > (UINT64_MAX - 999u) / 1000u)
    {
      return -EOVERFLOW;
    }

  *now_ms = (uint64_t)value.tv_sec * 1000u +
            (uint64_t)value.tv_nsec / 1000000u;
  return 0;
}

static void clear_principal_locked(void)
{
  mbedtls_platform_zeroize(g_diagnostics.client,
                           sizeof(g_diagnostics.client));
  mbedtls_platform_zeroize(g_diagnostics.key, sizeof(g_diagnostics.key));
  mbedtls_platform_zeroize(g_diagnostics.transaction,
                           sizeof(g_diagnostics.transaction));
  g_diagnostics.binding = 0;
  g_diagnostics.revision = 0;
  g_diagnostics.deadline_ms = 0;
  g_diagnostics.active = false;
}

static int advance_locked(uint64_t now_ms)
{
  if (g_diagnostics.have_time && now_ms < g_diagnostics.last_ms)
    {
      if (g_diagnostics.active)
        {
          clear_principal_locked();
          g_diagnostics.used = true;
        }

      return -ERANGE;
    }

  g_diagnostics.last_ms = now_ms;
  g_diagnostics.have_time = true;
  if (g_diagnostics.active && now_ms >= g_diagnostics.deadline_ms)
    {
      clear_principal_locked();
      g_diagnostics.used = true;
      g_diagnostics.expired = true;
      return -ETIMEDOUT;
    }

  return 0;
}

static int make_principal(const uint8_t certificate[32],
                          const uint8_t record[52], uint8_t digest[32])
{
  static const uint8_t domain[] = "SHANIU-FACTORY-DIAGNOSTICS-v1";
  uint8_t material[sizeof(domain) - 1 + 32 + 52];
  int ret;

  memcpy(material, domain, sizeof(domain) - 1);
  memcpy(material + sizeof(domain) - 1, certificate, 32);
  memcpy(material + sizeof(domain) - 1 + 32, record, 52);
  ret = mbedtls_sha256(material, sizeof(material), digest, 0);
  mbedtls_platform_zeroize(material, sizeof(material));
  return ret == 0 ? 0 : -EIO;
}

int bkfactory_diagnostics_gate(bool eligible,
                               const uint8_t certificate_sha256[32],
                               uint64_t now_ms)
{
  int ret;
  int lockret;

  if (eligible && (certificate_sha256 == NULL ||
                   all_zero(certificate_sha256, 32)))
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&g_diagnostics.lock);
  if (lockret < 0)
    {
      return lockret;
    }

  ret = advance_locked(now_ms);
  if (!eligible)
    {
      if (g_diagnostics.active)
        {
          clear_principal_locked();
          g_diagnostics.used = true;
        }

      g_diagnostics.eligible = false;
      g_diagnostics.expired = false;
      mbedtls_platform_zeroize(g_diagnostics.certificate,
                               sizeof(g_diagnostics.certificate));
      nxmutex_unlock(&g_diagnostics.lock);
      return ret < 0 ? ret : 0;
    }

  if (ret < 0)
    {
      nxmutex_unlock(&g_diagnostics.lock);
      return ret;
    }

  if (g_diagnostics.active &&
      memcmp(g_diagnostics.certificate, certificate_sha256, 32) != 0)
    {
      clear_principal_locked();
      g_diagnostics.used = true;
      g_diagnostics.eligible = false;
      mbedtls_platform_zeroize(g_diagnostics.certificate,
                               sizeof(g_diagnostics.certificate));
      nxmutex_unlock(&g_diagnostics.lock);
      return -ESTALE;
    }

  memcpy(g_diagnostics.certificate, certificate_sha256, 32);
  g_diagnostics.eligible = true;
  nxmutex_unlock(&g_diagnostics.lock);
  return 0;
}

int bkfactory_diagnostics_enable(const uint8_t *record, size_t size,
                                 uint64_t now_ms)
{
  uint8_t digest[32];
  uint64_t binding = 0;
  int ret;
  int lockret;

  if (record == NULL || size != BKFACTORY_DIAGNOSTICS_RECORD_SIZE)
    {
      return -EINVAL;
    }

  if (memcmp(record, "BKD1", 4) != 0)
    {
      return -EPROTO;
    }

  if (all_zero(record + 4, 16) || all_zero(record + 20, 32))
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&g_diagnostics.lock);
  if (lockret < 0)
    {
      return lockret;
    }

  ret = advance_locked(now_ms);
  if (ret < 0)
    {
      ret = g_diagnostics.used ? -EPERM : ret;
      goto out;
    }

  if (!g_diagnostics.eligible)
    {
      ret = -EACCES;
      goto out;
    }

  if (g_diagnostics.active)
    {
      ret = -EALREADY;
      goto out;
    }

  if (g_diagnostics.used)
    {
      ret = -EPERM;
      goto out;
    }

  if (now_ms > UINT64_MAX - BKFACTORY_DIAGNOSTICS_TTL_MS)
    {
      ret = -EOVERFLOW;
      goto out;
    }

  ret = make_principal(g_diagnostics.certificate, record, digest);
  if (ret < 0)
    {
      goto out;
    }

  for (size_t i = 0; i < sizeof(binding); i++)
    {
      binding = (binding << 8) | digest[i];
    }

  if (binding == 0)
    {
      binding = 1;
    }

  memcpy(g_diagnostics.client, record + 4, 16);
  memcpy(g_diagnostics.key, record + 20, 32);
  memcpy(g_diagnostics.transaction, digest + 8, 16);
  g_diagnostics.binding = binding;
  g_diagnostics.revision = 1;
  g_diagnostics.deadline_ms = now_ms + BKFACTORY_DIAGNOSTICS_TTL_MS;
  g_diagnostics.active = true;
  g_diagnostics.used = true;
  g_diagnostics.expired = false;
  ret = 0;

out:
  mbedtls_platform_zeroize(digest, sizeof(digest));
  nxmutex_unlock(&g_diagnostics.lock);
  return ret;
}

int bkfactory_diagnostics_revoke(uint64_t now_ms)
{
  int lockret = nxmutex_lock(&g_diagnostics.lock);

  if (lockret < 0)
    {
      return lockret;
    }

  (void)advance_locked(now_ms);
  if (g_diagnostics.active)
    {
      clear_principal_locked();
    }

  g_diagnostics.used = true;
  g_diagnostics.expired = false;
  nxmutex_unlock(&g_diagnostics.lock);
  return 0;
}

int bkfactory_diagnostics_status(uint64_t now_ms, uint32_t *flags,
                                 uint32_t *remaining_ms)
{
  uint64_t remaining = 0;
  int ret;
  int lockret;

  if (flags == NULL || remaining_ms == NULL)
    {
      return -EINVAL;
    }

  lockret = nxmutex_lock(&g_diagnostics.lock);
  if (lockret < 0)
    {
      *flags = 0;
      *remaining_ms = 0;
      return lockret;
    }

  ret = advance_locked(now_ms);
  if (ret == 0 && g_diagnostics.expired)
    {
      ret = -ETIMEDOUT;
    }
  *flags = (g_diagnostics.eligible ? BKFACTORY_DIAGNOSTICS_ELIGIBLE : 0) |
           (g_diagnostics.active ? BKFACTORY_DIAGNOSTICS_ACTIVE : 0) |
           (g_diagnostics.used ? BKFACTORY_DIAGNOSTICS_USED : 0);
  if (g_diagnostics.active)
    {
      remaining = g_diagnostics.deadline_ms - now_ms;
    }

  *remaining_ms = remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
  nxmutex_unlock(&g_diagnostics.lock);
  return ret;
}

int bkfactory_diagnostics_certificate(uint32_t offset, uint32_t *word)
{
  uint64_t now_ms;
  int ret;
  int lockret;

  if (word == NULL || (offset & 3u) != 0 || offset > 28u)
    {
      return -EINVAL;
    }

  *word = 0;
  ret = monotonic_ms(&now_ms);
  if (ret < 0)
    {
      return ret;
    }

  lockret = nxmutex_lock(&g_diagnostics.lock);
  if (lockret < 0)
    {
      return lockret;
    }

  ret = advance_locked(now_ms);
  if (ret == 0 && g_diagnostics.active)
    {
      *word = (uint32_t)g_diagnostics.certificate[offset] << 24 |
              (uint32_t)g_diagnostics.certificate[offset + 1] << 16 |
              (uint32_t)g_diagnostics.certificate[offset + 2] << 8 |
              g_diagnostics.certificate[offset + 3];
    }
  else if (ret == 0)
    {
      ret = -EACCES;
    }

  nxmutex_unlock(&g_diagnostics.lock);
  return ret;
}

int bkfactory_diagnostics_snapshot(void *context, uint64_t *binding,
                                   struct bkprov_pc_snapshot_s *view)
{
  uint64_t now_ms;
  int ret;
  int lockret;

  (void)context;
  if (binding == NULL || view == NULL)
    {
      return -EINVAL;
    }

  *binding = 0;
  memset(view, 0, sizeof(*view));
  ret = monotonic_ms(&now_ms);
  if (ret < 0)
    {
      return ret;
    }

  lockret = nxmutex_lock(&g_diagnostics.lock);
  if (lockret < 0)
    {
      return lockret;
    }

  ret = advance_locked(now_ms);
  if (ret < 0 || !g_diagnostics.active)
    {
      nxmutex_unlock(&g_diagnostics.lock);
      return -EACCES;
    }

  *binding = g_diagnostics.binding;
  view->revision = g_diagnostics.revision;
  memcpy(view->transaction, g_diagnostics.transaction,
         sizeof(view->transaction));
  memcpy(view->client, g_diagnostics.client, sizeof(view->client));
  memcpy(view->key, g_diagnostics.key, sizeof(view->key));
  view->capabilities = BKPC_CAP_DIAGNOSTICS;
  nxmutex_unlock(&g_diagnostics.lock);
  return 0;
}
