/****************************************************************************
 * apps/bk7258/bk7258_memory_facts.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_memory_facts.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int valid_key(const char *key)
{
  return key != NULL &&
         (!strcmp(key, "name") || !strcmp(key, "preference") ||
          !strcmp(key, "topic"));
}

static int valid_text(const char *text, size_t limit)
{
  size_t size;

  if (text == NULL || (size = strnlen(text, limit + 1)) == 0 ||
      size > limit)
    {
      return 0;
    }

  for (size_t i = 0; i < size; )
    {
      unsigned char lead = text[i++];
      unsigned char second;
      size_t following;

      if (lead < 0x20)
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
      if ((second & 0xc0) != 0x80 ||
          (lead == 0xe0 && second < 0xa0) ||
          (lead == 0xed && second > 0x9f) ||
          (lead == 0xf0 && second < 0x90) ||
          (lead == 0xf4 && second > 0x8f))
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

static int valid_request(const cJSON *root, int setting)
{
  const cJSON *item;
  unsigned int op = 0;
  unsigned int key = 0;
  unsigned int value = 0;
  unsigned int revision = 0;

  if (!cJSON_IsObject(root))
    {
      return 0;
    }

  cJSON_ArrayForEach(item, root)
    {
      if (item->string == NULL)
        {
          return 0;
        }

      if (!strcmp(item->string, "op"))
        {
          op++;
        }
      else if (!strcmp(item->string, "key"))
        {
          key++;
        }
      else if (!strcmp(item->string, "value"))
        {
          value++;
        }
      else if (!strcmp(item->string, "expected_revision"))
        {
          revision++;
        }
      else
        {
          return 0;
        }
    }

  return op == 1 && key == 1 && revision == 1 &&
         value == (setting ? 1 : 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkmemory_facts_apply(struct bkmemory_facts_s *facts, const char *json,
                        uint64_t revision, uint64_t *expected)
{
  cJSON *root;
  cJSON *op;
  cJSON *key;
  cJSON *value;
  cJSON *want;
  char *end;
  unsigned long long parsed;

  if (facts == NULL || json == NULL || expected == NULL)
    {
      return -EINVAL;
    }

  root = cJSON_ParseWithOpts(json, NULL, true);
  op = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "op");
  key = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "key");
  value = root == NULL ? NULL :
          cJSON_GetObjectItemCaseSensitive(root, "value");
  want = root == NULL ? NULL :
         cJSON_GetObjectItemCaseSensitive(root, "expected_revision");
  if (!cJSON_IsString(op) || !cJSON_IsString(key) || !cJSON_IsString(want) ||
      !valid_key(key->valuestring))
    {
      goto bad;
    }

  if (strcmp(op->valuestring, "set") && strcmp(op->valuestring, "delete"))
    {
      goto bad;
    }

  if (!valid_request(root, !strcmp(op->valuestring, "set")))
    {
      goto bad;
    }

  for (const char *p = want->valuestring; *p; p++)
    {
      if (*p < '0' || *p > '9')
        {
          goto bad;
        }
    }

  errno = 0;
  parsed = strtoull(want->valuestring, &end, 10);
  if (errno || *want->valuestring == 0 || *end || parsed != revision)
    {
      goto stale;
    }

  *expected = parsed;
  for (size_t i = 0; i < BKMEMORY_FACT_SLOTS; i++)
    {
      if (!strcmp(facts->slot[i].key, key->valuestring))
        {
          if (!strcmp(op->valuestring, "delete"))
            {
              memset(&facts->slot[i], 0, sizeof(facts->slot[i]));
            }
          else if (cJSON_IsString(value) &&
                   valid_text(value->valuestring, BKMEMORY_FACT_VALUE))
            {
              snprintf(facts->slot[i].value, sizeof(facts->slot[i].value),
                       "%s", value->valuestring);
            }
          else
            {
              goto bad;
            }

          cJSON_Delete(root);
          return 0;
        }
    }

  if (strcmp(op->valuestring, "set") || !cJSON_IsString(value) ||
      !valid_text(value->valuestring, BKMEMORY_FACT_VALUE))
    {
      goto bad;
    }

  for (size_t i = 0; i < BKMEMORY_FACT_SLOTS; i++)
    {
      if (!facts->slot[i].key[0])
        {
          snprintf(facts->slot[i].key, sizeof(facts->slot[i].key), "%s",
                   key->valuestring);
          snprintf(facts->slot[i].value, sizeof(facts->slot[i].value), "%s",
                   value->valuestring);
          cJSON_Delete(root);
          return 0;
        }
    }

  cJSON_Delete(root);
  return -ENOSPC;

stale:
  cJSON_Delete(root);
  return -ESTALE;

bad:
  cJSON_Delete(root);
  return -EINVAL;
}

int bkmemory_facts_extract(struct bkmemory_facts_s *facts, const char *text)
{
  static const char *const prefix[] =
  {
    "记住我叫", "我喜欢", "我最近在聊"
  };

  static const char *const key[] =
  {
    "name", "preference", "topic"
  };

  if (facts == NULL || text == NULL)
    {
      return 0;
    }

  for (size_t i = 0; i < 3; i++)
    {
      size_t size = strlen(prefix[i]);

      if (!strncmp(text, prefix[i], size) &&
          valid_text(text + size, BKMEMORY_FACT_VALUE))
        {
          for (size_t slot = 0; slot < BKMEMORY_FACT_SLOTS; slot++)
            {
              if (!strcmp(facts->slot[slot].key, key[i]))
                {
                  snprintf(facts->slot[slot].key,
                           sizeof(facts->slot[slot].key), "%s", key[i]);
                  snprintf(facts->slot[slot].value,
                           sizeof(facts->slot[slot].value), "%s",
                           text + size);
                  return 1;
                }
            }

          for (size_t slot = 0; slot < BKMEMORY_FACT_SLOTS; slot++)
            {
              if (!facts->slot[slot].key[0])
                {
                  snprintf(facts->slot[slot].key,
                           sizeof(facts->slot[slot].key), "%s", key[i]);
                  snprintf(facts->slot[slot].value,
                           sizeof(facts->slot[slot].value), "%s",
                           text + size);
                  return 1;
                }
            }
        }
    }

  return 0;
}

int bkmemory_facts_json(const struct bkmemory_facts_s *facts,
                       uint64_t revision, int enabled, char *json,
                       size_t capacity)
{
  cJSON *root;
  cJSON *array;
  char number[24];
  char *text;

  if (facts == NULL || json == NULL || capacity == 0)
    {
      return -EINVAL;
    }

  root = cJSON_CreateObject();
  array = cJSON_CreateArray();
  snprintf(number, sizeof(number), "%llu", (unsigned long long)revision);
  if (root == NULL || array == NULL ||
      !cJSON_AddNumberToObject(root, "version", 2) ||
      !cJSON_AddStringToObject(root, "revision", number) ||
      !cJSON_AddBoolToObject(root, "enabled", enabled) ||
      !cJSON_AddItemToObject(root, "facts", array))
    {
      /* The root owns the array only after the final add succeeds. */

      cJSON_Delete(array);
      goto fail;
    }

  if (enabled)
    {
      for (size_t i = 0; i < BKMEMORY_FACT_SLOTS; i++)
        {
          if (facts->slot[i].key[0])
            {
              cJSON *item = cJSON_CreateObject();

              if (item == NULL ||
                  !cJSON_AddStringToObject(item, "key",
                                          facts->slot[i].key) ||
                  !cJSON_AddStringToObject(item, "value",
                                          facts->slot[i].value))
                {
                  cJSON_Delete(item);
                  goto fail;
                }

              cJSON_AddItemToArray(array, item);
            }
        }
    }

  text = cJSON_PrintUnformatted(root);
  if (text == NULL || strlen(text) + 1 > capacity)
    {
      free(text);
      goto fail;
    }

  strcpy(json, text);
  free(text);
  cJSON_Delete(root);
  return 0;

fail:
  cJSON_Delete(root);
  return -ENOSPC;
}

int bkmemory_facts_encode(const struct bkmemory_facts_s *facts,
                         uint8_t *data, size_t capacity, size_t *used)
{
  size_t pos = 1;
  unsigned int count = 0;

  if (!facts || !data || !used || capacity < 1)
    {
      return -EINVAL;
    }

  for (size_t i = 0; i < BKMEMORY_FACT_SLOTS; i++)
    {
      size_t key;
      size_t value;

      if (!facts->slot[i].key[0])
        {
          continue;
        }

      key = strlen(facts->slot[i].key);
      value = strlen(facts->slot[i].value);
      if (key > BKMEMORY_FACT_KEY || value > BKMEMORY_FACT_VALUE ||
          pos + 2 + key + value > capacity)
        {
          return -E2BIG;
        }

      data[pos++] = key;
      data[pos++] = value;
      memcpy(data + pos, facts->slot[i].key, key);
      pos += key;
      memcpy(data + pos, facts->slot[i].value, value);
      pos += value;
      count++;
    }

  data[0] = count;
  *used = pos;
  return 0;
}

int bkmemory_facts_decode(struct bkmemory_facts_s *facts,
                         const uint8_t *data, size_t size, size_t *used)
{
  size_t pos = 1;

  if (!facts || !data || !used || size < 1 ||
      data[0] > BKMEMORY_FACT_SLOTS)
    {
      return -EBADMSG;
    }

  memset(facts, 0, sizeof(*facts));
  for (unsigned int i = 0; i < data[0]; i++)
    {
      size_t key;
      size_t value;

      if (size - pos < 2)
        {
          return -EBADMSG;
        }

      key = data[pos++];
      value = data[pos++];
      if (!key || key > BKMEMORY_FACT_KEY || !value ||
          value > BKMEMORY_FACT_VALUE ||
          key + value > size - pos)
        {
          return -EBADMSG;
        }

      memcpy(facts->slot[i].key, data + pos, key);
      pos += key;
      memcpy(facts->slot[i].value, data + pos, value);
      pos += value;
      if (!valid_key(facts->slot[i].key) ||
          !valid_text(facts->slot[i].value, value))
        {
          return -EBADMSG;
        }
    }

  *used = pos;
  return 0;
}
