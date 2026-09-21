/****************************************************************************
 * app/bk7258/bk7258_provision_bootstrap.c
 * SPDX-License-Identifier: Apache-2.0
 * Consumes a CP-published, explicit factory permit. No formatting authority.
 ****************************************************************************/
#define _POSIX_C_SOURCE 200809L
#include <nuttx/config.h>
#ifdef CONFIG_BK7258_PROVISION_NATIVE
#include "bk7258_provision_bootstrap.h"
#include "bk7258_provision_storage.h"
#include "bk7258_provision_store.h"
#include "bk7258_display_service.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

#define ROOT "/cpdata"
#define PERMIT ROOT "/.shaniu-factory"
#define UPDATE PERMIT ".update"
static atomic_int g_status = ATOMIC_VAR_INIT(-EAGAIN);
static atomic_bool g_busy;
static atomic_bool g_cancel;
static bool g_started;

static int read_permit(unsigned char token[32])
{
  int ret = bkprov_store_check_filesystem(ROOT);
  if (ret) return ret;
  int fd = open(PERMIT, O_RDONLY);
  if (fd < 0) return -errno;
  size_t total = 0;
  while (total < 32)
    {
      ssize_t n = read(fd, token + total, 32 - total);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { ret = n < 0 ? -errno : -EBADMSG; break; }
      total += n;
    }
  unsigned char tail;
  if (!ret && read(fd, &tail, 1) != 0) ret = -EBADMSG;
  if (close(fd) < 0 && !ret) ret = -errno;
  if (!ret && (memcmp(token, "SFB1", 4) || token[4] != 1 ||
      (token[5] != 1 && token[5] != 2))) ret = -EBADMSG;
  unsigned char nonzero = 0;
  if (!ret)
    {
      for (size_t i = 6; i < 32; i++)
        if (i >= 8 && i < 24) nonzero |= token[i];
        else if (token[i]) ret = -EBADMSG;
      if (!nonzero) ret = -EBADMSG;
    }
  return ret;
}

static int seal_permit(unsigned char token[32])
{
  token[5] = 2;
  int fd = open(UPDATE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return -errno;
  int ret = 0;
  size_t at = 0;
  while (at < 32)
    {
      ssize_t n = write(fd, token + at, 32 - at);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { ret = n < 0 ? -errno : -EIO; break; }
      at += n;
    }
  if (!ret && fsync(fd) < 0) ret = -errno;
  if (close(fd) < 0 && !ret) ret = -errno;
  if (!ret && rename(UPDATE, PERMIT) < 0) ret = -errno;
  if (!ret) ret = bkprov_store_sync_directory(ROOT);
  return ret;
}

static void *bootstrap_worker(void *unused)
{
  (void)unused;
  unsigned char token[32];
  unsigned char *candidate = NULL;
  size_t candidate_size = 0;
  unsigned char *existing = malloc(8192);
  int result = existing ? -EAGAIN : -ENOMEM;
  struct timespec pause = {0, 100000000};
  /* Startup wait is bounded. An in-flight identity commit is reconciled even
   * after cancellation; it is never abandoned and replaced by another key.
   */
  for (unsigned int attempts = 0; existing && attempts < 1200; attempts++)
    {
      if (atomic_load(&g_cancel) && !candidate) { result = -ECANCELED; break; }
      size_t size = 0;
      int identity = bkprov_storage_identity(existing, 8192, &size);
      int permit = read_permit(token);
      if (!candidate && permit == -ENOENT && !identity)
        {
          result = size >= 4 && !memcmp(existing, "BPI1", 4) ? 0 : -EACCES;
          break;
        }
      if (permit && permit != -ENOENT && permit != -EAGAIN &&
          permit != -ENODEV && permit != -ENOTCONN && permit != -EXDEV)
        { result = permit; break; }
      if (!permit && !identity)
        {
          struct bkprov_identity_s checked = {0};
          result = bkprov_identity_load(&checked, existing, size);
          if (!result && !checked.generated) result = -EEXIST;
          bkprov_identity_clear(&checked);
          if (!result && token[5] == 1) result = seal_permit(token);
          if (!result) result = 1;
          break;
        }
      if (!permit && token[5] == 2 && identity == -ENOENT)
        { result = -ENOKEY; break; }
      if (identity && identity != -ENOENT && identity != -EAGAIN &&
          identity != -ENODEV && identity != -ENOTCONN && identity != -EXDEV)
        { result = identity; break; }
      if (!permit && token[5] == 1 && identity == -ENOENT && !candidate)
        {
          atomic_store(&g_busy, true);
          result = bkprov_identity_generate(&candidate, &candidate_size);
          if (result) break;
          struct bkprov_identity_s checked = {0};
          result = bkprov_identity_load(&checked, candidate, candidate_size);
          bkprov_identity_clear(&checked);
          if (result) break;
        }
      if (candidate)
        {
          int installed = bkprov_storage_identity_install(candidate, candidate_size);
          if (installed && installed != -EAGAIN && installed != -EBUSY)
            { result = installed; break; }
        }
      else if (identity == -ENODEV || identity == -ENOTCONN || identity == -EXDEV)
        (void)bkprov_storage_refresh();
      (void)nanosleep(&pause, NULL);
      result = -ETIMEDOUT;
    }
  if (candidate)
    { mbedtls_platform_zeroize(candidate, candidate_size); free(candidate); }
  if (existing)
    { mbedtls_platform_zeroize(existing, 8192); free(existing); }
  mbedtls_platform_zeroize(token, sizeof(token));
  atomic_store(&g_status, result);
  atomic_store(&g_busy, false);
  syslog(result < 0 ? LOG_ERR : LOG_INFO,
         "BKBOOTSTRAP identity ready=%d native=%d result=%d\n",
         result >= 0, result == 1, result < 0 ? result : 0);
  return NULL;
}

int bkprov_bootstrap_start(void)
{
  if (g_started && (atomic_load(&g_status) != -ECANCELED || atomic_load(&g_busy))) return 0;
  atomic_store(&g_cancel, false);
  atomic_store(&g_status, -EAGAIN);
  pthread_attr_t attr;
  pthread_t thread;
  int ret = pthread_attr_init(&attr);
  if (ret) return -ret;
  ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if (!ret) ret = pthread_attr_setstacksize(&attr, 16384);
  if (!ret)
    {
      atomic_store(&g_busy, true);
      ret = pthread_create(&thread, &attr, bootstrap_worker, NULL);
      if (ret) atomic_store(&g_busy, false);
    }
  pthread_attr_destroy(&attr);
  if (!ret) g_started = true;
  return -ret;
}
int bkprov_bootstrap_status(void) { return atomic_load(&g_status); }
bool bkprov_bootstrap_busy(void) { return atomic_load(&g_busy); }
void bkprov_bootstrap_cancel(void) { atomic_store(&g_cancel, true); }

int bkprov_bootstrap_window(bool open, unsigned char secret[32], void *context)
{
  struct bkprov_identity_s *identity = context;
  if (!identity || !identity->generated || !secret) return -EINVAL;
  if (!open)
    {
      mbedtls_platform_zeroize(secret, 32);
      return bk7258_display_onboarding(NULL);
    }
  unsigned char raw[64];
  char qr[108] = "SN1:";
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);
  static const unsigned char purpose[] = "shaniu-claim-window-v1";
  int ret = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                                  purpose, sizeof(purpose) - 1);
  if (!ret) ret = mbedtls_ctr_drbg_random(&random, raw + 32, 32);
  if (!ret) ret = mbedtls_sha256(identity->certificate.raw.p,
                                 identity->certificate.raw.len, raw, 0);
  if (!ret)
    {
      unsigned int acc = 0, bits = 0;
      size_t at = 4;
      for (size_t i = 0; i < sizeof(raw); i++)
        {
          acc = (acc << 8) | raw[i]; bits += 8;
          while (bits >= 5) { bits -= 5; qr[at++] = alphabet[(acc >> bits) & 31]; }
        }
      if (bits) qr[at++] = alphabet[(acc << (5 - bits)) & 31];
      qr[at] = 0;
      ret = bk7258_display_onboarding(qr);
      if (!ret) memcpy(secret, raw + 32, 32);
    }
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  mbedtls_platform_zeroize(raw, sizeof(raw));
  mbedtls_platform_zeroize(qr, sizeof(qr));
  return ret;
}
#endif
