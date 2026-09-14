/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_config.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>

void bkcloud_config_clear(struct bkcloud_config_s *config)
{
  if (config != NULL)
    {
      volatile uint8_t *p = (volatile uint8_t *)config;
      for (size_t i = 0; i < sizeof(*config); i++) p[i] = 0;
    }
}

static bool alnum(uint8_t c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}

static bool model_valid(const uint8_t *p, size_t size)
{
  if (size == 0 || size > BKCLOUD_NAME_MAX) return false;
  for (size_t i = 0; i < size; i++)
    if (!alnum(p[i]) && p[i] != '.' && p[i] != '_' && p[i] != ':' &&
        p[i] != '/' && p[i] != '-') return false;
  return true;
}

int bkcloud_models_decode(struct bkcloud_models_s *models,
                          const void *record, size_t size)
{
  const uint8_t *p = record;
  size_t lengths[3], total = 12, offset = 12;
  char *fields[] = {models ? models->asr_model : NULL,
                    models ? models->chat_model : NULL,
                    models ? models->tts_model : NULL};
  if (!models) return -EINVAL;
  memset(models, 0, sizeof(*models));
  if (!p || size < 12 || size > BKCLOUD_MODELS_RECORD_MAX ||
      memcmp(p, "MCP1", 4) || p[10] || p[11]) return -EBADMSG;
  for (size_t i = 0; i < 3; i++)
    {
      lengths[i] = ((size_t)p[4 + 2 * i] << 8) | p[5 + 2 * i];
      if (lengths[i] > size - total) return -EBADMSG;
      if (!model_valid(p + offset, lengths[i])) return -EBADMSG;
      total += lengths[i]; offset += lengths[i];
    }
  if (total != size) return -EBADMSG;
  offset = 12;
  for (size_t i = 0; i < 3; i++)
    { memcpy(fields[i], p + offset, lengths[i]); offset += lengths[i]; }
  return 0;
}

int bkcloud_models_encode(const struct bkcloud_models_s *models,
                          uint8_t *record, size_t capacity, size_t *size)
{
  const char *fields[3]; size_t lengths[3], total = 12, offset = 12;
  if (!models || !record || !size) return -EINVAL;
  fields[0] = models->asr_model; fields[1] = models->chat_model;
  fields[2] = models->tts_model;
  for (size_t i = 0; i < 3; i++)
    {
      lengths[i] = strnlen(fields[i], BKCLOUD_NAME_MAX + 1u);
      if (!model_valid((const uint8_t *)fields[i], lengths[i])) return -EINVAL;
      total += lengths[i];
    }
  if (total > capacity) return -ENOSPC;
  memset(record, 0, total); memcpy(record, "MCP1", 4);
  for (size_t i = 0; i < 3; i++)
    { record[4 + 2 * i] = lengths[i] >> 8; record[5 + 2 * i] = lengths[i]; }
  for (size_t i = 0; i < 3; i++)
    { memcpy(record + offset, fields[i], lengths[i]); offset += lengths[i]; }
  *size = total; return 0;
}

static bool host_valid(const uint8_t *p, size_t size)
{
  size_t label = 0;
  if (!alnum(p[0]) || !alnum(p[size - 1])) return false;
  for (size_t i = 0; i < size; i++)
    {
      if (p[i] == '.')
        {
          if (label == 0 || !alnum(p[i - 1]) ||
              i + 1 == size || !alnum(p[i + 1])) return false;
          label = 0;
        }
      else if ((!alnum(p[i]) && p[i] != '-') || ++label > 63)
        return false;
    }
  return true;
}

int bkcloud_config_decode(struct bkcloud_config_s *config,
                          const void *record, size_t size)
{
  const uint8_t *p = record;
  size_t lengths[6], total = 24, offset;
  if (config == NULL) return -EINVAL;
  bkcloud_config_clear(config);
  if (p == NULL || size < 24 || size > BKCLOUD_CONFIG_MAX ||
      memcmp(p, "CCF1", 4) || (p[4] != 1 && p[4] != 2) || p[5] ||
      (p[6] == 0 && p[7] == 0)) return -EBADMSG;
  for (size_t i = 20; i < 24; i++) if (p[i]) return -EBADMSG;
  for (size_t i = 0; i < 6; i++)
    {
      lengths[i] = ((size_t)p[8 + 2 * i] << 8) | p[9 + 2 * i];
      if (lengths[i] == 0 || lengths[i] >
          (i == 2 ? BKCLOUD_KEY_MAX : BKCLOUD_NAME_MAX)) return -EBADMSG;
      total += lengths[i];
    }
  if (total != size || !host_valid(p + 24, lengths[0])) return -EBADMSG;
  offset = 24 + lengths[0];
  if (p[offset] != '/') return -EBADMSG;
  for (size_t i = 1; i < 6; i++)
    {
      for (size_t j = 0; j < lengths[i]; j++)
        {
          uint8_t c = p[offset + j];
          if (i == 2)
            { if (c < 33 || c > 126) return -EBADMSG; }
          else if (!alnum(c) && c != '-' && c != '_' && c != '/' &&
                   c != '.' && (i == 1 || c != ':')) return -EBADMSG;
          if (i == 1 && j > 0 && c == '.' && p[offset + j - 1] == '.')
            return -EBADMSG;
        }
      offset += lengths[i];
    }
  char *fields[] = {config->host, config->base_path, config->api_key,
                    config->asr_model, config->chat_model, config->tts_model};
  offset = 24;
  for (size_t i = 0; i < 6; i++)
    { memcpy(fields[i], p + offset, lengths[i]); offset += lengths[i]; }
  config->dialect = p[4];
  config->port = ((uint16_t)p[6] << 8) | p[7];
  return 0;
}
