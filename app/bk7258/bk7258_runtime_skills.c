/****************************************************************************
 * apps/bk7258/bk7258_runtime_skills.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_runtime_skills.h"
#include "bk7258_runtime_skills_store.h"
#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
#include <mbedtls/ecp.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

/****************************************************************************
 * Private Types and Data
 ****************************************************************************/

#define CAP_FOCUS 1u
#define CAP_STATUS 2u
#define CAP_MOTION 4u
#define CAP_CAMERA 8u
#define CAP_EXPRESSION 16u

struct skill_s
{
  const char *id;
  const char *trigger;
  const char *markdown;
  unsigned int caps;
  bool enabled;
};

static struct skill_s g_skills[] =
{
  { "focus-coach", "专注助手",
    "# focus-coach\n1. Start `focus_timer` with action=start and seconds.\n"
    "2. Check status; use pause, resume, or cancel only when requested.",
    CAP_FOCUS | CAP_STATUS | CAP_EXPRESSION, true },
  { "device-check", "设备体检",
    "# device-check\n1. Check `device_status` first.\n"
    "2. Then use `device_motion`; report unavailable sensors honestly.",
    CAP_STATUS | CAP_MOTION | CAP_EXPRESSION, true },
  { "object-guide", "识物解说",
    "# object-guide\nCall `camera_capture` with one prompt. "
    "If capture fails, report that failure and do not invent a result.",
    CAP_CAMERA | CAP_STATUS | CAP_EXPRESSION, true },
};
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_revision = 1;
static uint64_t g_last_request;
static bool g_last_open;
static int g_last_result;
static struct
{
  uint64_t request;
  unsigned int caps;
  char id[32];
  char markdown[513];
  bool active;
} g_turn;
static uint8_t g_owner_key[BK7258_RUNTIME_SKILLS_MAX_KEY];
static size_t g_owner_key_size;
static uint8_t g_manifest[2048];
static size_t g_manifest_size;
static uint8_t g_signature[256];
static size_t g_signature_size;
struct custom_s
{
  bool present;
  char id[32];
  char name[65];
  char trigger[64];
  char markdown[513];
  unsigned int caps;
  bool enabled;
};

struct runtime_state_s
{
  uint64_t revision;
  bool builtin[3];
  uint8_t owner_key[BK7258_RUNTIME_SKILLS_MAX_KEY];
  size_t owner_key_size;
  uint8_t manifest[sizeof(g_manifest)];
  size_t manifest_size;
  uint8_t signature[sizeof(g_signature)];
  size_t signature_size;
  struct custom_s custom;
};

static struct custom_s g_custom;
static uint64_t g_store_revision;
static bool g_store_uncertain;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int install_locked(const uint8_t *manifest, size_t manifest_size,
                          const uint8_t *signature, size_t signature_size);

static cJSON *
json(const char *text)
{
  return text == NULL
             ? NULL
             : cJSON_ParseWithLengthOpts(text, strlen(text) + 1, NULL, true);
}

static bool
fields(const cJSON *object, const char *const *names, size_t count)
{
  const cJSON *item;
  bool seen[8] =
  {
    false
  };

  size_t i;
  if (!cJSON_IsObject(object) || count > sizeof(seen) / sizeof(seen[0]))
    {
      return false;
    }

  cJSON_ArrayForEach (item, object)
    {
      if (item->string == NULL)
        {
          return false;
        }

      for (i = 0; i < count; i++)
        {
          if (!strcmp(item->string, names[i]))
            {
              break;
            }
        }

      if (i == count || seen[i])
        {
          return false;
        }

      seen[i] = true;
    }

  for (i = 0; i < count; i++)
    {
      if (!seen[i])
        {
          return false;
        }
    }

  return true;
}

static int
valid_text(const char *text, size_t limit, bool lines)
{
  size_t size;

  if (text == NULL || (size = strnlen(text, limit + 1)) == 0 || size > limit)
    {
      return 0;
    }

  for (size_t i = 0; i < size; )
    {
      unsigned char lead = text[i++];
      unsigned char second;
      size_t following;

      if (lead < 0x20 && !(lines && (lead == '\n' || lead == '\t')))
        {
          return 0;
        }

      if (lead < 0x80)
        {
          continue;
        }

      if (lead >= 0xc2 && lead <= 0xdf)
        {
          following = 1;
        }
      else if (lead >= 0xe0 && lead <= 0xef)
        {
          following = 2;
        }
      else if (lead >= 0xf0 && lead <= 0xf4)
        {
          following = 3;
        }
      else
        {
          return 0;
        }

      if (i + following > size)
        {
          return 0;
        }

      second = (unsigned char)text[i];
      if ((second & 0xc0) != 0x80 || (lead == 0xe0 && second < 0xa0) ||
          (lead == 0xed && second > 0x9f) ||
          (lead == 0xf0 && second < 0x90) || (lead == 0xf4 && second > 0x8f))
        {
          return 0;
        }

      for (size_t j = 1; j < following; j++)
        {
          if (((unsigned char)text[i + j] & 0xc0) != 0x80)
            {
              return 0;
            }
        }

      i += following;
    }

  return 1;
}

static bool
trigger(const char *text, const char *name)
{
  size_t n = strlen(name);
  if (text == NULL || strncmp(text, name, n))
    {
      return false;
    }

  return text[n] == ':' || text[n] == ',' || text[n] == ' ' ||
         (text[n] == '\xef' && text[n + 1] &&
          (unsigned char)text[n + 1] == 0xbc && text[n + 2] &&
          (unsigned char)text[n + 2] == 0x8c) ||
         (text[n] == '\xef' && text[n + 1] &&
          (unsigned char)text[n + 1] == 0xbc && text[n + 2] &&
          (unsigned char)text[n + 2] == 0x9a);
}

static unsigned int
cap_for(const char *tool)
{
  if (!strcmp(tool, "focus_timer"))
    {
      return CAP_FOCUS;
    }

  if (!strcmp(tool, "device_status"))
    {
      return CAP_STATUS;
    }

  if (!strcmp(tool, "device_motion"))
    {
      return CAP_MOTION;
    }

  if (!strcmp(tool, "camera_capture"))
    {
      return CAP_CAMERA;
    }

  if (!strcmp(tool, "reply_expression"))
    {
      return CAP_EXPRESSION;
    }

  return 0;
}

static int
validate(const char *tool, const char *input)
{
  static const char *const action[] =
  {
    "action"
  };

  static const char *const start[] =
  {
    "action", "seconds"
  };

  static const char *const emotion[] =
  {
    "emotion"
  };

  static const char *const prompt[] =
  {
    "prompt"
  };

  cJSON *object = json(input);
  cJSON *value;
  int ret = -EINVAL;
  if (!object)
    {
      return ret;
    }

  if (!strcmp(tool, "focus_timer"))
    {
      value = cJSON_GetObjectItemCaseSensitive(object, "action");
      if (cJSON_IsString(value) && !strcmp(value->valuestring, "start"))
        {
          cJSON *seconds =
              cJSON_GetObjectItemCaseSensitive(object, "seconds");
          if (fields(object, start, 2) && cJSON_IsNumber(seconds) &&
              seconds->valuedouble == seconds->valueint &&
              seconds->valueint > 0)
            {
              ret = 0;
            }
        }
      else if (cJSON_IsString(value) &&
               (!strcmp(value->valuestring, "status") ||
                !strcmp(value->valuestring, "pause") ||
                !strcmp(value->valuestring, "resume") ||
                !strcmp(value->valuestring, "cancel")) &&
               fields(object, action, 1))
        {
          ret = 0;
        }
    }
  else if (!strcmp(tool, "reply_expression"))
    {
      value = cJSON_GetObjectItemCaseSensitive(object, "emotion");
      if (fields(object, emotion, 1) && cJSON_IsString(value) &&
          (!strcmp(value->valuestring, "neutral") ||
           !strcmp(value->valuestring, "happy") ||
           !strcmp(value->valuestring, "confused") ||
           !strcmp(value->valuestring, "thinking") ||
           !strcmp(value->valuestring, "sorry")))
        {
          ret = 0;
        }
    }
  else if (!strcmp(tool, "camera_capture"))
    {
      value = cJSON_GetObjectItemCaseSensitive(object, "prompt");
      if (fields(object, prompt, 1) && cJSON_IsString(value) &&
          value->valuestring[0] && strlen(value->valuestring) <= 1024)
        {
          ret = 0;
        }
    }
  else if (fields(object, NULL, 0))
    {
      ret = 0;
    }

  cJSON_Delete(object);
  return ret;
}

static int
print_json(const cJSON *object, char *out, size_t size)
{
  char *rendered = cJSON_PrintUnformatted(object);
  if (!rendered)
    {
      return -ENOMEM;
    }

  if (strlen(rendered) >= size)
    {
      cJSON_free(rendered);
      return -ENOSPC;
    }

  strcpy(out, rendered);
  cJSON_free(rendered);
  return 0;
}

static bool
add_permissions(cJSON *object, const char *key, unsigned int caps)
{
  const char *names[6];
  int count = 0;
  cJSON *array;

  if (caps & CAP_FOCUS)
    {
      names[count++] = "focus_timer";
    }

  if (caps & CAP_STATUS)
    {
      names[count++] = "device_status";
    }

  if (caps & CAP_MOTION)
    {
      names[count++] = "device_motion";
    }

  if (caps & CAP_CAMERA)
    {
      names[count++] = "camera_capture";
    }

  if (caps & CAP_EXPRESSION)
    {
      names[count++] = "reply_expression";
    }

  names[count++] = "read_file";
  array = cJSON_CreateStringArray(names, count);
  if (!array || !cJSON_AddItemToObject(object, key, array))
    {
      cJSON_Delete(array);
      return false;
    }

  return true;
}

static void
state_get(struct runtime_state_s *state)
{
  size_t i;

  memset(state, 0, sizeof(*state));
  state->revision = g_revision;
  for (i = 0; i < sizeof(g_skills) / sizeof(g_skills[0]); i++)
    {
      state->builtin[i] = g_skills[i].enabled;
    }

  memcpy(state->owner_key, g_owner_key, g_owner_key_size);
  state->owner_key_size = g_owner_key_size;
  memcpy(state->manifest, g_manifest, g_manifest_size);
  state->manifest_size = g_manifest_size;
  memcpy(state->signature, g_signature, g_signature_size);
  state->signature_size = g_signature_size;
  state->custom = g_custom;
}

static void
state_put(const struct runtime_state_s *state)
{
  size_t i;

  g_revision = state->revision;
  for (i = 0; i < sizeof(g_skills) / sizeof(g_skills[0]); i++)
    {
      g_skills[i].enabled = state->builtin[i];
    }

  memcpy(g_owner_key, state->owner_key, state->owner_key_size);
  g_owner_key_size = state->owner_key_size;
  memcpy(g_manifest, state->manifest, state->manifest_size);
  g_manifest_size = state->manifest_size;
  memcpy(g_signature, state->signature, state->signature_size);
  g_signature_size = state->signature_size;
  g_custom = state->custom;
}

static int
state_encode(const struct runtime_state_s *state, char *out, size_t size,
             size_t *actual)
{
  char *scratch;
  char *key;
  char *manifest;
  char *signature;
  size_t key_size;
  size_t manifest_size;
  size_t signature_size;
  int written;

  scratch = malloc(4 * ((BK7258_RUNTIME_SKILLS_MAX_KEY + 2) / 3) + 1 +
                   4 * ((sizeof(g_manifest) + 2) / 3) + 1 +
                   4 * ((sizeof(g_signature) + 2) / 3) + 1);
  if (!scratch)
    {
      return -ENOMEM;
    }

  key = scratch;
  manifest = key + 4 * ((BK7258_RUNTIME_SKILLS_MAX_KEY + 2) / 3) + 1;
  signature = manifest + 4 * ((sizeof(g_manifest) + 2) / 3) + 1;
  if (mbedtls_base64_encode(
          (unsigned char *)key,
          4 * ((BK7258_RUNTIME_SKILLS_MAX_KEY + 2) / 3) + 1, &key_size,
          state->owner_key, state->owner_key_size) ||
      mbedtls_base64_encode(
          (unsigned char *)manifest, 4 * ((sizeof(g_manifest) + 2) / 3) + 1,
          &manifest_size, state->manifest, state->manifest_size) ||
      mbedtls_base64_encode((unsigned char *)signature,
                            4 * ((sizeof(g_signature) + 2) / 3) + 1,
                            &signature_size, state->signature,
                            state->signature_size))
    {
      free(scratch);
      return -EOVERFLOW;
    }

  key[key_size] = 0;
  manifest[manifest_size] = 0;
  signature[signature_size] = 0;
  written = snprintf(
      out, size,
      "{\"revision\":\"%llu\",\"builtin\":[%s,%s,%s],\"key\":\"%s\","
      "\"manifest\":\"%s\",\"signature\":\"%s\",\"custom_enabled\":%s}",
      (unsigned long long)state->revision,
      state->builtin[0] ? "true" : "false",
      state->builtin[1] ? "true" : "false",
      state->builtin[2] ? "true" : "false", key, manifest, signature,
      state->custom.enabled ? "true" : "false");
  free(scratch);
  if (written < 0 || (size_t)written >= size)
    {
      return -ENOSPC;
    }

  *actual = (size_t)written;
  return 0;
}

static int
persist_locked(void)
{
  char *record;
  struct runtime_state_s *state;
  uint64_t revision;
  size_t size;
  int ret;

  if (g_store_uncertain)
    {
      return -EINPROGRESS;
    }

  record = malloc(BKRUNTIME_SKILLS_STORE_MAX);
  state = malloc(sizeof(*state));
  if (!record || !state)
    {
      free(record);
      free(state);
      return -ENOMEM;
    }

  state_get(state);
  ret = state_encode(state, record, BKRUNTIME_SKILLS_STORE_MAX, &size);
  if (!ret)
    {
      ret = bkruntime_skills_store_commit(g_store_revision, record, size,
                                          &revision);
    }

  free(state);
  free(record);
  if (!ret)
    {
      g_store_revision = revision;
    }
  else if (ret == -EINPROGRESS)
    {
      g_store_uncertain = true;
    }

  return ret;
}

static int
state_decode(const char *record, size_t size, struct runtime_state_s *state)
{
  static const char *const names[] =
  {
    "revision", "builtin", "key", "manifest", "signature", "custom_enabled"
  };

  cJSON *object = cJSON_ParseWithLengthOpts(record, size, NULL, false);
  cJSON *revision;
  cJSON *builtin;
  cJSON *key;
  cJSON *manifest;
  cJSON *signature;
  cJSON *enabled;
  char *end;
  size_t decoded;
  int i;

  if (!object || !fields(object, names, 6))
    {
      goto bad;
    }

  revision = cJSON_GetObjectItemCaseSensitive(object, "revision");
  builtin = cJSON_GetObjectItemCaseSensitive(object, "builtin");
  key = cJSON_GetObjectItemCaseSensitive(object, "key");
  manifest = cJSON_GetObjectItemCaseSensitive(object, "manifest");
  signature = cJSON_GetObjectItemCaseSensitive(object, "signature");
  enabled = cJSON_GetObjectItemCaseSensitive(object, "custom_enabled");
  if (!cJSON_IsString(revision) || !cJSON_IsArray(builtin) ||
      cJSON_GetArraySize(builtin) != 3 || !cJSON_IsString(key) ||
      !cJSON_IsString(manifest) || !cJSON_IsString(signature) ||
      !cJSON_IsBool(enabled))
    {
      goto bad;
    }

  errno = 0;
  state->revision = strtoull(revision->valuestring, &end, 10);
  if (errno || !revision->valuestring[0] || *end)
    {
      goto bad;
    }

  memset(state, 0, sizeof(*state));
  state->revision = strtoull(revision->valuestring, NULL, 10);
  for (i = 0; i < 3; i++)
    {
      cJSON *item = cJSON_GetArrayItem(builtin, i);
      if (!cJSON_IsBool(item))
        {
          goto bad;
        }

      state->builtin[i] = cJSON_IsTrue(item);
    }

  if (mbedtls_base64_decode(
          state->owner_key, sizeof(state->owner_key), &decoded,
          (const unsigned char *)key->valuestring, strlen(key->valuestring)))
    {
      goto bad;
    }

  state->owner_key_size = decoded;
  if (mbedtls_base64_decode(state->manifest, sizeof(state->manifest),
                            &decoded,
                            (const unsigned char *)manifest->valuestring,
                            strlen(manifest->valuestring)))
    {
      goto bad;
    }

  state->manifest_size = decoded;
  if (mbedtls_base64_decode(state->signature, sizeof(state->signature),
                            &decoded,
                            (const unsigned char *)signature->valuestring,
                            strlen(signature->valuestring)))
    {
      goto bad;
    }

  state->signature_size = decoded;
  state->custom.enabled = cJSON_IsTrue(enabled);
  cJSON_Delete(object);
  return 0;
bad:
  cJSON_Delete(object);
  return -EBADMSG;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int
bk7258_runtime_skills_init(void)
{
  char *record;
  struct runtime_state_s *saved;
  struct runtime_state_s *loaded;
  size_t actual;
  uint64_t revision;
  int ret;

  record = malloc(BKRUNTIME_SKILLS_STORE_MAX);
  saved = malloc(sizeof(*saved));
  loaded = malloc(sizeof(*loaded));
  if (!record || !saved || !loaded)
    {
      pthread_mutex_lock(&g_lock);
      g_store_uncertain = true;
      pthread_mutex_unlock(&g_lock);
      free(record);
      free(saved);
      free(loaded);
      return -ENOMEM;
    }

  ret = bkruntime_skills_store_load(record, BKRUNTIME_SKILLS_STORE_MAX,
                                    &actual, &revision);
  if (ret)
    {
      pthread_mutex_lock(&g_lock);
      g_store_uncertain = true;
      pthread_mutex_unlock(&g_lock);
      free(record);
      free(saved);
      free(loaded);
      return ret;
    }

  if (!actual)
    {
      pthread_mutex_lock(&g_lock);
      g_skills[0].enabled = true;
      g_skills[1].enabled = true;
      g_skills[2].enabled = true;
      g_revision = 1;
      g_last_request = 0;
      g_last_open = false;
      memset(&g_turn, 0, sizeof(g_turn));
      memset(g_owner_key, 0, sizeof(g_owner_key));
      g_owner_key_size = 0;
      memset(g_manifest, 0, sizeof(g_manifest));
      g_manifest_size = 0;
      memset(g_signature, 0, sizeof(g_signature));
      g_signature_size = 0;
      memset(&g_custom, 0, sizeof(g_custom));
      g_store_revision = 0;
      g_store_uncertain = false;
      pthread_mutex_unlock(&g_lock);
      free(record);
      free(saved);
      free(loaded);
      return 0;
    }

  ret = state_decode(record, actual, loaded);
  if (ret)
    {
      goto unavailable;
    }

  if ((loaded->manifest_size == 0) != (loaded->signature_size == 0) ||
      (loaded->manifest_size && !loaded->owner_key_size))
    {
      ret = -EBADMSG;
      goto unavailable;
    }

  if (loaded->owner_key_size)
    {
      mbedtls_pk_context key;

      mbedtls_pk_init(&key);
      ret = mbedtls_pk_parse_public_key(&key, loaded->owner_key,
                                        loaded->owner_key_size) ||
                    !mbedtls_pk_can_do(&key, MBEDTLS_PK_ECKEY) ||
                    mbedtls_pk_ec(key)->grp.id != MBEDTLS_ECP_DP_SECP256R1
                ? -EBADMSG
                : 0;
      mbedtls_pk_free(&key);
      if (ret)
        {
          goto unavailable;
        }
    }

  pthread_mutex_lock(&g_lock);
  state_get(saved);
  state_put(loaded);
  memset(&g_custom, 0, sizeof(g_custom));
  if (loaded->manifest_size)
    {
      ret = install_locked(loaded->manifest, loaded->manifest_size,
                           loaded->signature, loaded->signature_size);
    }

  if (!ret)
    {
      g_custom.enabled = loaded->custom.enabled;
      g_revision = loaded->revision;
      g_store_revision = revision;
      g_store_uncertain = false;
      g_last_request = 0;
      g_last_open = false;
      memset(&g_turn, 0, sizeof(g_turn));
      pthread_mutex_unlock(&g_lock);
      free(record);
      free(saved);
      free(loaded);
      return 0;
    }

  state_put(saved);
  g_store_uncertain = true;
  pthread_mutex_unlock(&g_lock);
  free(record);
  free(saved);
  free(loaded);
  return ret;
unavailable:
  pthread_mutex_lock(&g_lock);
  g_store_uncertain = true;
  pthread_mutex_unlock(&g_lock);
  free(record);
  free(saved);
  free(loaded);
  return ret;
}

int
bk7258_runtime_skills_begin(uint64_t request, const char *text)
{
  size_t i;
  int ret = 0;
  if (!request || !text)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_lock);
  if (g_store_uncertain)
    {
      ret = -EINPROGRESS;
    }
  else if (g_last_open && request == g_last_request)
    {
      ret = g_last_result;
    }
  else if (request <= g_last_request)
    {
      ret = -ESTALE;
    }
  else
    {
      g_turn.active = false;
      g_last_request = request;
      g_last_open = true;
      for (i = 0; i < sizeof(g_skills) / sizeof(g_skills[0]); i++)
        {
          if (trigger(text, g_skills[i].trigger))
            {
              ret = g_skills[i].enabled ? 1 : -EACCES;
              if (ret > 0)
                {
                  g_turn.request = request;
                  g_turn.caps = g_skills[i].caps;
                  snprintf(g_turn.id, sizeof(g_turn.id), "%s",
                           g_skills[i].id);
                  snprintf(g_turn.markdown, sizeof(g_turn.markdown), "%s",
                           g_skills[i].markdown);
                  g_turn.active = true;
                }
              break;
            }
        }

      if (!ret && g_custom.present && trigger(text, g_custom.trigger))
        {
          ret = g_custom.enabled ? 1 : -EACCES;
          if (ret > 0)
            {
              g_turn.request = request;
              g_turn.caps = g_custom.caps;
              snprintf(g_turn.id, sizeof(g_turn.id), "%s", g_custom.id);
              snprintf(g_turn.markdown, sizeof(g_turn.markdown), "%s",
                       g_custom.markdown);
              g_turn.active = true;
            }
        }

      g_last_result = ret;
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

bool
bk7258_runtime_skills_selected(uint64_t request)
{
  bool selected;
  pthread_mutex_lock(&g_lock);
  selected = g_turn.active && g_turn.request == request;
  pthread_mutex_unlock(&g_lock);
  return selected;
}

void
bk7258_runtime_skills_end(uint64_t request)
{
  pthread_mutex_lock(&g_lock);
  if (g_last_request == request)
    {
      g_last_open = false;
    }

  if (g_turn.active && g_turn.request == request)
    {
      g_turn.active = false;
    }

  pthread_mutex_unlock(&g_lock);
}

void
bk7258_runtime_skills_clear(void)
{
  pthread_mutex_lock(&g_lock);
  g_last_open = false;
  g_turn.active = false;
  g_turn.request = 0;
  g_turn.caps = 0;
  memset(g_turn.id, 0, sizeof(g_turn.id));
  memset(g_turn.markdown, 0, sizeof(g_turn.markdown));
  pthread_mutex_unlock(&g_lock);
}

int
bk7258_runtime_skills_enable(const char *id, bool enabled)
{
  size_t i;
  if (id == NULL)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_lock);
  for (i = 0; i < sizeof(g_skills) / sizeof(g_skills[0]); i++)
    {
      if (!strcmp(id, g_skills[i].id))
        {
          g_skills[i].enabled = enabled;
          g_revision++;
          pthread_mutex_unlock(&g_lock);
          return 0;
        }
    }

  pthread_mutex_unlock(&g_lock);
  return -ENOENT;
}

int
bk7258_runtime_skills_check(uint64_t request, const char *tool,
                            const char *input)
{
  unsigned int cap;
  int ret;
  if (tool == NULL)
    {
      return -EINVAL;
    }

  if (!strcmp(tool, "read_file"))
    {
      static const char *const names[] =
      {
        "path"
      };

      const char *ignored;
      cJSON *object = json(input);
      cJSON *path = cJSON_GetObjectItemCaseSensitive(object, "path");
      ret = fields(object, names, 1) && cJSON_IsString(path)
                ? bk7258_runtime_skills_read_file(request, path->valuestring,
                                                  &ignored)
                : -EINVAL;
      cJSON_Delete(object);
      return ret;
    }

  cap = cap_for(tool);
  pthread_mutex_lock(&g_lock);
  ret = g_store_uncertain
            ? -EINPROGRESS
            : (!g_turn.active || request != g_turn.request
                   ? -ESTALE
                   : (!cap || !(g_turn.caps & cap) ? -EACCES : 0));
  pthread_mutex_unlock(&g_lock);
  return ret ? ret : validate(tool, input);
}

int
bk7258_runtime_skills_prompt(uint64_t request, char *out, size_t size)
{
  int ret = 0;
  cJSON *object = NULL;

  if (!out || !size)
    {
      return -EINVAL;
    }

  out[0] = 0;
  pthread_mutex_lock(&g_lock);
  if (g_store_uncertain)
    {
      ret = -EINPROGRESS;
    }
  else if (g_turn.active && request == g_turn.request)
    {
      object = cJSON_CreateObject();
      if (!object || !cJSON_AddStringToObject(object, "id", g_turn.id) ||
          !add_permissions(object, "allowed_tools", g_turn.caps) ||
          !cJSON_AddStringToObject(object, "instructions", g_turn.markdown))
        {
          ret = -ENOMEM;
        }
      else
        {
          ret = print_json(object, out, size);
        }
    }

  pthread_mutex_unlock(&g_lock);
  cJSON_Delete(object);
  return ret;
}

const char *
bk7258_runtime_skills_markdown(const char *id)
{
  size_t i;
  if (!id)
    {
      return NULL;
    }

  for (i = 0; i < sizeof(g_skills) / sizeof(g_skills[0]); i++)
    {
      if (!strcmp(id, g_skills[i].id))
        {
          return g_skills[i].markdown;
        }
    }

  if (g_custom.present && !strcmp(id, g_custom.id))
    {
      return g_custom.markdown;
    }

  return NULL;
}

int
bk7258_runtime_skills_read_file(uint64_t request, const char *path,
                                const char **markdown)
{
  const char *prefix = "/data/agent/skills/";
  const char *id;
  int ret = -EACCES;
  if (!path || !markdown || strncmp(path, prefix, strlen(prefix)))
    {
      return -EACCES;
    }

  id = path + strlen(prefix);
  pthread_mutex_lock(&g_lock);
  if (g_store_uncertain)
    {
      ret = -EINPROGRESS;
    }
  else if (g_turn.active && request == g_turn.request && !strstr(id, "..") &&
           !strchr(id, '/') && strlen(id) > 3 &&
           !strcmp(id + strlen(id) - 3, ".md") &&
           !strncmp(id, g_turn.id, strlen(g_turn.id)) &&
           !strcmp(id + strlen(g_turn.id), ".md"))
    {
      *markdown = g_turn.markdown;
      ret = 0;
    }

  pthread_mutex_unlock(&g_lock);
  return ret;
}

static bool
add_skill(cJSON *items, const char *id, const char *name,
          const char *trigger_text, unsigned int caps, bool enabled,
          bool builtin)
{
  cJSON *item = cJSON_CreateObject();

  if (!item || !cJSON_AddStringToObject(item, "id", id) ||
      !cJSON_AddStringToObject(item, "name", name) ||
      !cJSON_AddStringToObject(item, "trigger", trigger_text) ||
      !cJSON_AddBoolToObject(item, "enabled", enabled) ||
      !cJSON_AddBoolToObject(item, "builtin", builtin) ||
      !cJSON_AddBoolToObject(item, "signed", !builtin) ||
      !add_permissions(item, "permissions", caps) ||
      !cJSON_AddItemToArray(items, item))
    {
      cJSON_Delete(item);
      return false;
    }

  return true;
}

int
bk7258_runtime_skills_app_read(char *out, size_t size)
{
  uint8_t hash[32];
  char hex[65] = "";
  char revision[24];
  cJSON *object = NULL;
  cJSON *items;
  int ret = -ENOMEM;

  if (!out || !size)
    {
      return -EINVAL;
    }

  out[0] = 0;
  pthread_mutex_lock(&g_lock);
  if (g_store_uncertain)
    {
      ret = -EINPROGRESS;
      goto out;
    }

  if (g_owner_key_size)
    {
      if (mbedtls_sha256(g_owner_key, g_owner_key_size, hash, 0))
        {
          ret = -EIO;
          goto out;
        }

      for (size_t i = 0; i < sizeof(hash); i++)
        {
          snprintf(hex + i * 2, 3, "%02x", hash[i]);
        }
    }

  snprintf(revision, sizeof(revision), "%llu",
           (unsigned long long)g_revision);
  object = cJSON_CreateObject();
  if (!object || !cJSON_AddStringToObject(object, "revision", revision) ||
      !cJSON_AddStringToObject(object, "trusted_key_sha256", hex))
    {
      goto out;
    }

  items = cJSON_AddArrayToObject(object, "items");
  if (!items)
    {
      goto out;
    }

  for (size_t i = 0; i < sizeof(g_skills) / sizeof(g_skills[0]); i++)
    {
      if (!add_skill(items, g_skills[i].id, g_skills[i].trigger,
                     g_skills[i].trigger, g_skills[i].caps,
                     g_skills[i].enabled, true))
        {
          goto out;
        }
    }

  if (g_custom.present &&
      !add_skill(items, g_custom.id, g_custom.name, g_custom.trigger,
                 g_custom.caps, g_custom.enabled, false))
    {
      goto out;
    }

  ret = print_json(object, out, size);
out:
  pthread_mutex_unlock(&g_lock);
  cJSON_Delete(object);
  return ret;
}

int
bk7258_runtime_skills_builtin_read(char *out, size_t size)
{
  cJSON *root;
  cJSON *items;
  char *rendered;

  if (!out || !size)
    {
      return -EINVAL;
    }

  root = cJSON_CreateObject();
  items = root ? cJSON_AddArrayToObject(root, "items") : NULL;
  pthread_mutex_lock(&g_lock);
  for (size_t i = 0; items && i < sizeof(g_skills) / sizeof(g_skills[0]);
       i++)
    {
      cJSON *item = cJSON_CreateObject();
      if (!item || !cJSON_AddItemToArray(items, item) ||
          !cJSON_AddStringToObject(item, "id", g_skills[i].id) ||
          !cJSON_AddStringToObject(item, "markdown", g_skills[i].markdown))
        {
          cJSON_Delete(root);
          pthread_mutex_unlock(&g_lock);
          return -ENOMEM;
        }
    }

  pthread_mutex_unlock(&g_lock);
  if (!items)
    {
      cJSON_Delete(root);
      return -ENOMEM;
    }

  rendered = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!rendered)
    {
      return -ENOMEM;
    }

  if (strlen(rendered) >= size)
    {
      cJSON_free(rendered);
      return -ENOSPC;
    }

  strcpy(out, rendered);
  cJSON_free(rendered);
  return 0;
}

int
bk7258_runtime_skills_app_apply(const char *json_text, char *out,
                                size_t size)
{
  static const char *const names[] =
  {
    "revision", "id", "enabled"
  };

  static const char *const trust_names[] =

  {
    "revision", "op", "public_key"
  };

  static const char *const install_names[] =

  {
    "revision", "op", "manifest", "signature"
  };

  cJSON *object = json(json_text);
  cJSON *revision = cJSON_GetObjectItemCaseSensitive(object, "revision");
  cJSON *id = cJSON_GetObjectItemCaseSensitive(object, "id");
  cJSON *enabled = cJSON_GetObjectItemCaseSensitive(object, "enabled");
  char expected[32];
  size_t i;
  int ret = -EINVAL;
  cJSON *op = cJSON_GetObjectItemCaseSensitive(object, "op");
  cJSON *key64 = cJSON_GetObjectItemCaseSensitive(object, "public_key");
  cJSON *manifest64 = cJSON_GetObjectItemCaseSensitive(object, "manifest");
  cJSON *signature64 = cJSON_GetObjectItemCaseSensitive(object, "signature");
  unsigned char key[256];
  unsigned char manifest[2049];
  unsigned char signature[256];
  size_t key_size = 0;
  size_t manifest_size = 0;
  size_t signature_size = 0;
  mbedtls_pk_context parsed;
  struct runtime_state_s *old;

  if (!object)
    {
      return -EINVAL;
    }

  old = malloc(sizeof(*old));
  if (!old)
    {
      cJSON_Delete(object);
      return -ENOMEM;
    }

  ret = 0;
  pthread_mutex_lock(&g_lock);
  if (g_turn.active)
    {
      ret = -EBUSY;
    }
  else if (g_store_uncertain)
    {
      ret = -EINPROGRESS;
    }

  pthread_mutex_unlock(&g_lock);
  if (ret)
    {
      goto out;
    }

  ret = -EINVAL;
  if (cJSON_IsString(op) && !strcmp(op->valuestring, "trust") &&
      fields(object, trust_names, 3) && cJSON_IsString(revision) &&
      cJSON_IsString(key64))
    {
      if (mbedtls_base64_decode(key, sizeof(key), &key_size,
                                (const unsigned char *)key64->valuestring,
                                strlen(key64->valuestring)))
        {
          goto out;
        }

      mbedtls_pk_init(&parsed);
      if (mbedtls_pk_parse_public_key(&parsed, key, key_size) ||
          !mbedtls_pk_can_do(&parsed, MBEDTLS_PK_ECKEY) ||
          mbedtls_pk_ec(parsed)->grp.id != MBEDTLS_ECP_DP_SECP256R1)
        {
          mbedtls_pk_free(&parsed);
          goto out;
        }

      mbedtls_pk_free(&parsed);
      pthread_mutex_lock(&g_lock);
      if (g_turn.active)
        {
          ret = -EBUSY;
        }
      else
        {
          snprintf(expected, sizeof(expected), "%llu",
                   (unsigned long long)g_revision);
          if (strcmp(expected, revision->valuestring))
            {
              ret = -EAGAIN;
            }
          else
            {
              state_get(old);
              memcpy(g_owner_key, key, key_size);
              g_owner_key_size = key_size;
              memset(&g_custom, 0, sizeof(g_custom));
              memset(g_manifest, 0, sizeof(g_manifest));
              g_manifest_size = 0;
              memset(g_signature, 0, sizeof(g_signature));
              g_signature_size = 0;
              g_revision++;
              ret = persist_locked();
              if (ret)
                {
                  state_put(old);
                }
            }
        }

      pthread_mutex_unlock(&g_lock);
      if (!ret)
        {
          ret = bk7258_runtime_skills_app_read(out, size);
        }

      goto out;
    }

  if (cJSON_IsString(op) && !strcmp(op->valuestring, "install") &&
      fields(object, install_names, 4) && cJSON_IsString(revision) &&
      cJSON_IsString(manifest64) && cJSON_IsString(signature64))
    {
      if (mbedtls_base64_decode(
              manifest, sizeof(manifest), &manifest_size,
              (const unsigned char *)manifest64->valuestring,
              strlen(manifest64->valuestring)) ||
          mbedtls_base64_decode(
              signature, sizeof(signature), &signature_size,
              (const unsigned char *)signature64->valuestring,
              strlen(signature64->valuestring)))
        {
          goto out;
        }

      pthread_mutex_lock(&g_lock);
      if (g_turn.active)
        {
          ret = -EBUSY;
        }
      else
        {
          snprintf(expected, sizeof(expected), "%llu",
                   (unsigned long long)g_revision);
          if (strcmp(expected, revision->valuestring))
            {
              ret = -EAGAIN;
            }
          else
            {
              state_get(old);
              ret = install_locked(manifest, manifest_size, signature,
                                   signature_size);
              if (!ret)
                {
                  g_revision++;
                  ret = persist_locked();
                  if (ret)
                    {
                      state_put(old);
                    }
                }
            }
        }

      pthread_mutex_unlock(&g_lock);
      if (!ret)
        {
          ret = bk7258_runtime_skills_app_read(out, size);
        }

      goto out;
    }

  if (!out || !size || !fields(object, names, 3) ||
      !cJSON_IsString(revision) || !cJSON_IsString(id) ||
      !cJSON_IsBool(enabled))
    {
      goto out;
    }

  pthread_mutex_lock(&g_lock);
  if (g_turn.active)
    {
      ret = -EBUSY;
    }
  else
    {
      snprintf(expected, sizeof(expected), "%llu",
               (unsigned long long)g_revision);
      if (strcmp(expected, revision->valuestring))
        {
          ret = -EAGAIN;
        }
      else
        {
          ret = -ENOENT;
          for (i = 0; i < sizeof(g_skills) / sizeof(g_skills[0]); i++)
            {
              if (!strcmp(id->valuestring, g_skills[i].id))
                {
                  state_get(old);
                  g_skills[i].enabled = cJSON_IsTrue(enabled);
                  g_revision++;
                  ret = persist_locked();
                  if (ret)
                    {
                      state_put(old);
                    }
                  break;
                }
            }
        }
    }

  if (ret == -ENOENT && g_custom.present &&
      !strcmp(id->valuestring, g_custom.id))
    {
      state_get(old);
      g_custom.enabled = cJSON_IsTrue(enabled);
      g_revision++;
      ret = persist_locked();
      if (ret)
        {
          state_put(old);
        }
    }

  pthread_mutex_unlock(&g_lock);
  if (!ret)
    {
      ret = bk7258_runtime_skills_app_read(out, size);
    }

out:
  cJSON_Delete(object);
  free(old);
  return ret;
}

int
bk7258_runtime_skills_set_owner_key(const uint8_t *der, size_t size)
{
  if (!der || !size || size > sizeof(g_owner_key))
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_lock);
  memcpy(g_owner_key, der, size);
  g_owner_key_size = size;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

static int
install_locked(const uint8_t *manifest, size_t manifest_size,
               const uint8_t *signature, size_t signature_size)
{
  mbedtls_pk_context key;
  uint8_t digest[32];
  cJSON *object;
  cJSON *id;
  cJSON *profile;
  cJSON *name;
  cJSON *triggers;
  cJSON *tools;
  cJSON *permissions;
  cJSON *body;
  unsigned int base = 0;
  unsigned int requested = 0;
  int i;
  int ret = -EKEYREJECTED;
  const char *end;
  static const char *const manifest_fields[] =
  {
    "id", "name", "profile", "triggers", "tools", "permissions", "body"
  };

  if (!manifest || !manifest_size || manifest_size > 2048 ||
      memchr(manifest, 0, manifest_size) != NULL || !signature ||
      !signature_size || signature_size > sizeof(g_signature))
    {
      return -EINVAL;
    }

  if (!g_owner_key_size || g_custom.present)
    {
      return g_custom.present ? -EEXIST : -EKEYREJECTED;
    }

  mbedtls_pk_init(&key);
  if (mbedtls_pk_parse_public_key(&key, g_owner_key, g_owner_key_size) ||
      !mbedtls_pk_can_do(&key, MBEDTLS_PK_ECKEY) ||
      mbedtls_pk_ec(key)->grp.id != MBEDTLS_ECP_DP_SECP256R1 ||
      mbedtls_sha256(manifest, manifest_size, digest, 0) ||
      mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                        signature, signature_size))
    {
      goto out;
    }

  object = cJSON_ParseWithLengthOpts((const char *)manifest, manifest_size,
                                     &end, false);
  while (object && end < (const char *)manifest + manifest_size &&
         isspace((unsigned char)*end))
    {
      end++;
    }

  if (!object || end != (const char *)manifest + manifest_size)
    {
      cJSON_Delete(object);
      ret = -EINVAL;
      goto out;
    }

  id = cJSON_GetObjectItemCaseSensitive(object, "id");
  name = cJSON_GetObjectItemCaseSensitive(object, "name");
  profile = cJSON_GetObjectItemCaseSensitive(object, "profile");
  triggers = cJSON_GetObjectItemCaseSensitive(object, "triggers");
  tools = cJSON_GetObjectItemCaseSensitive(object, "tools");
  permissions = cJSON_GetObjectItemCaseSensitive(object, "permissions");
  body = cJSON_GetObjectItemCaseSensitive(object, "body");
  if (!fields(object, manifest_fields, 7) || !cJSON_IsString(id) ||
      !cJSON_IsString(name) || !cJSON_IsString(profile) ||
      !cJSON_IsArray(triggers) || cJSON_GetArraySize(triggers) != 1 ||
      !cJSON_IsString(cJSON_GetArrayItem(triggers, 0)) ||
      !cJSON_IsArray(tools) || !cJSON_IsArray(permissions) ||
      !cJSON_IsString(body) || !id->valuestring[0] ||
      id->valuestring[0] == '-' ||
      strlen(id->valuestring) >= sizeof(g_custom.id) ||
      !valid_text(name->valuestring, sizeof(g_custom.name) - 1, false) ||
      !valid_text(cJSON_GetArrayItem(triggers, 0)->valuestring,
                  sizeof(g_custom.trigger) - 1, false) ||
      strchr(cJSON_GetArrayItem(triggers, 0)->valuestring, ':') ||
      strstr(cJSON_GetArrayItem(triggers, 0)->valuestring, "：") ||
      !valid_text(body->valuestring, sizeof(g_custom.markdown) - 1, true))
    {
      ret = -EINVAL;
      cJSON_Delete(object);
      goto out;
    }

  for (i = 0; id->valuestring[i]; i++)
    {
      if (!(id->valuestring[i] >= 'a' && id->valuestring[i] <= 'z') &&
          !(id->valuestring[i] >= '0' && id->valuestring[i] <= '9') &&
          id->valuestring[i] != '-')
        {
          ret = -EINVAL;
          cJSON_Delete(object);
          goto out;
        }
    }

  for (i = 0; i < (int)(sizeof(g_skills) / sizeof(g_skills[0])); i++)
    {
      if (!strcmp(id->valuestring, g_skills[i].id) ||
          !strcmp(cJSON_GetArrayItem(triggers, 0)->valuestring,
                  g_skills[i].trigger))
        {
          ret = -EEXIST;
          cJSON_Delete(object);
          goto out;
        }
    }

  if (!strcmp(profile->valuestring, "focus-coach"))
    {
      base = CAP_FOCUS | CAP_STATUS | CAP_EXPRESSION;
    }
  else if (!strcmp(profile->valuestring, "device-check"))
    {
      base = CAP_STATUS | CAP_MOTION | CAP_EXPRESSION;
    }
  else if (!strcmp(profile->valuestring, "object-guide"))
    {
      base = CAP_CAMERA | CAP_STATUS | CAP_EXPRESSION;
    }
  else
    {
      base = 0;
    }

  for (i = 0; base && i < cJSON_GetArraySize(tools); i++)
    {
      cJSON *item = cJSON_GetArrayItem(tools, i);
      unsigned int cap = 0;
      if (!cJSON_IsString(item) || (cap = cap_for(item->valuestring)) == 0 ||
          !(base & cap) || (requested & cap))
        {
          base = 0;
        }

      requested |= cap;
    }

  if (!base || cJSON_GetArraySize(permissions) != cJSON_GetArraySize(tools))
    {
      ret = -EACCES;
      cJSON_Delete(object);
      goto out;
    }

  for (i = 0; i < cJSON_GetArraySize(tools); i++)
    {
      cJSON *tool = cJSON_GetArrayItem(tools, i);
      bool found = false;

      if (!cJSON_IsString(tool))
        {
          ret = -EACCES;
          cJSON_Delete(object);
          goto out;
        }

      for (int j = 0; j < cJSON_GetArraySize(permissions); j++)
        {
          cJSON *permission = cJSON_GetArrayItem(permissions, j);

          if (cJSON_IsString(permission) &&
              !strcmp(tool->valuestring, permission->valuestring))
            {
              found = true;
              break;
            }
        }

      if (!found)
        {
          ret = -EACCES;
          cJSON_Delete(object);
          goto out;
        }
    }

  strcpy(g_custom.id, id->valuestring);
  strcpy(g_custom.name, name->valuestring);
  strcpy(g_custom.trigger, cJSON_GetArrayItem(triggers, 0)->valuestring);
  strcpy(g_custom.markdown, body->valuestring);
  g_custom.caps = requested;
  g_custom.enabled = false;
  g_custom.present = true;
  memcpy(g_manifest, manifest, manifest_size);
  g_manifest_size = manifest_size;
  memcpy(g_signature, signature, signature_size);
  g_signature_size = signature_size;
  ret = 0;
  cJSON_Delete(object);
out:
  mbedtls_pk_free(&key);
  return ret;
}

int
bk7258_runtime_skills_install(const uint8_t *manifest, size_t manifest_size,
                              const uint8_t *signature,
                              size_t signature_size)
{
  int ret;

  pthread_mutex_lock(&g_lock);
  ret = install_locked(manifest, manifest_size, signature, signature_size);
  pthread_mutex_unlock(&g_lock);
  return ret;
}
