/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_network.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_storage.h"
#include "bk7258_voice_config.h"
#include "bk7258_provision_time.h"
#include <arch/chip/bk7258_wifi.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <mbedtls/platform_util.h>

enum phase_e { WIFI, TIME, VOICE, SERVICE, VERIFIED, PERSIST, FINISH, ABORTING, QUARANTINE };
struct trial_s
{
  uint8_t bundle[BKPROV_BUNDLE_MAX];
  uint8_t voice[BKVOICE_CONFIG_MAX_BYTES];
  size_t size;
  size_t voice_size;
  uint8_t transaction[16];
  struct bkprov_settings_s settings;
  struct timespec previous_clock;
  uint64_t previous_monotonic;
  uint32_t lease;
  uint32_t ticket;
  enum phase_e phase;
  int error;
  bool wifi_pending;
  bool finish_pending;
  bool voice_loaded;
  bool clock_saved;
  bool committing;
  bool restoring;
  uint64_t started;
};
static struct trial_s *g_trial;
static struct bkprov_identity_s *g_identity;
static const struct bkprov_voice_ops_s *g_voice;
static void *g_context;
static uint8_t g_last_transaction[16];
static bool g_commit_known;
static int g_last_error;

static void bkprov_failure(const char *stage, int ret)
{
  syslog(LOG_WARNING, "BKVOICE PROVISION stage=%s ret=%d\n", stage, ret);
}

bool bkprov_network_busy(void) { return g_trial != NULL; }
int bkprov_network_bind(struct bkprov_identity_s *identity,
                         const struct bkprov_voice_ops_s *voice, void *context)
{
  if (g_trial != NULL) return -EBUSY;
  if (identity == NULL || identity->record == NULL || voice == NULL ||
      voice->available == NULL || voice->load == NULL || voice->connect == NULL ||
      voice->ready == NULL || voice->clear == NULL) return -EINVAL;
  g_identity = identity; g_voice = voice; g_context = context;
  return 0;
}
int bkprov_network_unbind(void)
{
  if (g_trial != NULL) return -EBUSY;
  g_identity = NULL; g_voice = NULL; g_context = NULL;
  return 0;
}
static void release_trial(void)
{
  g_last_error = g_trial->error;
  mbedtls_platform_zeroize(g_trial, sizeof(*g_trial));
  free(g_trial); g_trial = NULL;
}
static int begin(void *context, const uint8_t *bundle, size_t size)
{
  (void)context;
  if (g_trial != NULL) return -EBUSY;
  if (g_identity == NULL || !g_voice->available(g_context)) return -EBUSY;
  if (bundle == NULL || size == 0 || size > BKPROV_BUNDLE_MAX) return -EINVAL;
  struct trial_s *t = calloc(1, sizeof(*t));
  if (t == NULL)
    {
      bkprov_failure("alloc", -ENOMEM);
      return -ENOMEM;
    }
  memcpy(t->bundle, bundle, size); t->size = size;
  int ret = bkprov_settings_decode(&t->settings, t->bundle, size);
  if (ret < 0) bkprov_failure("decode", ret);
  if (ret == 0)
    {
      ret = bkprov_settings_voice(&t->settings, g_identity->record + 48,
                g_identity->certificate_size,
                g_identity->record + 48 + g_identity->certificate_size,
                g_identity->key_size, t->voice, sizeof(t->voice), &t->voice_size);
      if (ret < 0) bkprov_failure("voice_record", ret);
    }
  if (ret == 0 && t->settings.cloud_size && !g_voice->load_cloud) ret = -ENOTSUP;
  if (ret == 0)
    {
      ret = bkvoice_config_validate(t->voice, t->voice_size);
      if (ret < 0) bkprov_failure("validate", ret);
    }
  if (ret == 0)
    {
      ret = bk7258_wifi_trial_start(t->settings.ssid, t->settings.password,
                                     30000, &t->lease);
      if (ret < 0) bkprov_failure("wifi_start", ret);
    }
  if (ret < 0)
    { mbedtls_platform_zeroize(t, sizeof(*t)); free(t); return ret; }
  t->ticket = t->lease; t->wifi_pending = true; t->phase = WIFI;
  t->started = bkvoice_config_now_ms(NULL);
  g_trial = t; g_commit_known = false; g_last_error = 0;
  return 0;
}
int bkprov_network_restore(const void *bundle, size_t size)
{
  int ret = begin(NULL, bundle, size);
  if (ret == 0) g_trial->restoring = true;
  return ret;
}
static int poll_trial(void *context)
{
  (void)context;
  if (g_trial == NULL) return g_last_error < 0 ? g_last_error : -ENOTCONN;
  if (g_trial->error < 0) return g_trial->error;
  return g_trial->phase == VERIFIED ? 1 : 0;
}
static void abort_trial(void *context)
{
  (void)context;
  if (g_trial == NULL || g_trial->committing || g_trial->phase == QUARANTINE) return;
  g_trial->phase = ABORTING;
  if (g_trial->wifi_pending) (void)bk7258_wifi_connect_cancel(g_trial->ticket);
}
static void persist_result(int ret)
{
  struct trial_s *t = g_trial;
  if (ret == -EAGAIN) return;
  memcpy(g_last_transaction, t->transaction, 16);
  g_commit_known = true;
  if (ret == -EINPROGRESS)
    { t->phase = QUARANTINE; t->error = ret; }
  else if (ret == 0) t->phase = FINISH;
  else
    { t->committing = false; t->error = ret; abort_trial(NULL); }
}
static int commit(void *context, const uint8_t transaction[16],
                    const uint8_t *bundle, size_t size)
{
  (void)context;
  if (transaction == NULL || bundle == NULL || size == 0 ||
      size > BKPROV_BUNDLE_MAX) return -EINVAL;
  if (g_commit_known && !memcmp(transaction, g_last_transaction, 16))
    return bkprov_storage_commit(0, transaction, bundle, size);
  struct trial_s *t = g_trial;
  if (t == NULL || size != t->size || memcmp(bundle, t->bundle, size)) return -ESTALE;
  if (t->phase != VERIFIED && t->phase != PERSIST) return -EBUSY;
  if (t->committing && memcmp(transaction, t->transaction, 16)) return -ESTALE;
  memcpy(t->transaction, transaction, 16);
  t->committing = true; t->phase = PERSIST;
  /* Initial claim only: owner rejects an already selected configuration. */
  int ret = bkprov_storage_commit(0, transaction, t->bundle, t->size);
  persist_result(ret);
  return ret;
}
static const struct bkprov_claim_ops_s g_ops = {begin, poll_trial, commit, abort_trial};
const struct bkprov_claim_ops_s *bkprov_network_ops(void) { return &g_ops; }

void bkprov_network_step(void)
{
  struct trial_s *t = g_trial;
  struct bk7258_wifi_result_s result;
  int ret;
  if (t == NULL || t->phase == QUARANTINE) return;
  if (t->wifi_pending)
    {
      ret = bk7258_wifi_connect_poll(t->ticket, &result);
      if (ret == -EAGAIN) return;
      t->wifi_pending = false;
      if (ret < 0)
        { bkprov_failure("wifi_poll", ret); t->error = ret; t->phase = QUARANTINE; return; }
      if (result.status < 0)
        { bkprov_failure("wifi_result", result.status); t->error = result.status; t->phase = ABORTING; }
      if (t->phase == WIFI) t->phase = t->restoring ? TIME : VOICE;
    }
  uint64_t now_ms = bkvoice_config_now_ms(NULL);
  if (!t->committing && t->phase != ABORTING &&
      (now_ms < t->started || now_ms - t->started >= 120000))
    { t->error = -ETIMEDOUT; abort_trial(NULL); }
  if (t->phase == TIME)
    {
      uint64_t utc;
      ret = bkprov_time_get(t->settings.utc, &utc);
      if (ret == -EAGAIN) return;
      if (ret < 0) { t->error = ret; t->phase = ABORTING; }
      else
        {
          t->settings.utc = utc;
          ret = bkprov_settings_voice(&t->settings, g_identity->record + 48,
                    g_identity->certificate_size,
                    g_identity->record + 48 + g_identity->certificate_size,
                    g_identity->key_size, t->voice, sizeof(t->voice), &t->voice_size);
          if (ret < 0) { t->error = ret; t->phase = ABORTING; }
          else t->phase = VOICE;
        }
    }
  if (t->phase == VOICE)
    {
      if (clock_gettime(CLOCK_REALTIME, &t->previous_clock) < 0)
        { t->error = -errno; t->phase = ABORTING; }
      else
        {
          t->previous_monotonic = bkvoice_config_now_ms(NULL);
          t->clock_saved = true;
          if (t->settings.cloud_size)
            ret = g_voice->load_cloud(g_context, t->voice, t->voice_size,
                                      t->settings.cloud, t->settings.cloud_size);
          else ret = g_voice->load(g_context, t->voice, t->voice_size);
          mbedtls_platform_zeroize(t->voice, sizeof(t->voice));
          t->voice_loaded = true;
          if (ret < 0) bkprov_failure("voice_load", ret);
          if (ret == 0)
            {
              ret = g_voice->connect(g_context);
              if (ret < 0) bkprov_failure("voice_connect", ret);
            }
          if (ret < 0) { t->error = ret; t->phase = ABORTING; }
          else t->phase = SERVICE;
        }
    }
  if (t->phase == SERVICE)
    {
      ret = g_voice->ready(g_context);
      if (ret > 0)
        {
          t->phase = t->restoring ? FINISH : VERIFIED;
          if (t->restoring) t->committing = true;
        }
      else if (ret < 0) { bkprov_failure("service_ready", ret); t->error = ret; t->phase = ABORTING; }
    }
  if (t->phase == PERSIST)
    persist_result(bkprov_storage_commit(0, t->transaction, t->bundle, t->size));
  if (t->phase == ABORTING)
    {
      if (t->voice_loaded)
        {
          ret = g_voice->clear(g_context);
          if (ret < 0) { t->error = ret; return; }
          t->voice_loaded = false;
        }
      if (t->clock_saved)
        {
          uint64_t now = bkvoice_config_now_ms(NULL);
          if (now < t->previous_monotonic) { t->error = -EIO; return; }
          struct timespec wall = t->previous_clock;
          uint64_t elapsed = now - t->previous_monotonic;
          wall.tv_sec += elapsed / 1000;
          wall.tv_nsec += (elapsed % 1000) * 1000000;
          if (wall.tv_nsec >= 1000000000) { wall.tv_sec++; wall.tv_nsec -= 1000000000; }
          if (clock_settime(CLOCK_REALTIME, &wall) < 0) { t->error = -errno; return; }
          t->clock_saved = false;
        }
    }
  if (t->phase == ABORTING || t->phase == FINISH)
    {
      if (!t->finish_pending)
        {
          ret = bk7258_wifi_trial_finish(t->lease, t->phase == FINISH, &t->ticket);
          if (ret < 0) { t->error = ret; return; }
          t->finish_pending = true;
        }
      ret = bk7258_wifi_connect_poll(t->ticket, &result);
      if (ret == -EAGAIN) return;
      if (ret < 0 || result.status < 0)
        {
          t->error = ret < 0 ? ret : result.status;
          t->phase = QUARANTINE;
          return;
        }
      release_trial();
    }
}
