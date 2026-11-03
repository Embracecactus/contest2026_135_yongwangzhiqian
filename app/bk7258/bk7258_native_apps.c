/****************************************************************************
 * apps/bk7258/bk7258_native_apps.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_native_apps.h"
#include "bk7258_native_loader.h"
#include "bk7258_native_intent.h"
#include "bk7258_provision_store.h"

#include <cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#ifdef CONFIG_SMP
#  include <sched.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef BKNATIVE_ROOT
#  define BKNATIVE_ROOT "/cpdata/shaniu/native-apps"
#endif

#ifndef CONFIG_BK7258_NATIVE_PUBLISHER_SHA256
#  define CONFIG_BK7258_NATIVE_PUBLISHER_SHA256 ""
#endif

/* Callbacks share the product loop.  A callback that exceeds this wall-time
 * budget is aborted after it returns; a callback that never returns is left
 * to the existing supervisor, which this cooperative ABI cannot pre-empt.
 */

#ifndef BKNATIVE_CALLBACK_BUDGET_US
#  define BKNATIVE_CALLBACK_BUDGET_US 20000u
#endif

/* Abnormal terminations in one boot before an app is refused.  The count
 * lives in RAM: apps never auto-start, and the durable record format stays
 * readable by earlier firmware after a rollback.
 */

#define BKNATIVE_FAULT_LIMIT 2u
#define BKNATIVE_READ_MAX 8192u

#define BKNATIVE_PERMIT_DISPLAY (1u << 0)
#define BKNATIVE_PERMIT_MOTION (1u << 1)
#define BKNATIVE_PERMIT_HAPTIC (1u << 2)
#define BKNATIVE_PERMIT_SOUND (1u << 3)
#define BKNATIVE_PERMIT_CLOCK (1u << 4)
#define BKNATIVE_PERMIT_STORAGE (1u << 5)
#define BKNATIVE_PERMIT_COUNT 6u

/* Highest permission index each ABI may declare (inclusive). */

#define BKNATIVE_PERMIT_LAST_V2 3u
#define BKNATIVE_PERMIT_LAST_V3 5u

#define BKNATIVE_SCHEDULE_ROOT BKNATIVE_ROOT "/schedule"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct native_info_s
{
  char id[32];
  char name[65];
  char description[161];
  char sha256[65];
  uint32_t version;
  uint32_t abi;
  uint32_t permissions;
  uint32_t faults;
  unsigned int result_count;
  char results[BKNATIVE_RESULT_MAX][BKNATIVE_LABEL_MAX];
};

struct native_schedule_s
{
  uint32_t hash;          /* 0 marks an empty slot */
  uint32_t minute;
  uint32_t days;
  uint32_t seconds;
  int last_result;        /* 1 not yet due since boot/change */
  uint32_t fired_day;     /* RAM only: local day already handled */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static cJSON *g_record;
static struct native_info_s g_apps[BKNATIVE_APP_MAX];
static unsigned int g_count;
static uint64_t g_revision;
static uint64_t g_store_revision;
static int g_error = -ENODEV;
static int g_result;
static void *g_module;
static const struct bknative_program_s *g_program;
static union
{
  uint64_t alignment;
  uint8_t bytes[BKNATIVE_STATE_MAX];
} g_state;
static char g_running[32];
static char g_last_app[32];
static unsigned int g_running_index;
static uint32_t g_running_permissions;
static uint32_t g_events;
static uint32_t g_random;
static uint64_t g_started;
static uint64_t g_last_step;

static uint32_t g_last_value;
static bool g_clock_valid;
static uint32_t g_clock_minute;
static uint32_t g_clock_day;
static uint8_t g_saved[BKNATIVE_SAVED_MAX];
static uint32_t g_saved_size;
static struct native_schedule_s g_schedule[BKNATIVE_SCHEDULE_MAX];
static uint64_t g_schedule_revision;
static int g_schedule_error = -ENODEV;

static const char *const g_permission_names[BKNATIVE_PERMIT_COUNT] =
{
  "display", "motion", "haptic", "sound", "clock", "storage"
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bknative_require_cpu0(void)
{
#ifdef CONFIG_SMP
  return sched_getcpu() == 0 ? 0 : -EXDEV;
#else
  return 0;
#endif
}

static uint64_t monotonic_us(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

static uint32_t next_random(void)
{
  /* xorshift32: varied game input, never key or nonce material. */

  g_random ^= g_random << 13;
  g_random ^= g_random >> 17;
  g_random ^= g_random << 5;
  return g_random;
}

/* ABI 1 grants only "display".  ABI 2 lists "display" first, then any of the
 * remaining names once each in canonical order, so a manifest has exactly
 * one spelling for each grant set.
 */

static int permissions_parse(const cJSON *array, uint32_t abi,
                             uint32_t *bits)
{
  int count = cJSON_GetArraySize(array);
  unsigned int previous = 0;
  unsigned int last = abi == BKNATIVE_ABI ? 0 :
                      abi == BKNATIVE_ABI_V2 ? BKNATIVE_PERMIT_LAST_V2 :
                      BKNATIVE_PERMIT_LAST_V3;
  *bits = 0;
  if (!cJSON_IsArray(array) || count < 1 || count > (int)last + 1)
    {
      return -EINVAL;
    }

  for (int index = 0; index < count; index++)
    {
      const cJSON *entry = cJSON_GetArrayItem(array, index);
      unsigned int name;
      if (!cJSON_IsString(entry))
        {
          return -EINVAL;
        }

      for (name = 0; name < BKNATIVE_PERMIT_COUNT; name++)
        {
          if (!strcmp(entry->valuestring, g_permission_names[name]))
            {
              break;
            }
        }

      if (name == BKNATIVE_PERMIT_COUNT || name > last ||
          (index == 0) != (name == 0) || (index && name <= previous))
        {
          return -EINVAL;
        }

      previous = name;
      *bits |= 1u << name;
    }

  return 0;
}

static cJSON *item(const cJSON *object, const char *name)
{
  return cJSON_GetObjectItemCaseSensitive(object, name);
}

static bool fields(const cJSON *object, const char *const *names, int count)
{
  const cJSON *entry;
  unsigned int seen = 0;
  if (!cJSON_IsObject(object) || cJSON_GetArraySize(object) != count)
    {
      return false;
    }

  cJSON_ArrayForEach(entry, object)
    {
      int index;
      for (index = 0; index < count; index++)
        {
          if (entry->string && !strcmp(entry->string, names[index]))
            {
              break;
            }
        }

      if (index == count || (seen & (1u << index)))
        {
          return false;
        }

      seen |= 1u << index;
    }

  return true;
}

static cJSON *parse(const char *text, size_t size)
{
  const char *end;
  cJSON *object = cJSON_ParseWithLengthOpts(text, size, &end, false);
  while (object && end < text + size &&
         (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t'))
    {
      end++;
    }

  if (!object || end != text + size)
    {
      cJSON_Delete(object);
      return NULL;
    }

  return object;
}

static bool string_value(const cJSON *value, char *out, size_t capacity)
{
  if (!cJSON_IsString(value) || !value->valuestring[0] ||
      strlen(value->valuestring) >= capacity)
    {
      return false;
    }

  for (const unsigned char *text =
       (const unsigned char *)value->valuestring; *text; text++)
    {
      if (*text < 32)
        {
          return false;
        }
    }

  strcpy(out, value->valuestring);
  return true;
}

static int decode(const cJSON *value, uint8_t *out, size_t capacity,
                   size_t *size)
{
  if (!cJSON_IsString(value) ||
      strlen(value->valuestring) > ((capacity + 2) / 3) * 4)
    {
      return -EINVAL;
    }

  return mbedtls_base64_decode(out, capacity, size,
           (const uint8_t *)value->valuestring,
           strlen(value->valuestring)) == 0 && *size ? 0 : -EINVAL;
}

static void hex(const uint8_t *bytes, char out[65])
{
  static const char digits[] = "0123456789abcdef";
  for (unsigned int index = 0; index < 32; index++)
    {
      out[index * 2] = digits[bytes[index] >> 4];
      out[index * 2 + 1] = digits[bytes[index] & 15];
    }

  out[64] = 0;
}

static uint32_t little(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
         (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static int elf_check(const uint8_t *image, size_t size)
{
  uint32_t offset;
  uint32_t count;
  uint32_t allocated = 0;
  bool symbols_seen = false;
  if (size < 52 || memcmp(image, "\177ELF\1\1\1", 7) ||
      image[16] != 1 || image[17] || image[18] != 40 || image[19] ||
      little(image + 20) != 1 || image[40] != 52 || image[41] ||
      image[46] != 40 || image[47])
    {
      return -ENOEXEC;
    }

  offset = little(image + 32);
  count = image[48] | (uint32_t)image[49] << 8;
  if (!count || count > 128 || offset > size || count * 40 > size - offset)
    {
      return -ENOEXEC;
    }

  for (uint32_t index = 0; index < count; index++)
    {
      const uint8_t *section = image + offset + index * 40;
      uint32_t type = little(section + 4);
      uint32_t begin = little(section + 16);
      uint32_t length = little(section + 20);
      uint32_t link = little(section + 24);
      if (link >= count)
        {
          return -ENOEXEC;
        }

      if (type != 8 && (begin > size || length > size - begin))
        {
          return -ENOEXEC;
        }

      if (type == 14 || type == 15 || type == 16)
        {
          return -ENOEXEC;
        }

      if (little(section + 8) & 2)
        {
          uint32_t alignment = little(section + 32);
          if (length > 65536u - allocated || alignment > 16 ||
              (alignment && (alignment & (alignment - 1))))
            {
              return -EFBIG;
            }

          allocated += length;
        }

      if (type == 2)
        {
          symbols_seen = true;
          if (little(section + 36) != 16 || length % 16 ||
              little(image + offset + link * 40 + 4) != 3)
            {
              return -ENOEXEC;
            }

          for (uint32_t symbol = 16; symbol < length; symbol += 16)
            {
              const uint8_t *entry = image + begin + symbol;
              uint32_t section_index = entry[14] | (uint32_t)entry[15] << 8;
              if (!section_index ||
                  (section_index >= count && section_index != 0xfff1u))
                {
                  return -ENOEXEC;
                }
            }
        }

      if (type == 9 || type == 4)
        {
          uint32_t target = little(section + 28);
          uint32_t stride = type == 9 ? 8 : 12;
          const uint8_t *symbols = image + offset + link * 40;
          if (target >= count || little(symbols + 4) != 2 ||
              little(section + 36) != stride || length % stride)
            {
              return -ENOEXEC;
            }

          uint32_t target_size = little(image + offset + target * 40 + 20);
          uint32_t symbol_count = little(symbols + 20) / 16;
          for (uint32_t relocation = 0; relocation < length;
               relocation += stride)
            {
              const uint8_t *entry = image + begin + relocation;
              uint32_t address = little(entry);
              uint32_t info = little(entry + 4);
              if ((info >> 8) >= symbol_count ||
                  ((info & 255) && (address > target_size ||
                                   target_size - address < 4)))
                {
                  return -ENOEXEC;
                }
            }
        }
    }

  return allocated && symbols_seen ? 0 : -ENOEXEC;
}

static int public_key(const cJSON *value, mbedtls_pk_context *key)
{
  uint8_t der[256];
  uint8_t hash[32];
  char fingerprint[65];
  size_t size;
  int ret = decode(value, der, sizeof(der), &size);
  if (ret || mbedtls_sha256(der, size, hash, 0))
    {
      return -EKEYREJECTED;
    }

  hex(hash, fingerprint);
  if (strcmp(fingerprint, CONFIG_BK7258_NATIVE_PUBLISHER_SHA256) ||
      mbedtls_pk_parse_public_key(key, der, size) ||
      !mbedtls_pk_can_do(key, MBEDTLS_PK_ECKEY) ||
      mbedtls_pk_ec(*key)->grp.id != MBEDTLS_ECP_DP_SECP256R1)
    {
      return -EKEYREJECTED;
    }

  return 0;
}

static int verify(const cJSON *package, const cJSON *key_value,
                   struct native_info_s *info, uint8_t **image_out,
                   size_t *size_out)
{
  static const char *const package_fields[] =
  {
    "format", "manifest", "signature", "image"
  };

  static const char *const manifest_fields[] =
  {
    "id", "name", "description", "version", "abi", "target",
    "sha256", "image_size", "permissions", "results"
  };

  uint8_t manifest[1024];
  uint8_t signature[80];
  uint8_t hash[32];
  size_t manifest_size;
  size_t signature_size;
  size_t image_size;
  char digest[65];
  cJSON *object = NULL;
  uint8_t *image = NULL;
  mbedtls_pk_context key;
  int ret = -EINVAL;
  mbedtls_pk_init(&key);
  memset(info, 0, sizeof(*info));
  if (!fields(package, package_fields, 4) ||
      !cJSON_IsString(item(package, "format")) ||
      strcmp(item(package, "format")->valuestring, "shaniu.native/1") ||
      decode(item(package, "manifest"), manifest, sizeof(manifest),
             &manifest_size) || memchr(manifest, 0, manifest_size) ||
      decode(item(package, "signature"), signature, sizeof(signature),
             &signature_size))
    {
      goto done;
    }

  ret = public_key(key_value, &key);
  if (ret || mbedtls_sha256(manifest, manifest_size, hash, 0) ||
      mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, 32,
                        signature, signature_size))
    {
      ret = -EKEYREJECTED;
      goto done;
    }

  ret = -EINVAL;
  object = parse((const char *)manifest, manifest_size);
  if ((!fields(object, manifest_fields, 9) &&
       !fields(object, manifest_fields, 10)) ||
      !string_value(item(object, "id"), info->id, sizeof(info->id)) ||
      !string_value(item(object, "name"), info->name, sizeof(info->name)) ||
      !string_value(item(object, "description"), info->description,
                     sizeof(info->description)) ||
      !string_value(item(object, "sha256"), info->sha256,
                     sizeof(info->sha256)) ||
      !cJSON_IsNumber(item(object, "version")) ||
      item(object, "version")->valuedouble < 1 ||
      item(object, "version")->valuedouble > UINT32_MAX ||
      !cJSON_IsNumber(item(object, "abi")) ||
      item(object, "abi")->valuedouble < BKNATIVE_ABI ||
      item(object, "abi")->valuedouble > BKNATIVE_ABI_V3 ||
      item(object, "abi")->valuedouble !=
      (uint32_t)item(object, "abi")->valuedouble ||
      !cJSON_IsString(item(object, "target")) ||
      strcmp(item(object, "target")->valuestring, "bk7258-ap-armv8m"))
    {
      goto done;
    }

  info->abi = (uint32_t)item(object, "abi")->valuedouble;
  if (permissions_parse(item(object, "permissions"), info->abi,
                        &info->permissions))
    {
      goto done;
    }

  /* Optional ABI 3 result labels: 1..4 short display strings. */

  if (item(object, "results"))
    {
      cJSON *results = item(object, "results");
      int count = cJSON_GetArraySize(results);
      if (info->abi != BKNATIVE_ABI_V3 || !cJSON_IsArray(results) ||
          count < 1 || count > (int)BKNATIVE_RESULT_MAX)
        {
          goto done;
        }

      for (int index = 0; index < count; index++)
        {
          if (!string_value(cJSON_GetArrayItem(results, index),
                            info->results[index], BKNATIVE_LABEL_MAX))
            {
              goto done;
            }
        }

      info->result_count = (unsigned int)count;
    }

  info->version = (uint32_t)item(object, "version")->valuedouble;
  if ((double)info->version != item(object, "version")->valuedouble ||
      info->id[0] == '-')
    {
      goto done;
    }

  for (const char *text = info->id; *text; text++)
    {
      if (!((*text >= 'a' && *text <= 'z') ||
            (*text >= '0' && *text <= '9') || *text == '-'))
        {
          goto done;
        }
    }

  image = malloc(BKNATIVE_IMAGE_MAX);
  if (!image)
    {
      ret = -ENOMEM;
      goto done;
    }

  if (decode(item(package, "image"), image, BKNATIVE_IMAGE_MAX,
              &image_size) || !cJSON_IsNumber(item(object, "image_size")) ||
      item(object, "image_size")->valuedouble != (double)image_size ||
      mbedtls_sha256(image, image_size, hash, 0))
    {
      goto done;
    }

  hex(hash, digest);
  if (strcmp(digest, info->sha256))
    {
      ret = -EBADMSG;
      goto done;
    }

  ret = elf_check(image, image_size);
  if (!ret && image_out)
    {
      *image_out = image;
      *size_out = image_size;
      image = NULL;
    }

done:
  free(image);
  cJSON_Delete(object);
  mbedtls_pk_free(&key);
  return ret;
}

static int stop_locked(void)
{
  int ret = g_module ? bknative_loader_close(g_module) : 0;
  if (!ret)
    {
      g_module = NULL;
      g_program = NULL;
      g_running[0] = 0;
      g_running_permissions = 0;
      g_events = 0;
      g_saved_size = 0;
      memset(&g_state, 0, sizeof(g_state));
    }

  return ret;
}

/* Record how the running app ended.  Cancellation by the product or user is
 * not the app's fault; any other failure counts toward the boot-local limit.
 */

static void finish_locked(int result)
{
  if (g_running_index < g_count && g_running[0] &&
      !strcmp(g_apps[g_running_index].id, g_running))
    {
      struct native_info_s *info = &g_apps[g_running_index];
      if (!result)
        {
          info->faults = 0;
        }
      else if (result != -ECANCELED && info->faults < UINT32_MAX)
        {
          info->faults++;
        }
    }
}

/* Publish a metadata copy for the Agent tool and local-text route, which
 * may run on any CPU and must not enter this CPU0-owned manager.
 */

static void publish_locked(void)
{
  struct bknative_catalog_s catalog;
  memset(&catalog, 0, sizeof(catalog));
  catalog.revision = g_revision;
  catalog.count = g_count;
  for (unsigned int index = 0; index < g_count; index++)
    {
      struct bknative_catalog_app_s *app = &catalog.apps[index];
      strcpy(app->id, g_apps[index].id);
      strcpy(app->name, g_apps[index].name);
      app->hash = bknative_app_hash(g_apps[index].id);
      app->version = g_apps[index].version;
      app->blocked = g_apps[index].faults >= BKNATIVE_FAULT_LIMIT;
      app->result_count = g_apps[index].result_count;
      memcpy(app->results, g_apps[index].results, sizeof(app->results));
    }

  for (unsigned int slot = 0; slot < BKNATIVE_SCHEDULE_MAX; slot++)
    {
      catalog.schedule[slot].app_hash = g_schedule[slot].hash;
      catalog.schedule[slot].minute = g_schedule[slot].minute;
      catalog.schedule[slot].days = g_schedule[slot].days;
      catalog.schedule[slot].seconds = g_schedule[slot].seconds;
      catalog.schedule[slot].last_result = g_schedule[slot].last_result;
    }

  strcpy(catalog.running, g_running);
  strcpy(catalog.last_app, g_last_app);
  catalog.last_result = g_result;
  catalog.last_value = g_last_value;
  catalog.clock_valid = g_clock_valid;
  bknative_catalog_publish(&catalog);
}

/* 0 with *index for one installed app; an ambiguous hash selects none. */

static int find_hash_locked(uint32_t hash, unsigned int *index)
{
  int ret = -ENOENT;
  for (unsigned int candidate = 0; candidate < g_count; candidate++)
    {
      if (bknative_app_hash(g_apps[candidate].id) == hash)
        {
          ret = ret == -ENOENT ? 0 : -ENOTUNIQ;
          *index = candidate;
        }
    }

  return ret;
}

static int ensure_directory(const char *path)
{
  if (mkdir(BKNATIVE_ROOT, 0700) < 0 && errno != EEXIST)
    {
      return -errno;
    }

  return mkdir(path, 0700) < 0 && errno != EEXIST ? -errno : 0;
}

static void state_path(const char *id, char *path, size_t capacity)
{
  /* IDs are validated as [a-z0-9-], so they are safe path components. */

  snprintf(path, capacity, BKNATIVE_ROOT "/state-%.31s", id);
}

/* Loads up to size saved bytes; a missing or differently sized record
 * yields zeros, so an app version that changes its layout starts clean.
 */

static int state_load(const char *id, uint8_t *out, uint32_t size)
{
  struct bkprov_store_s store;
  uint8_t record[4 + BKNATIVE_SAVED_MAX];
  uint64_t revision;
  size_t length = 0;
  char path[96];
  int ret;

  memset(out, 0, size);
  state_path(id, path, sizeof(path));
  ret = bkprov_store_open(&store, path);
  if (ret == -ENOENT)
    {
      return 0;
    }

  if (!ret)
    {
      ret = bkprov_store_load(&store, record, sizeof(record), &length,
                              &revision, NULL);
    }

  if (ret == -ENOENT)
    {
      return 0;
    }

  if (!ret && length == 4 + size && !memcmp(record, "NSV1", 4))
    {
      memcpy(out, record + 4, size);
    }

  return ret;
}

static int state_save(const char *id, const uint8_t *data, uint32_t size)
{
  struct bkprov_store_s store;
  uint8_t record[4 + BKNATIVE_SAVED_MAX];
  uint8_t scratch[4 + BKNATIVE_SAVED_MAX];
  uint8_t transaction[16] =
  {
    'N', 'S', 'V', '1'
  };

  uint64_t revision = 0;
  size_t length;
  char path[96];
  int ret;

  state_path(id, path, sizeof(path));
  ret = ensure_directory(path);
  if (!ret)
    {
      ret = bkprov_store_open(&store, path);
    }

  if (!ret)
    {
      ret = bkprov_store_load(&store, scratch, sizeof(scratch), &length,
                              &revision, NULL);
      ret = ret == -ENOENT ? 0 : ret;
    }

  if (!ret)
    {
      memcpy(record, "NSV1", 4);
      memcpy(record + 4, data, size);
      for (unsigned int index = 0; index < 8; index++)
        {
          transaction[15 - index] = (uint8_t)((revision + 1) >> (index * 8));
        }

      ret = bkprov_store_commit(&store, revision, transaction, record,
                                4 + size);
    }

  return ret;
}

static void state_remove(const char *id)
{
  char path[96];
  char file[128];
  state_path(id, path, sizeof(path));
  snprintf(file, sizeof(file), "%s/config.bin", path);
  (void)unlink(file);
  snprintf(file, sizeof(file), "%s/config.pending", path);
  (void)unlink(file);
  (void)rmdir(path);
}

static int schedule_commit_locked(const struct native_schedule_s *next)
{
  struct bkprov_store_s store;
  uint8_t transaction[16] =
  {
    'N', 'S', 'C', '1'
  };

  cJSON *root = cJSON_CreateObject();
  cJSON *entries = root ? cJSON_AddArrayToObject(root, "entries") : NULL;
  char *serialized = NULL;
  int ret = entries ? 0 : -ENOMEM;

  for (unsigned int slot = 0; !ret && slot < BKNATIVE_SCHEDULE_MAX; slot++)
    {
      const double values[4] =
      {
        next[slot].hash, next[slot].minute, next[slot].days,
        next[slot].seconds
      };

      cJSON *entry = cJSON_CreateDoubleArray(values, 4);
      if (!entry || !cJSON_AddItemToArray(entries, entry))
        {
          cJSON_Delete(entry);
          ret = -ENOMEM;
        }
    }

  serialized = !ret ? cJSON_PrintUnformatted(root) : NULL;
  ret = ret ? ret : serialized ? ensure_directory(BKNATIVE_SCHEDULE_ROOT) :
        -ENOMEM;
  if (!ret)
    {
      ret = bkprov_store_open(&store, BKNATIVE_SCHEDULE_ROOT);
    }

  if (!ret)
    {
      for (unsigned int index = 0; index < 8; index++)
        {
          transaction[15 - index] =
            (uint8_t)((g_schedule_revision + 1) >> (index * 8));
        }

      ret = bkprov_store_commit(&store, g_schedule_revision, transaction,
                                serialized, strlen(serialized));
    }

  if (!ret)
    {
      g_schedule_revision++;
      memcpy(g_schedule, next, sizeof(g_schedule));
    }

  free(serialized);
  cJSON_Delete(root);
  return ret;
}

static int schedule_load_locked(void)
{
  struct bkprov_store_s store;
  char buffer[512];
  size_t size = 0;
  cJSON *root = NULL;
  int ret;

  memset(g_schedule, 0, sizeof(g_schedule));
  g_schedule_revision = 0;
  ret = bkprov_store_open(&store, BKNATIVE_SCHEDULE_ROOT);
  if (!ret)
    {
      ret = bkprov_store_load(&store, buffer, sizeof(buffer) - 1, &size,
                              &g_schedule_revision, NULL);
    }

  if (ret == -ENOENT)
    {
      g_schedule_revision = 0;
      return 0;
    }

  if (!ret)
    {
      cJSON *entries;
      buffer[size] = 0;
      root = cJSON_Parse(buffer);
      entries = item(root, "entries");
      ret = cJSON_IsArray(entries) &&
            cJSON_GetArraySize(entries) == BKNATIVE_SCHEDULE_MAX ?
            0 : -EBADMSG;
      for (unsigned int slot = 0; !ret && slot < BKNATIVE_SCHEDULE_MAX;
           slot++)
        {
          cJSON *entry = cJSON_GetArrayItem(entries, slot);
          double values[4];
          for (unsigned int field = 0; field < 4; field++)
            {
              cJSON *value = cJSON_GetArrayItem(entry, field);
              values[field] = cJSON_IsNumber(value) ? value->valuedouble : -1;
            }

          if (cJSON_GetArraySize(entry) != 4 || values[0] < 0 ||
              values[0] > UINT32_MAX || values[1] < 0 || values[1] >= 1440 ||
              values[2] < 0 || values[2] > BKNATIVE_DAYS_ALL ||
              values[3] < 0 || values[3] > BKNATIVE_INTENT_SECONDS_MAX ||
              (values[0] != 0) != (values[2] != 0))
            {
              ret = -EBADMSG;
              break;
            }

          g_schedule[slot].hash = (uint32_t)values[0];
          g_schedule[slot].minute = (uint32_t)values[1];
          g_schedule[slot].days = (uint32_t)values[2];
          g_schedule[slot].seconds = (uint32_t)values[3];
          g_schedule[slot].last_result = 1;
        }
    }

  if (ret)
    {
      memset(g_schedule, 0, sizeof(g_schedule));
    }

  cJSON_Delete(root);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bknative_abort(int reason)
{
  int owner = bknative_require_cpu0();
  if (owner < 0)
    {
      return owner;
    }

  pthread_mutex_lock(&g_lock);
  bool running = g_module != NULL;
  if (running)
    {
      finish_locked(reason);
    }

  int ret = stop_locked();
  if (running)
    {
      g_result = ret ? ret : reason;
      g_last_value = 0;
      g_revision++;
      publish_locked();
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bknative_stop(void)
{
  return bknative_abort(-ECANCELED);
}

static int validate_record(const cJSON *record,
                           struct native_info_s infos[BKNATIVE_APP_MAX],
                           unsigned int *count)
{
  static const char *const names[] =
  {
    "public_key", "packages"
  };

  cJSON *packages = item(record, "packages");
  if (!fields(record, names, 2) ||
      !cJSON_IsString(item(record, "public_key")) ||
      !cJSON_IsArray(packages) ||
      cJSON_GetArraySize(packages) > (int)BKNATIVE_APP_MAX)
    {
      return -EBADMSG;
    }

  *count = cJSON_GetArraySize(packages);
  if (item(record, "public_key")->valuestring[0])
    {
      mbedtls_pk_context key;
      mbedtls_pk_init(&key);
      int ret = public_key(item(record, "public_key"), &key);
      mbedtls_pk_free(&key);
      if (ret)
        {
          return ret;
        }
    }

  for (unsigned int index = 0; index < *count; index++)
    {
      int ret = verify(cJSON_GetArrayItem(packages, index),
                         item(record, "public_key"), &infos[index],
                         NULL, NULL);
      if (ret)
        {
          return ret;
        }

      for (unsigned int previous = 0; previous < index; previous++)
        {
          if (!strcmp(infos[index].id, infos[previous].id))
            {
              return -EEXIST;
            }
        }
    }

  return 0;
}

int bknative_init(void)
{
  struct bkprov_store_s store;
  size_t size = 0;
  char *buffer;
  int ret;

  ret = bknative_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  buffer = malloc(BKPROV_STORE_RECORD_MAX);
  pthread_mutex_lock(&g_lock);
  ret = stop_locked();
  if (ret || !buffer)
    {
      ret = ret ? ret : -ENOMEM;
      goto done;
    }

  cJSON_Delete(g_record);
  g_record = NULL;
  g_count = 0;
  g_store_revision = 0;
  ret = bkprov_store_open(&store, BKNATIVE_ROOT);
  if (!ret)
    {
      ret = bkprov_store_load(&store, buffer, BKPROV_STORE_RECORD_MAX,
                              &size, &g_store_revision, NULL);
    }

  if (ret == -ENOENT)
    {
      g_record = cJSON_Parse("{\"public_key\":\"\",\"packages\":[]}");
      ret = g_record ? 0 : -ENOMEM;
    }
  else if (!ret)
    {
      g_record = parse(buffer, size);
      ret = validate_record(g_record, g_apps, &g_count);
    }

  g_revision = g_store_revision;
  g_result = 0;
  g_last_value = 0;

  /* A damaged schedule disables only scheduling; apps remain usable. */

  g_schedule_error = schedule_load_locked();
  if (g_schedule_error)
    {
      syslog(LOG_WARNING, "BKNATIVE schedule load=%d\n", g_schedule_error);
    }

done:
  g_error = ret;
  if (!ret)
    {
      publish_locked();
    }

  pthread_mutex_unlock(&g_lock);
  free(buffer);
  return ret;
}

static int commit(cJSON *next)
{
  struct native_info_s infos[BKNATIVE_APP_MAX];
  struct bkprov_store_s store;
  unsigned int count;
  uint8_t transaction[16] =
  {
    'N', 'A', 'P', '1'
  };

  char *serialized = NULL;
  int ret = validate_record(next, infos, &count);
  if (ret)
    {
      return ret;
    }

  serialized = cJSON_PrintUnformatted(next);
  if (!serialized)
    {
      return -ENOMEM;
    }

  if (strlen(serialized) > BKPROV_STORE_RECORD_MAX)
    {
      ret = -ENOSPC;
      goto done;
    }

  if (mkdir(BKNATIVE_ROOT, 0700) < 0 && errno != EEXIST)
    {
      ret = -errno;
      goto done;
    }

  ret = bkprov_store_open(&store, BKNATIVE_ROOT);
  if (!ret)
    {
      uint64_t version = g_store_revision + 1;
      for (unsigned int index = 0; index < 8; index++)
        {
          transaction[15 - index] = version;
          version >>= 8;
        }

      ret = bkprov_store_commit(&store, g_store_revision, transaction,
                                 serialized, strlen(serialized));
    }

  if (!ret)
    {
      /* An untouched entry keeps its fault count.  Any signed reinstall has
       * a higher version and therefore starts clean, even with one image.
       */

      for (unsigned int index = 0; index < count; index++)
        {
          for (unsigned int old = 0; old < g_count; old++)
            {
              if (!strcmp(infos[index].id, g_apps[old].id) &&
                  infos[index].version == g_apps[old].version &&
                  !strcmp(infos[index].sha256, g_apps[old].sha256))
                {
                  infos[index].faults = g_apps[old].faults;
                }
            }
        }

      /* An uninstalled app's saved state goes with it; an update keeps
       * its state so a new version can continue a streak or counter.
       */

      for (unsigned int old = 0; old < g_count; old++)
        {
          bool kept = false;
          for (unsigned int index = 0; index < count; index++)
            {
              kept = kept || !strcmp(g_apps[old].id, infos[index].id);
            }

          if (!kept)
            {
              state_remove(g_apps[old].id);
            }
        }

      g_store_revision++;
      g_revision++;
      cJSON_Delete(g_record);
      g_record = next;
      memcpy(g_apps, infos, sizeof(g_apps));
      g_count = count;
      publish_locked();
    }
  else if (ret == -EINPROGRESS)
    {
      g_error = ret;
    }

done:
  free(serialized);
  return ret;
}

static int start_locked(unsigned int index, uint32_t argument)
{
  struct native_info_s info;
  uint8_t *image = NULL;
  size_t size;
  bool loaded = false;
  int ret;

  if (g_apps[index].faults >= BKNATIVE_FAULT_LIMIT)
    {
      return -EPERM;
    }

  ret = verify(cJSON_GetArrayItem(item(g_record, "packages"), index),
               item(g_record, "public_key"), &info, &image, &size);
  if (ret)
    {
      return ret;
    }

  int descriptor = open(BKNATIVE_ROOT "/running.elf",
                         O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  if (descriptor < 0)
    {
      free(image);
      return -errno;
    }

  size_t written = 0;
  while (written < size)
    {
      ssize_t bytes = write(descriptor, image + written, size - written);
      if (bytes < 0 && errno == EINTR)
        {
          continue;
        }

      if (bytes <= 0)
        {
          ret = bytes < 0 ? -errno : -EIO;
          break;
        }

      written += bytes;
    }

  if (close(descriptor) < 0 && !ret)
    {
      ret = -errno;
    }

  free(image);
  if (!ret)
    {
      ret = bknative_loader_open(BKNATIVE_ROOT "/running.elf", &g_module,
                                  &g_program);
      loaded = true;
    }

  if (unlink(BKNATIVE_ROOT "/running.elf") < 0 && !ret)
    {
      ret = -errno;
    }

  if (!ret)
    {
      /* The signed manifest and the loaded header must agree on the ABI,
       * so a v1 grant can never reach a v2 entry or the reverse.
       */

      if (!g_program || g_program->abi != info.abi ||
          g_program->state_size > sizeof(g_state) ||
          !g_program->start || !g_program->step ||
          (info.abi == BKNATIVE_ABI_V3 &&
           (g_program->saved_size > BKNATIVE_SAVED_MAX ||
            g_program->saved_size > g_program->state_size ||
            (g_program->saved_size &&
             !(info.permissions & BKNATIVE_PERMIT_STORAGE)))))
        {
          ret = -ENOEXEC;
        }
      else
        {
          uint64_t begin;
          memset(&g_state, 0, sizeof(g_state));
          g_saved_size = info.abi == BKNATIVE_ABI_V3 ?
                         g_program->saved_size : 0;
          if (g_saved_size)
            {
              /* Storage failure is not the app's fault: refuse the start
               * without loading partial state.
               */

              int loaded_state = state_load(info.id, g_saved, g_saved_size);
              if (loaded_state)
                {
                  int cleanup = stop_locked();
                  return cleanup ? cleanup : loaded_state;
                }

              memcpy(g_state.bytes, g_saved, g_saved_size);
            }

          begin = monotonic_us();
          ret = g_program->start(g_state.bytes, argument);
          if (!ret && monotonic_us() - begin > BKNATIVE_CALLBACK_BUDGET_US)
            {
              ret = -ETIMEDOUT;
            }
        }
    }

  if (ret)
    {
      int cleanup = stop_locked();
      if (loaded && g_apps[index].faults < UINT32_MAX)
        {
          g_apps[index].faults++;
        }

      return cleanup ? cleanup : ret;
    }

  strcpy(g_running, info.id);
  strcpy(g_last_app, info.id);
  g_last_value = 0;
  g_running_index = index;
  g_running_permissions = info.permissions;
  g_events = 0;
  g_random = (uint32_t)monotonic_us() ^ 0x9e3779b9u;
  if (!g_random)
    {
      g_random = 1;
    }

  g_started = 0;
  g_last_step = 0;
  return 0;
}

int bknative_apply(const char *input)
{
  static const char *const trust_fields[] =
  {
    "revision", "op", "public_key"
  };

  static const char *const install_fields[] =
  {
    "revision", "op", "package"
  };

  static const char *const action_fields[] =
  {
    "revision", "op", "id"
  };

  static const char *const start_fields[] =
  {
    "revision", "op", "id", "argument"
  };

  cJSON *request;
  cJSON *next = NULL;
  cJSON *operation;
  cJSON *revision;
  cJSON *id;
  char expected[24];
  unsigned int index;
  int ret;

  ret = bknative_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  request = input ? parse(input, strlen(input)) : NULL;
  operation = item(request, "op");
  revision = item(request, "revision");
  id = item(request, "id");
  ret = -EINVAL;
  pthread_mutex_lock(&g_lock);
  if (g_error)
    {
      ret = g_error;
      goto done;
    }

  if (!cJSON_IsString(operation) || !cJSON_IsString(revision))
    {
      goto done;
    }

  snprintf(expected, sizeof(expected), "%llu",
             (unsigned long long)g_revision);
  if (strcmp(expected, revision->valuestring))
    {
      ret = -EAGAIN;
      goto done;
    }

  if (!strcmp(operation->valuestring, "start") ||
      !strcmp(operation->valuestring, "stop"))
    {
      bool start = !strcmp(operation->valuestring, "start");
      if (!cJSON_IsString(id) ||
          (!fields(request, action_fields, 3) &&
           !(start && fields(request, start_fields, 4))))
        {
          goto done;
        }

      for (index = 0; index < g_count; index++)
        {
          if (!strcmp(g_apps[index].id, id->valuestring))
            {
              break;
            }
        }

      if (index == g_count)
        {
          ret = -ENOENT;
          goto done;
        }

      if (start)
        {
          cJSON *argument = item(request, "argument");
          if (argument && (!cJSON_IsNumber(argument) ||
              argument->valuedouble < 1 || argument->valuedouble > 3600 ||
              argument->valuedouble != argument->valueint))
            {
              goto done;
            }

          ret = g_module ? -EBUSY :
            start_locked(index, argument ? argument->valueint : 60);
        }
      else
        {
          if (g_running[0] && strcmp(g_running, id->valuestring))
            {
              ret = -EBUSY;
            }
          else
            {
              finish_locked(-ECANCELED);
              ret = stop_locked();
            }
        }

      if (!ret)
        {
          g_revision++;
          g_result = 0;
        }

      goto done;
    }

  if (g_module)
    {
      ret = -EBUSY;
      goto done;
    }

  next = cJSON_Duplicate(g_record, true);
  if (!next)
    {
      ret = -ENOMEM;
      goto done;
    }

  if (!strcmp(operation->valuestring, "trust") &&
      fields(request, trust_fields, 3))
    {
      mbedtls_pk_context key;
      mbedtls_pk_init(&key);
      ret = g_count ? -EBUSY : public_key(item(request, "public_key"), &key);
      mbedtls_pk_free(&key);
      if (ret)
        {
          goto done;
        }

      cJSON *copy = cJSON_Duplicate(item(request, "public_key"), true);
      if (!copy || !cJSON_ReplaceItemInObjectCaseSensitive(next,
                                                        "public_key", copy))
        {
          cJSON_Delete(copy);
          ret = -ENOMEM;
          goto done;
        }
    }
  else if (!strcmp(operation->valuestring, "install") &&
           fields(request, install_fields, 3))
    {
      struct native_info_s info;
      cJSON *package = item(request, "package");
      char *encoded = cJSON_PrintUnformatted(package);
      if (!encoded)
        {
          ret = -ENOMEM;
          goto done;
        }

      size_t length = strlen(encoded);
      free(encoded);
      if (length > BKNATIVE_PACKAGE_MAX)
        {
          ret = -EFBIG;
          goto done;
        }

      ret = verify(package, item(next, "public_key"), &info, NULL, NULL);
      if (ret)
        {
          goto done;
        }

      for (index = 0; index < g_count; index++)
        {
          if (!strcmp(g_apps[index].id, info.id))
            {
              break;
            }
        }

      if (index < g_count && info.version <= g_apps[index].version)
        {
          ret = -ESTALE;
          goto done;
        }

      if (index == g_count && g_count == BKNATIVE_APP_MAX)
        {
          ret = -ENOSPC;
          goto done;
        }

      cJSON *copy = cJSON_Duplicate(package, true);
      cJSON *packages = item(next, "packages");
      if (!copy || !(index < g_count ?
          cJSON_ReplaceItemInArray(packages, index, copy) :
          cJSON_AddItemToArray(packages, copy)))
        {
          cJSON_Delete(copy);
          ret = -ENOMEM;
          goto done;
        }
    }
  else if (!strcmp(operation->valuestring, "uninstall") &&
           fields(request, action_fields, 3) && cJSON_IsString(id))
    {
      for (index = 0; index < g_count; index++)
        {
          if (!strcmp(g_apps[index].id, id->valuestring))
            {
              break;
            }
        }

      if (index == g_count)
        {
          ret = -ENOENT;
          goto done;
        }

      cJSON_DeleteItemFromArray(item(next, "packages"), index);
    }
  else
    {
      ret = -EINVAL;
      goto done;
    }

  ret = commit(next);
  if (!ret)
    {
      next = NULL;
    }

done:
  if (!ret)
    {
      publish_locked();
    }

  pthread_mutex_unlock(&g_lock);
  cJSON_Delete(next);
  cJSON_Delete(request);
  return ret;
}

int bknative_launch(uint32_t hash, uint32_t seconds)
{
  unsigned int index = 0;
  int ret = bknative_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  if (!hash || seconds > BKNATIVE_INTENT_SECONDS_MAX)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_lock);
  ret = g_error ? g_error : g_module ? -EBUSY : find_hash_locked(hash, &index);
  if (!ret)
    {
      ret = start_locked(index, seconds ? seconds : 60);
      g_result = ret;
      g_revision++;
      publish_locked();
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bknative_stop_app(uint32_t hash)
{
  int ret = bknative_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  pthread_mutex_lock(&g_lock);
  if (g_error)
    {
      ret = g_error;
    }
  else if (!g_module)
    {
      ret = -EALREADY;
    }
  else if (bknative_app_hash(g_running) != hash)
    {
      ret = -EBUSY;
    }
  else
    {
      finish_locked(-ECANCELED);
      ret = stop_locked();
      g_result = ret ? ret : -ECANCELED;
      g_last_value = 0;
      g_revision++;
      publish_locked();
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

void bknative_set_clock(bool valid, uint32_t minute_of_day, uint32_t day)
{
  if (bknative_require_cpu0() < 0)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  valid = valid && minute_of_day < 1440;
  bool changed = valid != g_clock_valid;
  g_clock_valid = valid;
  g_clock_minute = valid ? minute_of_day : 0;
  g_clock_day = valid ? day : 0;
  if (changed && !g_error)
    {
      publish_locked();
    }

  pthread_mutex_unlock(&g_lock);
}

int bknative_schedule_set(uint32_t hash, uint32_t minute, uint32_t days,
                          uint32_t seconds)
{
  struct native_schedule_s next[BKNATIVE_SCHEDULE_MAX];
  unsigned int index = 0;
  unsigned int slot;
  int ret = bknative_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  if (!hash || minute >= 1440 || !days || (days & ~BKNATIVE_DAYS_ALL) ||
      seconds > BKNATIVE_INTENT_SECONDS_MAX)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_lock);
  ret = g_error ? g_error : g_schedule_error ? g_schedule_error :
        find_hash_locked(hash, &index);
  if (!ret)
    {
      /* (app, minute) names one entry: repeat requests replace it. */

      memcpy(next, g_schedule, sizeof(next));
      for (slot = 0; slot < BKNATIVE_SCHEDULE_MAX; slot++)
        {
          if (next[slot].hash == hash && next[slot].minute == minute)
            {
              break;
            }
        }

      for (unsigned int free_slot = 0; slot == BKNATIVE_SCHEDULE_MAX &&
           free_slot < BKNATIVE_SCHEDULE_MAX; free_slot++)
        {
          if (!next[free_slot].hash)
            {
              slot = free_slot;
            }
        }

      if (slot == BKNATIVE_SCHEDULE_MAX)
        {
          ret = -ENOSPC;
        }
      else
        {
          next[slot].hash = hash;
          next[slot].minute = minute;
          next[slot].days = days;
          next[slot].seconds = seconds;
          next[slot].last_result = 1;
          next[slot].fired_day = g_clock_valid &&
            minute <= g_clock_minute ? g_clock_day : 0;
          ret = schedule_commit_locked(next);
        }
    }

  if (!ret)
    {
      g_revision++;
      publish_locked();
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bknative_schedule_clear(uint32_t hash)
{
  struct native_schedule_s next[BKNATIVE_SCHEDULE_MAX];
  bool found = false;
  int ret = bknative_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  pthread_mutex_lock(&g_lock);
  ret = g_error ? g_error : g_schedule_error;
  if (!ret)
    {
      memcpy(next, g_schedule, sizeof(next));
      for (unsigned int slot = 0; slot < BKNATIVE_SCHEDULE_MAX; slot++)
        {
          if (hash && next[slot].hash == hash)
            {
              memset(&next[slot], 0, sizeof(next[slot]));
              found = true;
            }
        }

      ret = found ? schedule_commit_locked(next) : -ENOENT;
    }

  if (!ret)
    {
      g_revision++;
      publish_locked();
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bknative_schedule_step(bool admitted)
{
  int started = 0;
  if (bknative_require_cpu0() < 0)
    {
      return 0;
    }

  pthread_mutex_lock(&g_lock);
  uint32_t weekday = (g_clock_day + 4u) % 7u;
  for (unsigned int slot = 0; !g_error && g_clock_valid &&
       slot < BKNATIVE_SCHEDULE_MAX; slot++)
    {
      struct native_schedule_s *entry = &g_schedule[slot];
      unsigned int index = 0;
      if (!entry->hash || !(entry->days & (1u << weekday)) ||
          entry->minute != g_clock_minute || entry->fired_day == g_clock_day)
        {
          continue;
        }

      /* An occurrence is retried throughout its own minute while voice or
       * another app holds the device, then left as -EBUSY.  Once attempted
       * with admission it is final for the day; there is no catch-up queue.
       */

      if (!admitted || g_module)
        {
          if (entry->last_result == -EBUSY)
            {
              continue;
            }

          entry->last_result = -EBUSY;
        }
      else
        {
          entry->fired_day = g_clock_day;
          entry->last_result = find_hash_locked(entry->hash, &index);
          if (!entry->last_result)
            {
              entry->last_result = start_locked(index, entry->seconds ?
                                                entry->seconds : 60);
              g_result = entry->last_result;
              started = entry->last_result ? 0 : 1;
            }
        }

      g_revision++;
      syslog(LOG_INFO, "BKNATIVE schedule slot=%u minute=%lu result=%d\n",
             slot, (unsigned long)entry->minute, entry->last_result);
      publish_locked();
    }

  pthread_mutex_unlock(&g_lock);
  return started;
}

int bknative_read(char *output, size_t capacity)
{
  cJSON *object;
  cJSON *apps = NULL;
  char *serialized = NULL;
  char revision[24];
  char fingerprint[65] = "";
  uint8_t key[256];
  uint8_t digest[32];
  size_t key_size;
  int ret;

  ret = bknative_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  object = cJSON_CreateObject();
  ret = -ENOMEM;
  pthread_mutex_lock(&g_lock);
  if (g_error)
    {
      ret = g_error;
      goto done;
    }

  if (!decode(item(g_record, "public_key"), key, sizeof(key), &key_size) &&
      !mbedtls_sha256(key, key_size, digest, 0))
    {
      hex(digest, fingerprint);
    }

  snprintf(revision, sizeof(revision), "%llu",
             (unsigned long long)g_revision);
  if (!object || !cJSON_AddStringToObject(object, "revision", revision) ||
      !cJSON_AddNumberToObject(object, "abi", BKNATIVE_ABI) ||
      !cJSON_AddNumberToObject(object, "abi_max", BKNATIVE_ABI_V3) ||
      !cJSON_AddStringToObject(object, "target", "bk7258-ap-armv8m") ||
      !cJSON_AddStringToObject(object, "trusted_key_sha256", fingerprint) ||
      !cJSON_AddStringToObject(object, "publisher_key_sha256",
                               CONFIG_BK7258_NATIVE_PUBLISHER_SHA256) ||
      !cJSON_AddNumberToObject(object, "capacity", BKNATIVE_APP_MAX) ||
      !cJSON_AddNumberToObject(object, "max_package_bytes",
                               BKNATIVE_PACKAGE_MAX) ||
      !cJSON_AddNumberToObject(object, "storage_limit",
                               BKPROV_STORE_RECORD_MAX) ||
      !cJSON_AddStringToObject(object, "running", g_running) ||
      !cJSON_AddNumberToObject(object, "last_result", g_result) ||
      !cJSON_AddStringToObject(object, "last_app", g_last_app) ||
      !cJSON_AddNumberToObject(object, "last_value", g_last_value) ||
      !cJSON_AddBoolToObject(object, "clock_valid", g_clock_valid) ||
      !cJSON_AddNumberToObject(object, "schedule_error", g_schedule_error) ||
      !cJSON_AddNumberToObject(object, "fault_limit", BKNATIVE_FAULT_LIMIT) ||
      !(apps = cJSON_AddArrayToObject(object, "items")))
    {
      goto done;
    }

  for (unsigned int index = 0; index < g_count; index++)
    {
      cJSON *app = cJSON_CreateObject();
      cJSON *permissions = NULL;
      if (!app || !cJSON_AddItemToArray(apps, app))
        {
          cJSON_Delete(app);
          goto done;
        }

      if (!cJSON_AddStringToObject(app, "id", g_apps[index].id) ||
          !cJSON_AddStringToObject(app, "name", g_apps[index].name) ||
          !cJSON_AddStringToObject(app, "description",
                                   g_apps[index].description) ||
          !cJSON_AddNumberToObject(app, "version", g_apps[index].version) ||
          !cJSON_AddStringToObject(app, "sha256", g_apps[index].sha256) ||
          !cJSON_AddNumberToObject(app, "abi", g_apps[index].abi) ||
          !cJSON_AddNumberToObject(app, "faults", g_apps[index].faults) ||
          !cJSON_AddBoolToObject(app, "blocked",
            g_apps[index].faults >= BKNATIVE_FAULT_LIMIT) ||
          !(permissions = cJSON_AddArrayToObject(app, "permissions")))
        {
          goto done;
        }

      if (g_apps[index].result_count)
        {
          cJSON *results = cJSON_AddArrayToObject(app, "results");
          for (unsigned int label = 0; results &&
               label < g_apps[index].result_count; label++)
            {
              cJSON *text = cJSON_CreateString(g_apps[index].results[label]);
              if (!text || !cJSON_AddItemToArray(results, text))
                {
                  cJSON_Delete(text);
                  results = NULL;
                }
            }

          if (!results)
            {
              goto done;
            }
        }

      for (unsigned int bit = 0; bit < BKNATIVE_PERMIT_COUNT; bit++)
        {
          if (!(g_apps[index].permissions & (1u << bit)))
            {
              continue;
            }

          cJSON *name = cJSON_CreateString(g_permission_names[bit]);
          if (!name || !cJSON_AddItemToArray(permissions, name))
            {
              cJSON_Delete(name);
              goto done;
            }
        }
    }

  cJSON *schedule = cJSON_AddArrayToObject(object, "schedule");
  for (unsigned int slot = 0; schedule && slot < BKNATIVE_SCHEDULE_MAX;
       slot++)
    {
      const struct native_schedule_s *entry = &g_schedule[slot];
      unsigned int index = 0;
      cJSON *row;
      if (!entry->hash)
        {
          continue;
        }

      row = cJSON_CreateObject();
      if (row && !cJSON_AddItemToArray(schedule, row))
        {
          cJSON_Delete(row);
          row = NULL;
        }

      /* Once attached, the row is owned by the response object. */

      if (!row ||
          !cJSON_AddStringToObject(row, "id",
            find_hash_locked(entry->hash, &index) ? "" : g_apps[index].id) ||
          !cJSON_AddNumberToObject(row, "minute", entry->minute) ||
          !cJSON_AddNumberToObject(row, "days", entry->days) ||
          !cJSON_AddNumberToObject(row, "seconds", entry->seconds) ||
          !cJSON_AddNumberToObject(row, "last_result", entry->last_result))
        {
          schedule = NULL;
        }
    }

  if (!schedule)
    {
      goto done;
    }

  serialized = cJSON_PrintUnformatted(object);
  if (serialized)
    {
      ret = strlen(serialized) >= capacity ? -ENOSPC : 0;
      if (!ret)
        {
          strcpy(output, serialized);
        }
    }

done:
  pthread_mutex_unlock(&g_lock);
  free(serialized);
  cJSON_Delete(object);
  return ret;
}

void bknative_post_events(uint32_t events)
{
  if (bknative_require_cpu0() < 0)
    {
      return;
    }

  pthread_mutex_lock(&g_lock);
  if (g_module && (g_running_permissions & BKNATIVE_PERMIT_MOTION))
    {
      g_events |= events & BKNATIVE_EVENT_ALL;
    }

  pthread_mutex_unlock(&g_lock);
}

bool bknative_motion_owner(void)
{
  bool owner;
  if (bknative_require_cpu0() < 0)
    {
      return false;
    }

  pthread_mutex_lock(&g_lock);
  owner = g_module && (g_running_permissions & BKNATIVE_PERMIT_MOTION);
  pthread_mutex_unlock(&g_lock);
  return owner;
}

/* Calls one program step and validates everything it asked for against the
 * signed grants.  Returns the program result or a negative violation.
 */

static int step_locked(uint32_t elapsed, struct bknative_output3_s *output)
{
  struct bknative_input3_s input;
  uint64_t begin;
  int ret;

  memset(&input, 0, sizeof(input));
  input.base.events = g_events;
  input.base.random = next_random();
  input.minute_of_day = BKNATIVE_TIME_UNKNOWN;
  input.day = BKNATIVE_TIME_UNKNOWN;
  if (g_clock_valid && (g_running_permissions & BKNATIVE_PERMIT_CLOCK))
    {
      input.minute_of_day = g_clock_minute;
      input.day = g_clock_day;
    }

  begin = monotonic_us();
  if (g_program->abi == BKNATIVE_ABI)
    {
      ret = g_program->step(g_state.bytes, elapsed, &output->base.frame);
    }
  else
    {
      g_events = 0;
      ret = g_program->abi == BKNATIVE_ABI_V2 ?
            g_program->step2(g_state.bytes, elapsed, &input.base,
                             &output->base) :
            g_program->step3(g_state.bytes, elapsed, &input, output);
    }

  if (ret >= 0 && monotonic_us() - begin > BKNATIVE_CALLBACK_BUDGET_US)
    {
      return -ETIMEDOUT;
    }

  if (ret < 0 || ret > 1)
    {
      return ret < 0 ? ret : -ERANGE;
    }

  if ((output->base.haptic &&
       !(g_running_permissions & BKNATIVE_PERMIT_HAPTIC)) ||
      (output->base.cue && !(g_running_permissions & BKNATIVE_PERMIT_SOUND)))
    {
      return -EPERM;
    }

  if (output->base.frame.expression > 3 ||
      output->base.frame.duration_ms > 5000 ||
      (output->base.frame.expression && !output->base.frame.duration_ms) ||
      output->base.frame.progress > 100 || output->base.haptic > 1 ||
      output->base.cue > BKNATIVE_CUE_RHYTHM ||
      (output->base.cue && ret != 1) || output->reserved ||
      output->result > g_apps[g_running_index].result_count ||
      (output->result && ret != 1))
    {
      return -ERANGE;
    }

  return ret;
}

int bknative_step(uint64_t now, bool admitted,
                  struct bknative_output_s *output)
{
  int ret = 0;
  memset(output, 0, sizeof(*output));

  ret = bknative_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  pthread_mutex_lock(&g_lock);
  if (g_module && !admitted)
    {
      finish_locked(-ECANCELED);
      ret = stop_locked();
      g_result = ret ? ret : -ECANCELED;
      g_last_value = 0;
      g_revision++;
      publish_locked();
    }
  else if (g_module && (!g_last_step || now - g_last_step >= 100))
    {
      if (!g_started)
        {
          g_started = now;
        }

      struct bknative_output3_s full;
      memset(&full, 0, sizeof(full));
      g_last_step = now;
      ret = step_locked((uint32_t)(now - g_started), &full);
      *output = full.base;
      if (ret)
        {
          /* A completed program keeps only its completion feedback; a
           * failed one publishes nothing from the failing step.
           */

          uint32_t haptic = ret == 1 ? full.base.haptic : 0;
          uint32_t cue = ret == 1 ? full.base.cue : 0;
          int result = ret < 0 ? ret : 0;
          int saved = 0;

          /* Saved state is written only after a clean completion, and only
           * when it changed.  A write failure is reported, not a fault.
           */

          if (ret == 1 && g_saved_size &&
              memcmp(g_saved, g_state.bytes, g_saved_size))
            {
              saved = state_save(g_running, g_state.bytes, g_saved_size);
              if (saved)
                {
                  syslog(LOG_WARNING, "BKNATIVE save app=%s result=%d\n",
                         g_running, saved);
                }
            }

          g_last_value = ret == 1 ? full.result : 0;
          finish_locked(result);
          int cleanup = stop_locked();
          g_result = cleanup ? cleanup : result ? result : saved;
          g_revision++;
          memset(output, 0, sizeof(*output));
          output->haptic = haptic;
          output->cue = cue;
          ret = cleanup;
          publish_locked();
        }
    }

  if (ret >= 0)
    {
      ret = g_module ? 1 : 0;
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

int bknative_control(enum bkcontrol_command_e command, uint32_t offset,
                     const uint8_t *record, size_t size,
                     struct bkcontrol_status_s *status)
{
  int owner = bknative_require_cpu0();
  if (owner < 0)
    {
      return owner;
    }

  if (command == BKCONTROL_CONFIG_BEGIN)
    {
      return size && size <= BKNATIVE_REQUEST_MAX ? 0 : -EINVAL;
    }

  if (command == BKCONTROL_CONFIG_APPLY)
    {
      if (!record || !size || size > BKNATIVE_REQUEST_MAX ||
          memchr(record, 0, size))
        {
          return -EINVAL;
        }

      char *input = strndup((const char *)record, size);
      int ret = input ? bknative_apply(input) : -ENOMEM;
      free(input);
      return ret;
    }

  if (command != BKCONTROL_CONFIG_READ || (offset & 15u))
    {
      return -EINVAL;
    }

  /* Four apps with full ABI 3 labels and schedules can exceed 4 KiB. */

  char *output = malloc(BKNATIVE_READ_MAX);
  int ret = output ? bknative_read(output, BKNATIVE_READ_MAX) : -ENOMEM;
  if (!ret)
    {
      size_t length = strlen(output);
      status->config_total = 16 + length;
      memset(status->config_chunk, 0, 16);
      if (!offset)
        {
          cJSON *object = cJSON_Parse(output);
          cJSON *revision = item(object, "revision");
          if (!cJSON_IsString(revision))
            {
              ret = -ENOMEM;
            }
          else
            {
              uint64_t value = strtoull(revision->valuestring, NULL, 10);
              memcpy(status->config_chunk, "AJV1", 4);
              for (int index = 7; index >= 4; index--)
                {
                  status->config_chunk[index] = length;
                  length >>= 8;
                }

              for (int index = 15; index >= 8; index--)
                {
                  status->config_chunk[index] = value;
                  value >>= 8;
                }
            }

          cJSON_Delete(object);
        }
      else if (offset - 16 >= length)
        {
          ret = -ERANGE;
        }
      else
        {
          size_t chunk = length - (offset - 16);
          memcpy(status->config_chunk, output + offset - 16,
                   chunk < 16 ? chunk : 16);
        }
    }

  free(output);
  return ret;
}
