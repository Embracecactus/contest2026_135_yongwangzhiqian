/****************************************************************************
 * app/bk7258/bk7258_preferences.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_preferences.h"
#include "bk7258_preferences_storage.h"
#include "bk7258_provision_store.h"
#include <nuttx/config.h>
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
#include "bk7258_voice_volume_store.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <kvdb.h>
#include <unqlite.h>
#include <nuttx/mutex.h>

#define BK7258_PREFERENCES_VOLUME_KEY  "persist.shaniu.volume"
#define BK7258_PREFERENCES_PERSONA_KEY "persist.shaniu.persona"
#define BK7258_PREFERENCES_THINKING_KEY "persist.shaniu.thinking"
#define BK7258_PREFERENCES_WAKE_THRESHOLD_KEY "persist.shaniu.wake_threshold"
#define BK7258_PREFERENCES_DEFAULT_VOLUME 50u
#define BK7258_CLOUD_MODELS_ROOT "/cpdata/shaniu/cloud-models"

/* One owner serializes disk operations and publication of the last confirmed
 * volume. Playback can use this value while the shared medium is unavailable.
 */
static mutex_t g_preferences_lock = NXMUTEX_INITIALIZER;
static int g_playback_volume = -1;

int bk7258_preferences_with_storage(int (*operation)(void *), void *context)
{
  if (operation == NULL) return -EINVAL;
  int ret = nxmutex_lock(&g_preferences_lock);
  if (ret < 0) return ret;
  ret = bk7258_preferences_storage_begin();
  if (ret >= 0)
    {
      ret = bk7258_preferences_storage_end(operation(context));
    }
  nxmutex_unlock(&g_preferences_lock);
  return ret;
}

#ifdef CONFIG_BK7258_PROVISION_GATT
static bool g_cloud_models_uncertain;

static int bk7258_preferences_cloud_models_open(struct bkprov_store_s *store)
{
  if (mkdir(BK7258_CLOUD_MODELS_ROOT, 0700) < 0 && errno != EEXIST)
    return -errno;
  return bkprov_store_open(store, BK7258_CLOUD_MODELS_ROOT);
}

int bk7258_preferences_cloud_models_get(struct bkcloud_models_s *models)
{
  struct bkprov_store_s store;
  uint8_t record[BKCLOUD_MODELS_RECORD_MAX];
  size_t size; uint64_t revision;
  int ret;
  if (!models) return -EINVAL;
  memset(models, 0, sizeof(*models));
  ret = nxmutex_lock(&g_preferences_lock);
  if (ret < 0) return ret;
  if (g_cloud_models_uncertain)
    {
      nxmutex_unlock(&g_preferences_lock);
      return -EINPROGRESS;
    }
  ret = bk7258_preferences_cloud_models_open(&store);
  if (!ret) ret = bkprov_store_load(&store, record, sizeof(record), &size,
                                    &revision, NULL);
  if (!ret) ret = bkcloud_models_decode(models, record, size);
  memset(record, 0, sizeof(record));
  nxmutex_unlock(&g_preferences_lock);
  return ret;
}

int bk7258_preferences_cloud_models_set(const struct bkcloud_models_s *models)
{
  struct bkprov_store_s store;
  uint8_t record[BKCLOUD_MODELS_RECORD_MAX], transaction[16] = {'M', 'C', 'P', '1'};
  size_t size; uint64_t revision;
  int ret;
  if (!models) return -EINVAL;
  ret = nxmutex_lock(&g_preferences_lock);
  if (ret < 0) return ret;
  if (g_cloud_models_uncertain)
    {
      nxmutex_unlock(&g_preferences_lock);
      return -EINPROGRESS;
    }
  ret = bk7258_preferences_cloud_models_open(&store);
  if (!ret) ret = bkprov_store_load(&store, record, sizeof(record), &size,
                                    &revision, NULL);
  if (ret == -ENOENT) { revision = 0; ret = 0; }
  else if (!ret) ret = bkcloud_models_decode(&(struct bkcloud_models_s){0}, record, size);
  if (!ret && revision == UINT64_MAX) ret = -EOVERFLOW;
  if (!ret) ret = bkcloud_models_encode(models, record, sizeof(record), &size);
  if (!ret)
    {
      uint64_t next = revision + 1u;
      for (int i = 11; i >= 4; i--) { transaction[i] = next; next >>= 8; }
      ret = bkprov_store_commit(&store, revision, transaction, record, size);
      /* Reading the just-renamed bytes cannot establish a failed durability
       * barrier. Preserve uncertainty until a fresh process or completed reset.
       */
      if (ret == -EINPROGRESS) g_cloud_models_uncertain = true;
    }
  memset(record, 0, sizeof(record)); memset(transaction, 0, sizeof(transaction));
  nxmutex_unlock(&g_preferences_lock);
  return ret;
}
int bk7258_preferences_cloud_models_reset_complete(void)
{
  struct stat info;
  int ret = nxmutex_lock(&g_preferences_lock);
  if (ret < 0) return ret;
  ret = bkprov_store_check_filesystem("/cpdata/shaniu");
  if (!ret)
    {
      if (lstat(BK7258_CLOUD_MODELS_ROOT, &info) == 0) ret = -EBUSY;
      else if (errno != ENOENT) ret = -errno;
      else g_cloud_models_uncertain = false;
    }
  nxmutex_unlock(&g_preferences_lock);
  return ret;
}

#else
int bk7258_preferences_cloud_models_reset_complete(void) { return 0; }
int bk7258_preferences_cloud_models_get(struct bkcloud_models_s *models)
{ (void)models; return -ENOTSUP; }
int bk7258_preferences_cloud_models_set(const struct bkcloud_models_s *models)
{ (void)models; return -ENOTSUP; }
#endif

struct bk7258_persona_name_s
{
  enum bk7258_persona_e persona;
  const char *name;
};

static const struct bk7258_persona_name_s g_personas[] =
{
  { BK7258_PERSONA_GENTLE, "gentle" },
  { BK7258_PERSONA_PLAYFUL, "playful" },
  { BK7258_PERSONA_QUIET, "quiet" },
  { BK7258_PERSONA_SERIOUS, "serious" },
  { BK7258_PERSONA_TSUNDERE_LITE, "tsundere_lite" }
};

#ifndef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
static int bk7258_preferences_parse_volume(const char *value,
                                           unsigned int *volume_percent)
{
  unsigned int result = 0;
  size_t index;

  if (value == NULL || value[0] == '\0')
    {
      return -EBADMSG;
    }

  for (index = 0; value[index] != '\0'; index++)
    {
      if (value[index] < '0' || value[index] > '9')
        {
          return -EBADMSG;
        }

      result = result * 10u + (unsigned int)(value[index] - '0');
      if (result > 100u)
        {
          return -ERANGE;
        }
    }

  *volume_percent = result;
  return 0;
}

#endif

static int bk7258_preferences_parse_persona(const char *name,
                                             enum bk7258_persona_e *persona)
{
  size_t index;

  if (name == NULL)
    {
      return -EINVAL;
    }

  for (index = 0; index < sizeof(g_personas) / sizeof(g_personas[0]);
       index++)
    {
      if (strcmp(name, g_personas[index].name) == 0)
        {
          *persona = g_personas[index].persona;
          return 0;
        }
    }

  return -EINVAL;
}

static int bk7258_preferences_backend_result(int ret)
{
  /* The current direct/UnQLite backend returns library codes as-is, and they
   * are not all errno values. IOERR (-2) in particular is not ENOENT, so it
   * must not be overridden with the first-use default. Arguments are already
   * validated by this adapter; KVDB's own errno-class errors are preserved.
   */
  switch (ret)
    {
      case UNQLITE_NOTFOUND: return -ENODATA;
      case UNQLITE_IOERR:
      case UNQLITE_CANTOPEN: return -EIO;
      case UNQLITE_NOMEM: return -ENOMEM;
      case UNQLITE_BUSY:
      case UNQLITE_LOCKED:
      case UNQLITE_LOCKERR: return -EBUSY;
      case UNQLITE_CORRUPT: return -EBADMSG;
      case UNQLITE_READ_ONLY: return -EROFS;
      default: return ret;
    }
}

static int bk7258_preferences_read(const char *key, char value[PROP_VALUE_MAX])
{
  /* property_get() substitutes a default for every backend error, which
   * would make a damaged or unavailable DB look like first boot.  The KVDB
   * error-preserving public variant is therefore required here.
   */

  return bk7258_preferences_backend_result(property_get_with_err(key, value));
}

static int bk7258_preferences_read_thinking(void *context)
{
  char value[PROP_VALUE_MAX] = {0};
  bool *enabled = context;
  int ret = bk7258_preferences_read(BK7258_PREFERENCES_THINKING_KEY, value);
  if (ret == -ENOENT || ret == -ENODATA)
    {
      *enabled = false;
      return 0;
    }
  if (ret < 0) return ret;
  if (strcmp(value, "0") && strcmp(value, "1")) return -EBADMSG;
  *enabled = value[0] == '1';
  return 0;
}

int bk7258_preferences_thinking_get(bool *enabled)
{
  if (!enabled) return -EINVAL;
  bool observed = false;
  int ret = bk7258_preferences_with_storage(bk7258_preferences_read_thinking,
                                           &observed);
  if (!ret) *enabled = observed;
  return ret;
}

static int bk7258_preferences_read_wake_threshold(void *context)
{
  unsigned int *percent = context;
  char value[PROP_VALUE_MAX] = {0};
  int ret = bk7258_preferences_read(BK7258_PREFERENCES_WAKE_THRESHOLD_KEY, value);
  if (ret == -ENOENT || ret == -ENODATA) { *percent = 60; return 0; }
  if (ret < 0) return ret;
  if (strlen(value) != 2 || value[0] < '0' || value[0] > '9' ||
      value[1] < '0' || value[1] > '9') return -EBADMSG;
  unsigned int parsed = (value[0] - '0') * 10u + value[1] - '0';
  if (parsed < 50 || parsed > 90) return -EBADMSG;
  *percent = parsed;
  return 0;
}

int bk7258_preferences_wake_threshold_get(unsigned int *percent)
{
  if (!percent) return -EINVAL;
  return bk7258_preferences_with_storage(bk7258_preferences_read_wake_threshold,
                                         percent);
}

static int bk7258_preferences_read_all(struct bk7258_preferences_s *preferences)
{
  char value[PROP_VALUE_MAX];
  int ret;

  if (preferences == NULL)
    {
      return -EINVAL;
    }

  memset(preferences, 0, sizeof(*preferences));
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  ret = bkvoice_volume_store_get(&preferences->volume_percent);
#else
  ret = bk7258_preferences_read(BK7258_PREFERENCES_VOLUME_KEY, value);
#endif
  if (ret == -ENOENT || ret == -ENODATA)
    {
      preferences->volume_percent = BK7258_PREFERENCES_DEFAULT_VOLUME;
      preferences->volume_is_default = true;
    }
  else if (ret < 0)
    {
      return ret;
    }
  else
    {
#ifndef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
      ret = bk7258_preferences_parse_volume(value,
                                            &preferences->volume_percent);
      if (ret < 0)
        {
          return ret;
        }
#endif
    }

  ret = bk7258_preferences_read(BK7258_PREFERENCES_PERSONA_KEY, value);
  if (ret == -ENOENT || ret == -ENODATA)
    {
      preferences->persona = BK7258_PERSONA_GENTLE;
      preferences->persona_is_default = true;
      return 0;
    }

  if (ret < 0)
    {
      return ret;
    }

  return bk7258_preferences_parse_persona(value, &preferences->persona);
}

static int bk7258_preferences_get_locked(struct bk7258_preferences_s *preferences)
{
  struct bk7258_preferences_s result;
  int ret;

  if (preferences == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_preferences_storage_begin();
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_preferences_storage_end(bk7258_preferences_read_all(&result));
  if (ret >= 0)
    {
      *preferences = result;
      g_playback_volume = (int)result.volume_percent;
    }
  else
    {
      g_playback_volume = -1;
    }

  return ret;
}

int bk7258_preferences_get(struct bk7258_preferences_s *preferences)
{
  int ret;

  if (preferences == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_preferences_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_preferences_get_locked(preferences);
  nxmutex_unlock(&g_preferences_lock);
  return ret;
}

int bk7258_preferences_playback_volume(unsigned int *volume_percent)
{
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  if (volume_percent == NULL) return -EINVAL;
  int ret = bkvoice_volume_store_get(volume_percent);
  if (ret == -ENOENT || ret == -ENODATA)
    {
      *volume_percent = BK7258_PREFERENCES_DEFAULT_VOLUME;
      return 0;
    }
  return ret;
#else
  struct bk7258_preferences_s preferences;
  int ret;

  if (volume_percent == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_preferences_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_playback_volume < 0)
    {
      ret = bk7258_preferences_get_locked(&preferences);
    }

  if (ret >= 0)
    {
      *volume_percent = (unsigned int)g_playback_volume;
    }

  nxmutex_unlock(&g_preferences_lock);
  return ret;
#endif
}

static int bk7258_preferences_write(const char *key, const char *value,
                                   int volume_percent)
{
  int ret = nxmutex_lock(&g_preferences_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_preferences_storage_begin();
  if (ret < 0)
    {
      nxmutex_unlock(&g_preferences_lock);
      return ret;
    }

  ret = bk7258_preferences_backend_result(property_set(key, value));
  if (ret >= 0)
    {
      ret = bk7258_preferences_backend_result(property_commit());
    }

  ret = bk7258_preferences_storage_end(ret);
  if (ret < 0)
    {
      /* A commit/cleanup error can mean the new value reached disk. */
      g_playback_volume = -1;
    }
  else if (volume_percent >= 0)
    {
      g_playback_volume = volume_percent;
    }

  nxmutex_unlock(&g_preferences_lock);
  return ret;
}

static int bk7258_preferences_reset_locked(void)
{
  static const char *const keys[] = {
    BK7258_PREFERENCES_VOLUME_KEY,
    BK7258_PREFERENCES_PERSONA_KEY,
    BK7258_PREFERENCES_THINKING_KEY,
    BK7258_PREFERENCES_WAKE_THRESHOLD_KEY,
  };
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
      int ret = bk7258_preferences_backend_result(property_delete(keys[i]));
      if (ret == -ENOENT || ret == -ENODATA) continue;
      if (ret < 0) return ret;
    }
  return bk7258_preferences_backend_result(property_commit());
}

int bk7258_preferences_reset(void)
{
  int ret = nxmutex_lock(&g_preferences_lock);
  if (ret < 0) return ret;
  /* Delete may partly reach disk before an error. Never retain the previous
   * confirmed volume across that uncertainty. */
  g_playback_volume = -1;
  ret = bk7258_preferences_storage_begin();
  if (ret >= 0) ret = bk7258_preferences_storage_end(bk7258_preferences_reset_locked());
  nxmutex_unlock(&g_preferences_lock);
  return ret;
}

int bk7258_preferences_set_volume(unsigned int volume_percent)
{
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  if (volume_percent > 100u) return -ERANGE;
  return bkvoice_volume_store_set(volume_percent);
#else
  char value[4];

  if (volume_percent > 100u)
    {
      return -ERANGE;
    }

  (void)snprintf(value, sizeof(value), "%u", volume_percent);
  return bk7258_preferences_write(BK7258_PREFERENCES_VOLUME_KEY, value,
                                  (int)volume_percent);
#endif
}

int bk7258_preferences_set_persona(const char *persona)
{
  enum bk7258_persona_e parsed;
  int ret;

  ret = bk7258_preferences_parse_persona(persona, &parsed);
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_preferences_write(BK7258_PREFERENCES_PERSONA_KEY,
                                  bk7258_preferences_persona_name(parsed), -1);
}

int bk7258_preferences_thinking_set(bool enabled)
{
  return bk7258_preferences_write(BK7258_PREFERENCES_THINKING_KEY,
                                  enabled ? "1" : "0", -1);
}

int bk7258_preferences_wake_threshold_set(unsigned int percent)
{
  char value[4];
  if (percent < 50 || percent > 90) return -ERANGE;
  snprintf(value, sizeof(value), "%u", percent);
  return bk7258_preferences_write(BK7258_PREFERENCES_WAKE_THRESHOLD_KEY,
                                  value, -1);
}
