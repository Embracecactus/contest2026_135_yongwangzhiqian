/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_config.h"
#include "bk7258_provision_settings.h"
#include "bk7258_provision_claim.h"
#include "bk7258_provision_storage.h"
#include "bk7258_cloud_config.h"
#include "bk7258_voice_config.h"
#ifdef __NuttX__
#include "bk7258_provision_time.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509_crt.h>

#define PATCH_WIFI          1u
#define PATCH_CLOUD         2u
#define PATCH_KEY           4u
#define PATCH_PASSWORD      8u
#define PATCH_CLEAR_CLOUD  16u
#define EDIT_IDLE           0u
#define EDIT_PENDING        1u
#define EDIT_STORED         2u
#define EDIT_FAILED         3u
#define EDIT_UNCERTAIN      4u

/* All entry points run on the serialized product owner, not a BLE callback.
 * The existing storage worker is the only durable configuration writer.
 */
static struct
{
  uint8_t *candidate;
  size_t size;
  uint64_t revision;
  uint64_t deadline;
  uint8_t operation[16];
  uint32_t state;
  int result;
  uint8_t public_record[BKPROV_SETTINGS_PUBLIC_MAX];
  size_t public_size;
} g_edit;

struct workspace_s
{
  uint8_t previous[BKPROV_BUNDLE_MAX];
  uint8_t candidate[BKPROV_BUNDLE_MAX];
  uint8_t cloud[BKCLOUD_CONFIG_MAX];
  struct bkcloud_config_s old_cloud;
  struct bkcloud_config_s new_cloud;
  struct bkprov_settings_s settings;
};

static uint16_t get16(const uint8_t *p)
{
  return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t get32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t get64(const uint8_t *p)
{
  return ((uint64_t)get32(p) << 32) | get32(p + 4);
}

static void put16(uint8_t *p, size_t n)
{
  p[0] = n >> 8;
  p[1] = n;
}

static void put32(uint8_t *p, uint32_t n)
{
  p[0] = n >> 24;
  p[1] = n >> 16;
  p[2] = n >> 8;
  p[3] = n;
}

static void put64(uint8_t *p, uint64_t n)
{
  put32(p, n >> 32);
  put32(p + 4, n);
}

static bool nonzero(const uint8_t *p, size_t size)
{
  uint8_t bits = 0;
  for (size_t i = 0; i < size; i++)
    {
      bits |= p[i];
    }

  return bits != 0;
}

bool bkprov_config_busy(void)
{
  return g_edit.state == EDIT_PENDING || g_edit.state == EDIT_UNCERTAIN;
}

void bkprov_config_step(void)
{
  int ret;

  if ((g_edit.state != EDIT_PENDING && g_edit.state != EDIT_UNCERTAIN) ||
      g_edit.candidate == NULL)
    {
      return;
    }

  ret = bkprov_storage_commit(g_edit.revision, g_edit.operation,
                              g_edit.candidate, g_edit.size);
  if (ret == -EAGAIN || ret == -EBUSY)
    {
      if (bkvoice_config_now_ms(NULL) >= g_edit.deadline)
        {
          /* A timeout is not proof that a durable write was canceled.
           * Keep polling the same candidate, never replay a new operation.
           */
          g_edit.state = EDIT_UNCERTAIN;
          g_edit.result = -EINPROGRESS;
        }

      return;
    }

#ifdef __NuttX__
  if (ret == 0)
    {
      struct bkprov_settings_s settings;
      if (bkprov_settings_decode(&settings, g_edit.candidate, g_edit.size) == 0)
        (void)bkprov_time_owner_utc(settings.utc);
    }
#endif
  g_edit.result = ret;
  g_edit.state = ret == 0 ? EDIT_STORED :
                 ret == -EINPROGRESS ? EDIT_UNCERTAIN : EDIT_FAILED;
  mbedtls_platform_zeroize(g_edit.candidate, g_edit.size);
  free(g_edit.candidate);
  g_edit.candidate = NULL;
  g_edit.size = 0;
}

static int merge_cloud(struct workspace_s *w, const uint8_t *record,
                        size_t size, bool replace_key)
{
  size_t lengths[6];
  size_t total = 24;
  size_t key_at;
  size_t key_size;
  int ret;

  if (size < 24 || memcmp(record, "CCF1", 4))
    {
      return -EBADMSG;
    }

  for (size_t i = 0; i < 6; i++)
    {
      lengths[i] = get16(record + 8 + i * 2);
      if (lengths[i] > (i == 2 ? BKCLOUD_KEY_MAX : BKCLOUD_NAME_MAX))
        {
          return -EBADMSG;
        }

      total += lengths[i];
    }

  if (total != size || (replace_key ? lengths[2] == 0 : lengths[2] != 0))
    {
      return -EBADMSG;
    }

  key_at = 24 + lengths[0] + lengths[1];
  key_size = replace_key ? lengths[2] : strlen(w->old_cloud.api_key);
  if (key_size == 0 || size - lengths[2] + key_size > sizeof(w->cloud))
    {
      return -ENOKEY;
    }

  memcpy(w->cloud, record, key_at);
  put16(w->cloud + 12, key_size);
  memcpy(w->cloud + key_at,
         replace_key ? record + key_at :
         (const uint8_t *)w->old_cloud.api_key, key_size);
  memcpy(w->cloud + key_at + key_size, record + key_at + lengths[2],
         size - key_at - lengths[2]);
  total = size - lengths[2] + key_size;
  ret = bkcloud_config_decode(&w->new_cloud, w->cloud, total);
  if (ret < 0)
    {
      return ret;
    }

  if (!replace_key &&
      (strcmp(w->new_cloud.host, w->old_cloud.host) ||
       w->new_cloud.port != w->old_cloud.port))
    {
      /* An omitted secret must never be forwarded to a different server. */

      return -EACCES;
    }

  w->settings.cloud = w->cloud;
  w->settings.cloud_size = total;
  return 0;
}

static int apply_patch(const uint8_t *p, size_t size)
{
  struct workspace_s *w;
  uint32_t flags;
  size_t ssid;
  size_t password;
  size_t cloud;
  size_t ca;
  size_t previous_size;
  size_t candidate_size;
  uint8_t transaction[16];
  uint64_t revision;
  int ret;

  if (p == NULL || size < BKPROV_PATCH_HEADER || size > BKPROV_PATCH_MAX ||
      memcmp(p, "SCP1", 4) || !nonzero(p + 4, 16))
    {
      return -EBADMSG;
    }

  if (bkprov_config_busy())
    {
      return -EBUSY;
    }

  flags = get32(p + 28);
  ssid = get16(p + 40);
  password = get16(p + 42);
  cloud = get16(p + 44);
  ca = get16(p + 46);
  if (flags == 0 || (flags & ~31u) ||
      ((flags & PATCH_KEY) && !(flags & PATCH_CLOUD)) ||
      ((flags & PATCH_PASSWORD) && !(flags & PATCH_WIFI)) ||
      ((flags & PATCH_CLEAR_CLOUD) && (flags & PATCH_CLOUD)) ||
      (flags & PATCH_WIFI ? ssid == 0 || ssid > 32 : ssid || password) ||
      (flags & PATCH_PASSWORD ? password > 64 : password != 0) ||
      (flags & PATCH_CLOUD ? cloud < 24 || cloud > BKCLOUD_CONFIG_MAX :
       cloud || ca || nonzero(p + 48, 4)) ||
      ca > 4096 || size != BKPROV_PATCH_HEADER + ssid + password + cloud + ca)
    {
      return -EBADMSG;
    }

  w = calloc(1, sizeof(*w));
  if (w == NULL)
    {
      return -ENOMEM;
    }

  ret = bkprov_storage_snapshot(w->previous, sizeof(w->previous),
                                &previous_size, &revision, transaction);
  if (ret < 0)
    {
      goto out;
    }

  if (revision != get64(p + 20))
    {
      ret = !memcmp(transaction, p + 4, 16) ? -EALREADY : -ESTALE;
      goto out;
    }

  ret = bkprov_settings_decode(&w->settings, w->previous, previous_size);
  if (ret < 0 || w->settings.control_key == NULL)
    {
      if (ret == 0) ret = -ENOKEY;
      goto out;
    }

  if (w->settings.cloud_size)
    {
      ret = bkcloud_config_decode(&w->old_cloud, w->settings.cloud,
                                  w->settings.cloud_size);
      if (ret < 0) goto out;
    }

  if (flags & PATCH_WIFI)
    {
      const uint8_t *name = p + BKPROV_PATCH_HEADER;
      if (memchr(name, 0, ssid + password))
        {
          ret = -EBADMSG;
          goto out;
        }

      if (!(flags & PATCH_PASSWORD) &&
          (strlen(w->settings.ssid) != ssid ||
           memcmp(w->settings.ssid, name, ssid)))
        {
          /* Require an explicit PSK choice for a different network. */

          ret = -EINVAL;
          goto out;
        }

      memset(w->settings.ssid, 0, sizeof(w->settings.ssid));
      memcpy(w->settings.ssid, name, ssid);
      if (flags & PATCH_PASSWORD)
        {
          mbedtls_platform_zeroize(w->settings.password,
                                  sizeof(w->settings.password));
          memcpy(w->settings.password, name + ssid, password);
        }
    }

  if (flags & PATCH_CLOUD)
    {
      const uint8_t *input = p + BKPROV_PATCH_HEADER + ssid + password;
      ret = merge_cloud(w, input, cloud, (flags & PATCH_KEY) != 0);
      if (ret < 0) goto out;
      if (ca)
        {
          mbedtls_x509_crt certificate;
          mbedtls_x509_crt_init(&certificate);
          ret = mbedtls_x509_crt_parse_der(&certificate, input + cloud, ca);
          if (ret == 0 && (!certificate.MBEDTLS_PRIVATE(ca_istrue) ||
              certificate.raw.len != ca)) ret = -EINVAL;
          mbedtls_x509_crt_free(&certificate);
          if (ret < 0) goto out;
          w->settings.ca = input + cloud;
          w->settings.ca_size = ca;
          memcpy(w->settings.address, p + 48, 4);
        }
      else if (strcmp(w->old_cloud.host, w->new_cloud.host) ||
               w->old_cloud.port != w->new_cloud.port)
        {
          ret = -ENOKEY;
          goto out;
        }
      else if (nonzero(p + 48, 4))
        {
          ret = -EBADMSG;
          goto out;
        }

      memcpy(w->settings.host, w->new_cloud.host, sizeof(w->settings.host));
      w->settings.port = w->new_cloud.port;
    }

  if (flags & PATCH_CLEAR_CLOUD)
    {
      memset(w->settings.host, 0, sizeof(w->settings.host));
      memset(w->settings.address, 0, sizeof(w->settings.address));
      w->settings.port = 0;
      w->settings.cloud = NULL;
      w->settings.cloud_size = 0;
      w->settings.ca = NULL;
      w->settings.ca_size = 0;
    }

  w->settings.utc = get64(p + 32);
  ret = bkprov_settings_encode(&w->settings, w->candidate,
                               sizeof(w->candidate), &candidate_size);
  if (ret < 0) goto out;
  g_edit.candidate = malloc(candidate_size);
  if (g_edit.candidate == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  memcpy(g_edit.candidate, w->candidate, candidate_size);
  g_edit.size = candidate_size;
  g_edit.revision = revision;
  memcpy(g_edit.operation, p + 4, 16);
  g_edit.result = -EAGAIN;
  g_edit.state = EDIT_PENDING;
  g_edit.deadline = bkvoice_config_now_ms(NULL) + 15000u;
  bkprov_config_step();
  ret = g_edit.state == EDIT_FAILED ? g_edit.result : 0;

out:
  mbedtls_platform_zeroize(w, sizeof(*w));
  free(w);
  return ret;
}

static int snapshot(void)
{
  struct workspace_s *w = calloc(1, sizeof(*w));
  const char *fields[6];
  uint8_t transaction[16];
  uint64_t revision;
  size_t size;
  size_t offset = 56;
  uint32_t flags = 0;
  int ret;

  if (w == NULL) return -ENOMEM;
  ret = bkprov_storage_snapshot(w->previous, sizeof(w->previous), &size,
                                &revision, transaction);
  if (ret == 0)
    {
      ret = bkprov_settings_decode(&w->settings, w->previous, size);
    }

  if (ret == 0 && w->settings.cloud_size)
    {
      ret = bkcloud_config_decode(&w->old_cloud, w->settings.cloud,
                                  w->settings.cloud_size);
    }

  if (ret < 0) goto out;
  memset(g_edit.public_record, 0, sizeof(g_edit.public_record));
  memcpy(g_edit.public_record, "SCS1", 4);
  put32(g_edit.public_record + 4,
        g_edit.state == EDIT_IDLE ? EDIT_STORED : g_edit.state);
  put64(g_edit.public_record + 8, revision);
  memcpy(g_edit.public_record + 16,
         g_edit.state == EDIT_IDLE ? transaction : g_edit.operation, 16);
  put32(g_edit.public_record + 32, g_edit.result);
  if (w->settings.ssid[0]) flags |= 1;
  if (w->settings.password[0]) flags |= 2;
  if (w->settings.cloud_size) flags |= 4;
  if (w->old_cloud.api_key[0]) flags |= 8;
  put32(g_edit.public_record + 36, flags);
  put16(g_edit.public_record + 40, w->old_cloud.port);
  put16(g_edit.public_record + 54, w->old_cloud.dialect);
  fields[0] = w->settings.ssid;
  fields[1] = w->old_cloud.host;
  fields[2] = w->old_cloud.base_path;
  fields[3] = w->old_cloud.asr_model;
  fields[4] = w->old_cloud.chat_model;
  fields[5] = w->old_cloud.tts_model;
  for (size_t i = 0; i < 6; i++)
    {
      size_t count = strlen(fields[i]);
      if (count > sizeof(g_edit.public_record) - offset)
        {
          ret = -EOVERFLOW;
          goto out;
        }

      put16(g_edit.public_record + 42 + i * 2, count);
      memcpy(g_edit.public_record + offset, fields[i], count);
      offset += count;
    }

  g_edit.public_size = offset;
out:
  mbedtls_platform_zeroize(w, sizeof(*w));
  free(w);
  if (ret < 0) g_edit.public_size = 0;
  return ret;
}

int bkprov_config_control(enum bkcontrol_command_e command, uint32_t offset,
                          const uint8_t *record, size_t size,
                          struct bkcontrol_status_s *status)
{
  int ret;
  size_t count;

  if (status == NULL) return -EINVAL;
  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      if (bkprov_config_busy()) return -EBUSY;
      return size >= BKPROV_PATCH_HEADER && size <= BKPROV_PATCH_MAX ?
             0 : -EMSGSIZE;
    }

  if (command == BKCONTROL_CONFIG_APPLY)
    {
      return apply_patch(record, size);
    }

  if (command != BKCONTROL_CONFIG_READ || (offset & 15u))
    {
      return -EINVAL;
    }

  if (offset == 0)
    {
      ret = snapshot();
      if (ret < 0) return ret;
    }

  if (offset >= g_edit.public_size) return -ERANGE;
  count = g_edit.public_size - offset;
  if (count > sizeof(status->config_chunk))
    {
      count = sizeof(status->config_chunk);
    }

  status->config_total = g_edit.public_size;
  memset(status->config_chunk, 0, sizeof(status->config_chunk));
  memcpy(status->config_chunk, g_edit.public_record + offset, count);
  return 0;
}
