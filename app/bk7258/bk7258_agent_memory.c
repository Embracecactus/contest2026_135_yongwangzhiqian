/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/mutex.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform_util.h>
#include "cJSON.h"
#include "core/session_mgr.h"
#include "bk7258_agent_memory.h"
#include "bk7258_agent_memory_codec.h"
#include "bk7258_preferences.h"
#include "bk7258_provision_store.h"

#define POLICY_ROOT "/cpdata/shaniu/memory-policy"
#define SNAPSHOT_ROOT "/cpdata/shaniu/memory-snapshot"
#define LEGACY_ROOT "/mnt/sdnand/shaniu-memory"
#define HISTORY_TURNS 3u
#define HISTORY_TEXT 4096u
#define HISTORY_BYTES (8u + HISTORY_TURNS * (4u + 2u * HISTORY_TEXT))
#define SEALED_BYTES (HISTORY_BYTES + BKMEMORY_OVERHEAD)
#define JSON_BYTES (HISTORY_BYTES * 6u + 512u)

static mutex_t g_memory_lock = NXMUTEX_INITIALIZER;
static uint8_t g_owner[32];
static struct bkmemory_policy_s g_policy;
static bool g_bound;
static bool g_known;
static bool g_restored;
static int g_error;

struct snapshot_s
{
  uint8_t plain[HISTORY_BYTES];
  uint8_t sealed[SEALED_BYTES];
  size_t size;
  uint64_t revision;
};

static int memory_random(void *unused, unsigned char *bytes, size_t size)
{
  static const unsigned char purpose[] = "shaniu-memory-snapshot";
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  (void)unused;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&random);
  int ret = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                                 purpose, sizeof(purpose) - 1);
  if (!ret) ret = mbedtls_ctr_drbg_random(&random, bytes, size);
  mbedtls_ctr_drbg_free(&random);
  mbedtls_entropy_free(&entropy);
  return ret ? -EIO : 0;
}

static void snapshot_free(struct snapshot_s *snapshot)
{
  if (snapshot) mbedtls_platform_zeroize(snapshot, sizeof(*snapshot));
  free(snapshot);
}

static void history_free(cJSON *history)
{
  cJSON *entry;
  cJSON_ArrayForEach(entry, history)
    {
      cJSON *text = cJSON_GetObjectItemCaseSensitive(entry, "content");
      if (cJSON_IsString(text))
        mbedtls_platform_zeroize(text->valuestring, strlen(text->valuestring));
    }
  cJSON_Delete(history);
}

static int history_read(cJSON **history)
{
  char *json = calloc(1, JSON_BYTES);
  if (!json) return -ENOMEM;
  int ret = session_get_history_json("voice", json, JSON_BYTES,
                                     HISTORY_TURNS * 2);
  *history = ret ? NULL : cJSON_ParseWithOpts(json, NULL, true);
  mbedtls_platform_zeroize(json, JSON_BYTES);
  free(json);
  if (!ret && !cJSON_IsArray(*history)) ret = -EBADMSG;
  return ret;
}

/* SMH1 is only a bounded wire format; it keeps no second mutable
 * conversation history.
 */
static int history_encode(cJSON *history, unsigned int persona,
                           struct snapshot_s *snapshot)
{
  int count = cJSON_GetArraySize(history);
  if (persona > 4 || count < 0 || count > HISTORY_TURNS * 2 || count % 2)
    return -EBADMSG;
  memcpy(snapshot->plain, "SMH1", 4);
  snapshot->plain[4] = count / 2;
  snapshot->plain[5] = persona;
  snapshot->size = 8;
  for (int i = 0; i < count; i++)
    {
      cJSON *entry = cJSON_GetArrayItem(history, i);
      cJSON *role = cJSON_GetObjectItemCaseSensitive(entry, "role");
      cJSON *text = cJSON_GetObjectItemCaseSensitive(entry, "content");
      if (!cJSON_IsString(role) || !cJSON_IsString(text) ||
          strcmp(role->valuestring, i % 2 ? "assistant" : "user"))
        return -EBADMSG;
      size_t length = strlen(text->valuestring);
      if (!length || length > HISTORY_TEXT) return -E2BIG;
      snapshot->plain[snapshot->size++] = length >> 8;
      snapshot->plain[snapshot->size++] = length;
      memcpy(snapshot->plain + snapshot->size, text->valuestring, length);
      snapshot->size += length;
    }
  return 0;
}

static int legacy_read(void *context)
{
  struct snapshot_s *snapshot = context;
  return bkmemory_restore(LEGACY_ROOT, &g_policy, snapshot->plain,
                          sizeof(snapshot->plain), &snapshot->size);
}

/* This is intentionally narrower than a removable-media tree cleanup. The
 * legacy codec has exactly these two files; an unavailable mount is an error,
 * never evidence that an old user replica is absent. */
static int legacy_reset(void *unused)
{
  static const char *const names[] = {"history.enc", "history.tmp"};
  char path[192];
  struct stat info;
  (void)unused;
  if (lstat(LEGACY_ROOT, &info) < 0)
    return errno == ENOENT ? 0 : -errno;
  if (!S_ISDIR(info.st_mode)) return -ENOTDIR;
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
      if (snprintf(path, sizeof(path), "%s/%s", LEGACY_ROOT, names[i]) >=
          (int)sizeof(path)) return -ENAMETOOLONG;
      if (lstat(path, &info) < 0)
        {
          if (errno == ENOENT) continue;
          return -errno;
        }
      if (!S_ISREG(info.st_mode)) return -EPERM;
      if (unlink(path) < 0) return -errno;
    }
  return 0;
}

static int snapshot_read(struct snapshot_s *snapshot)
{
  struct bkprov_store_s store;
  size_t size;
  int ret = bkprov_store_open(&store, SNAPSHOT_ROOT);
  if (!ret) ret = bkprov_store_load(&store, snapshot->sealed,
    sizeof(snapshot->sealed), &size, &snapshot->revision, NULL);
  if (ret == -ENOENT)
    {
      /* The legacy SD ciphertext is read for compatibility only when the new
       * store truly does not exist; corruption never degrades silently. The
       * migration read makes no network request and never deletes or
       * overwrites the original file.
       */
      ret = bk7258_preferences_with_storage(legacy_read, snapshot);
      syslog(LOG_INFO, "BKVOICE memory source=legacy result=%d\n", ret);
      return ret;
    }
  if (!ret) ret = bkmemory_open(&g_policy, snapshot->sealed, size,
    snapshot->plain, sizeof(snapshot->plain), &snapshot->size);
  return ret;
}

static int snapshot_write(struct snapshot_s *snapshot)
{
  struct bkprov_store_s store;
  uint8_t transaction[16] = { 'S', 'M', 'M', '1' };
  size_t size;
  int ret;
  if (mkdir(SNAPSHOT_ROOT, 0700) < 0 && errno != EEXIST) return -errno;
  ret = bkprov_store_open(&store, SNAPSHOT_ROOT);
  if (!ret) ret = bkprov_store_load(&store, snapshot->sealed,
    sizeof(snapshot->sealed), &size, &snapshot->revision, NULL);
  if (ret == -ENOENT) { snapshot->revision = 0; ret = 0; }
  if (!ret && snapshot->revision == UINT64_MAX) ret = -EOVERFLOW;
  if (!ret) ret = bkmemory_seal(&g_policy, snapshot->plain, snapshot->size,
    snapshot->sealed, sizeof(snapshot->sealed), &size, memory_random, NULL);
  if (!ret)
    {
      uint64_t next = snapshot->revision + 1;
      for (int i = 11; i >= 4; i--) { transaction[i] = next; next >>= 8; }
      ret = bkprov_store_commit(&store, snapshot->revision, transaction,
                                snapshot->sealed, size);
    }
  return ret;
}

static int restore_locked(void)
{
  cJSON *history = NULL;
  struct snapshot_s *snapshot = NULL;
  size_t offsets[HISTORY_TURNS * 2], lengths[HISTORY_TURNS * 2];
  unsigned int appended = 0, count = 0;
  int ret = 0;
  if (g_restored) return g_error;
  if (!g_known) return g_error ? g_error : -ENOTCONN;
  if (!g_policy.enabled) { g_restored = true; return 0; }
  ret = history_read(&history);
  if (ret) goto done;
  if (cJSON_GetArraySize(history))
    {
      /* An active session never has old fragments appended to it; once memory
       * is explicitly enabled, only the official existing context is saved.
       */
      g_restored = true;
      goto done;
    }
  snapshot = calloc(1, sizeof(*snapshot));
  if (!snapshot) { ret = -ENOMEM; goto done; }
  ret = snapshot_read(snapshot);
  if (ret == -ENOENT) { ret = 0; g_restored = true; goto done; }
  if (ret) goto done;
  uint8_t *plain = snapshot->plain;
  /* The persona stored inside SMH1 is save-time metadata, not an identity
   * boundary of the memory. The current persona only affects reply style;
   * after a switch the same owner's encrypted history is still restored.
   */
  if (snapshot->size < 8 || memcmp(plain, "SMH1", 4) ||
      plain[4] > HISTORY_TURNS || plain[5] > 4 || plain[6] || plain[7])
    { ret = -EBADMSG; goto done; }
  count = plain[4] * 2;
  size_t pos = 8;
  for (unsigned int i = 0; i < count; i++)
    {
      if (snapshot->size - pos < 2) { ret = -EBADMSG; goto done; }
      size_t length = (size_t)plain[pos] << 8 | plain[pos + 1];
      pos += 2;
      if (!length || length > HISTORY_TEXT || length > snapshot->size - pos ||
          memchr(plain + pos, 0, length)) { ret = -EBADMSG; goto done; }
      offsets[i] = pos; lengths[i] = length; pos += length;
    }
  if (pos != snapshot->size) { ret = -EBADMSG; goto done; }
  for (unsigned int i = 0; i < count; i++)
    {
      char *text = strndup((char *)plain + offsets[i], lengths[i]);
      if (!text) { ret = -ENOMEM; goto done; }
      ret = session_append("voice", i % 2 ? "assistant" : "user", text);
      mbedtls_platform_zeroize(text, lengths[i]);
      free(text);
      appended++;
      if (ret) goto done;
    }
  history_free(history); history = NULL;
  ret = history_read(&history);
  if (!ret && cJSON_GetArraySize(history) != count) ret = -EIO;
  for (unsigned int i = 0; !ret && i < count; i++)
    {
      cJSON *entry = cJSON_GetArrayItem(history, i);
      cJSON *role = cJSON_GetObjectItemCaseSensitive(entry, "role");
      cJSON *text = cJSON_GetObjectItemCaseSensitive(entry, "content");
      if (!cJSON_IsString(role) || !cJSON_IsString(text) ||
          strcmp(role->valuestring, i % 2 ? "assistant" : "user") ||
          strlen(text->valuestring) != lengths[i] ||
          memcmp(text->valuestring, plain + offsets[i], lengths[i])) ret = -EIO;
    }
  if (!ret) g_restored = true;
done:
  /* Rolls back only the incomplete projection that this import appended to an
   * originally empty tmpfs Session. The original encrypted snapshot, policy
   * and user configuration stay untouched, and an automatic overwrite is
   * forbidden after a failure.
   */
  if (ret && appended) (void)session_clear("voice");
  history_free(history);
  snapshot_free(snapshot);
  g_error = ret;
  syslog(ret ? LOG_WARNING : LOG_INFO,
         "BKVOICE memory restore result=%d messages=%u\n", ret, ret ? 0 : count);
  return ret;
}

int bkagent_memory_bind(const uint8_t owner[32])
{
  if (!owner) return -EINVAL;
  int ret = nxmutex_lock(&g_memory_lock);
  if (ret) return ret;
  if (!g_bound || memcmp(g_owner, owner, sizeof(g_owner)) || !g_known)
    {
      if (g_bound && memcmp(g_owner, owner, sizeof(g_owner)))
        (void)session_clear("voice");
      mbedtls_platform_zeroize(&g_policy, sizeof(g_policy));
      memcpy(g_owner, owner, sizeof(g_owner));
      g_bound = true; g_restored = false;
      ret = bkmemory_policy_load(POLICY_ROOT, g_owner, &g_policy);
      g_known = ret == 0; g_error = ret;
      syslog(ret ? LOG_WARNING : LOG_INFO,
             "BKVOICE memory policy result=%d enabled=%d\n", ret,
             g_known && g_policy.enabled);
    }
  nxmutex_unlock(&g_memory_lock);
  return ret;
}

int bkagent_memory_reset(void)
{
  int ret = nxmutex_lock(&g_memory_lock);
  if (ret) return ret;
  /* Do this before any fallible I/O: a failed SD cleanup must never permit a
   * late reply/control path to recreate the private snapshot. */
  mbedtls_platform_zeroize(g_owner, sizeof(g_owner));
  mbedtls_platform_zeroize(&g_policy, sizeof(g_policy));
  g_bound = false;
  g_known = false;
  g_restored = false;
  g_error = -EOWNERDEAD;
  ret = session_clear("voice");
  if (ret < 0 && errno == ENOENT) ret = 0;
  if (!ret) ret = bk7258_preferences_with_storage(legacy_reset, NULL);
  if (ret) g_error = ret;
  nxmutex_unlock(&g_memory_lock);
  return ret;
}

int bkagent_memory_restore(unsigned int persona)
{
  if (persona > 4) return -EINVAL;
  int ret = nxmutex_lock(&g_memory_lock);
  if (ret) return ret;
  ret = restore_locked();
  nxmutex_unlock(&g_memory_lock);
  return ret;
}

int bkagent_memory_commit(unsigned int persona, const char *reply)
{
  cJSON *history = NULL;
  struct snapshot_s *snapshot = NULL;
  int ret = nxmutex_lock(&g_memory_lock);
  if (ret) return ret;
  if (!g_known || !g_policy.enabled) goto done;
  if (!g_restored) { ret = g_error ? g_error : -EAGAIN; goto done; }
  snapshot = calloc(1, sizeof(*snapshot));
  if (!snapshot) { ret = -ENOMEM; goto done; }
  ret = history_read(&history);
  if (!ret && reply)
    {
      cJSON *last = cJSON_GetArrayItem(history, cJSON_GetArraySize(history) - 1);
      cJSON *content = cJSON_GetObjectItemCaseSensitive(last, "content");
      if (!cJSON_IsString(content) || strcmp(content->valuestring, reply))
        ret = -EIO;
    }
  if (!ret) ret = history_encode(history, persona, snapshot);
  if (!ret) ret = snapshot_write(snapshot);
  g_error = ret;
  syslog(ret ? LOG_WARNING : LOG_INFO,
         "BKVOICE memory save result=%d turns=%u bytes=%u\n", ret,
         snapshot->plain[4], (unsigned int)snapshot->size);
done:
  history_free(history);
  snapshot_free(snapshot);
  nxmutex_unlock(&g_memory_lock);
  return ret;
}

int bkagent_memory_control(enum bkcontrol_command_e command, uint32_t value,
                           unsigned int persona)
{
  int ret = nxmutex_lock(&g_memory_lock);
  if (ret) return ret;
  if (!g_bound || !g_known) { ret = -ENOTCONN; goto done; }
  bool deleting = command == BKCONTROL_MEMORY_DELETE;
  if ((!deleting && command != BKCONTROL_MEMORY_SET) ||
      (!deleting && value > 1) || persona > 4)
    { ret = -ENOTSUP; goto done; }
  bool enabled = !deleting && value != 0;
  ret = bkmemory_policy_set(POLICY_ROOT, g_owner, enabled, deleting,
                            memory_random, NULL);
  /* The policy is re-read when publication is uncertain; an old cache must
   * not be treated as persistent fact.
   */
  int loaded = bkmemory_policy_load(POLICY_ROOT, g_owner, &g_policy);
  g_known = loaded == 0;
  if (!ret) ret = loaded;
  if (!ret && g_policy.enabled != enabled) ret = -EIO;
  if (!ret && deleting)
    {
      /* The memory data key is rotated only in response to an explicit delete
       * command from the existing App; it is not the device root key. An
       * encrypted empty snapshot is written so that a later boot cannot
       * select the old SD file again.
       */
      struct snapshot_s *snapshot = calloc(1, sizeof(*snapshot));
      if (!snapshot) ret = -ENOMEM;
      else
        {
          memcpy(snapshot->plain, "SMH1", 4);
          snapshot->plain[5] = persona;
          snapshot->size = 8;
          g_policy.enabled = true;
          ret = snapshot_write(snapshot);
          g_policy.enabled = enabled;
          snapshot_free(snapshot);
        }
      if (session_clear("voice") < 0 && errno != ENOENT && !ret) ret = -EIO;
    }
  g_error = ret;
  if (!ret) { g_restored = false; ret = restore_locked(); }
done:
  nxmutex_unlock(&g_memory_lock);
  return ret;
}

uint32_t bkagent_memory_flags(void)
{
  uint32_t flags = 512u;
  if (nxmutex_lock(&g_memory_lock) < 0) return flags | 256u;
  if (g_known) flags |= 32u | (g_policy.enabled ? 64u : 0u);
  if (g_error) flags |= 256u;
  nxmutex_unlock(&g_memory_lock);
  return flags;
}
