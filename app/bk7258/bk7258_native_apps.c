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
static uint64_t g_started;
static uint64_t g_last_step;

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
    "sha256", "image_size", "permissions"
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
  if (!fields(object, manifest_fields, 9) ||
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
      item(object, "abi")->valuedouble != BKNATIVE_ABI ||
      !cJSON_IsString(item(object, "target")) ||
      strcmp(item(object, "target")->valuestring, "bk7258-ap-armv8m") ||
      !cJSON_IsArray(item(object, "permissions")) ||
      cJSON_GetArraySize(item(object, "permissions")) != 1 ||
      !cJSON_IsString(cJSON_GetArrayItem(item(object, "permissions"), 0)) ||
      strcmp(cJSON_GetArrayItem(item(object, "permissions"), 0)->valuestring,
             "display"))
    {
      goto done;
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
      memset(&g_state, 0, sizeof(g_state));
    }

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
  int ret = stop_locked();
  if (running)
    {
      g_result = ret ? ret : reason;
      g_revision++;
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
done:
  g_error = ret;
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
      g_store_revision++;
      g_revision++;
      cJSON_Delete(g_record);
      g_record = next;
      memcpy(g_apps, infos, sizeof(g_apps));
      g_count = count;
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
  int ret = verify(cJSON_GetArrayItem(item(g_record, "packages"), index),
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
    }

  if (unlink(BKNATIVE_ROOT "/running.elf") < 0 && !ret)
    {
      ret = -errno;
    }

  if (!ret)
    {
      if (!g_program || g_program->abi != BKNATIVE_ABI ||
          g_program->state_size > sizeof(g_state) ||
          !g_program->start || !g_program->step)
        {
          ret = -ENOEXEC;
        }
      else
        {
          memset(&g_state, 0, sizeof(g_state));
          ret = g_program->start(g_state.bytes, argument);
        }
    }

  if (ret)
    {
      int cleanup = stop_locked();
      return cleanup ? cleanup : ret;
    }

  strcpy(g_running, info.id);
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
          ret = g_running[0] && strcmp(g_running, id->valuestring) ?
            -EBUSY : stop_locked();
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
  pthread_mutex_unlock(&g_lock);
  cJSON_Delete(next);
  cJSON_Delete(request);
  return ret;
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
          !(permissions = cJSON_AddArrayToObject(app, "permissions")))
        {
          goto done;
        }

      cJSON *display = cJSON_CreateString("display");
      if (!display || !cJSON_AddItemToArray(permissions, display))
        {
          cJSON_Delete(display);
          goto done;
        }
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

int bknative_step(uint64_t now, bool admitted,
                  struct bknative_frame_s *frame)
{
  int ret = 0;
  memset(frame, 0, sizeof(*frame));

  ret = bknative_require_cpu0();
  if (ret < 0)
    {
      return ret;
    }

  pthread_mutex_lock(&g_lock);
  if (g_module && !admitted)
    {
      ret = stop_locked();
      g_result = ret ? ret : -ECANCELED;
      g_revision++;
    }
  else if (g_module && (!g_last_step || now - g_last_step >= 100))
    {
      if (!g_started)
        {
          g_started = now;
        }

      g_last_step = now;
      ret = g_program->step(g_state.bytes, (uint32_t)(now - g_started),
                             frame);
      if (!ret && (frame->expression > 3 ||
                   frame->duration_ms > 5000 ||
                   (frame->expression && !frame->duration_ms) ||
                   frame->progress > 100))
        {
          ret = -ERANGE;
        }

      if (ret)
        {
          int cleanup = stop_locked();
          g_result = cleanup ? cleanup : ret < 0 ? ret : 0;
          g_revision++;
          memset(frame, 0, sizeof(*frame));
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

  char *output = malloc(4096);
  int ret = output ? bknative_read(output, 4096) : -ENOMEM;
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
