/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_request.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#ifdef __NuttX__
#  include <netutils/cJSON.h>
#else
#  include <cJSON.h>
#endif
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>

static const char g_prefix[] =
  "{\"model\":\"%s\",\"stream\":false,\"messages\":[{"
  "\"role\":\"user\",\"content\":[{\"type\":\"input_audio\","
  "\"input_audio\":{\"data\":\"data:audio/wav;base64,";
static const char g_suffix[] = "\"}}]}]}";

static int prefix(const char *model, char *output, size_t capacity)
{
  size_t n;
  if (model == NULL) return -EINVAL;
  for (n = 0; n <= 128 && model[n]; n++)
    {
      unsigned char c = model[n];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '/' || c == ':')) return -EINVAL;
    }
  if (n == 0 || n > 128) return -EINVAL;
  int ret = snprintf(output, capacity, g_prefix, model);
  return ret < 0 || (size_t)ret >= capacity ? -ENOSPC : ret;
}

static void put32(uint8_t *p, uint32_t n)
{
  p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24;
}

int bkcloud_asr_size(const char *model, size_t pcm_size, size_t *body_size)
{
  char header[384];
  if (body_size == NULL) return -EINVAL;
  *body_size = 0;
  if (pcm_size == 0 || pcm_size > BKCLOUD_PCM_MAX || pcm_size % 2)
    return -EINVAL;
  int length = prefix(model, header, sizeof(header));
  if (length < 0) return length;
  *body_size = (size_t)length + sizeof(g_suffix) - 1 +
               ((44u + pcm_size + 2u) / 3u) * 4u;
  return 0;
}

void bkcloud_asr_source_clear(struct bkcloud_asr_source_s *source)
{
  if (source != NULL) mbedtls_platform_zeroize(source, sizeof(*source));
}

void bkcloud_image_source_clear(struct bkcloud_image_source_s *source)
{
  if (source != NULL) mbedtls_platform_zeroize(source, sizeof(*source));
}

int bkcloud_image_source_init(struct bkcloud_image_source_s *source,
                              const char *prefix, size_t prefix_size,
                              const char *suffix, size_t suffix_size,
                              const uint8_t *jpeg, size_t jpeg_size)
{
  size_t encoded;
  if (source == NULL) return -EINVAL;
  bkcloud_image_source_clear(source);
  if (prefix == NULL || prefix_size == 0 || suffix == NULL || suffix_size == 0 ||
      jpeg == NULL || jpeg_size < 4 || jpeg_size > BKCLOUD_JPEG_MAX ||
      jpeg[0] != 0xff || jpeg[1] != 0xd8 || jpeg[jpeg_size - 2] != 0xff ||
      jpeg[jpeg_size - 1] != 0xd9) return -EINVAL;
  if (jpeg_size > (SIZE_MAX - 2u) / 4u * 3u) return -EOVERFLOW;
  encoded = ((jpeg_size + 2u) / 3u) * 4u;
  if (prefix_size > SIZE_MAX - suffix_size || prefix_size + suffix_size > SIZE_MAX - encoded)
    return -EOVERFLOW;
  source->prefix = prefix;
  source->prefix_size = prefix_size;
  source->suffix = suffix;
  source->suffix_size = suffix_size;
  source->jpeg = jpeg;
  source->jpeg_size = jpeg_size;
  source->length = prefix_size + encoded + suffix_size;
  return 0;
}

int bkcloud_image_body(void *buffer, size_t *size, const void **data,
                       size_t requested, void *context)
{
  struct bkcloud_image_source_s *source = context;
  uint8_t input[3];
  unsigned char encoded[5];
  size_t produced = 0;
  if (size == NULL || data == NULL) return -EINVAL;
  size_t capacity = *size;
  *size = 0;
  *data = buffer;
  if (source == NULL || source->prefix == NULL || source->suffix == NULL ||
      source->jpeg == NULL || buffer == NULL || source->offset > source->length)
    return -EINVAL;
  size_t count = source->length - source->offset;
  if (count > requested) count = requested;
  if (count > capacity) count = capacity;
  if (count == 0 && requested && source->offset < source->length) return -ENOSPC;
  const size_t image_end = source->length - source->suffix_size;
  while (produced < count)
    {
      size_t pos = source->offset + produced;
      if (pos < source->prefix_size)
        ((char *)buffer)[produced++] = source->prefix[pos];
      else if (pos >= image_end)
        ((char *)buffer)[produced++] = source->suffix[pos - image_end];
      else
        {
          size_t index = (pos - source->prefix_size) / 4u * 3u;
          size_t bytes = source->jpeg_size - index;
          if (bytes > sizeof(input)) bytes = sizeof(input);
          memcpy(input, source->jpeg + index, bytes);
          size_t encoded_size;
          if (mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_size, input, bytes) != 0)
            {
              mbedtls_platform_zeroize(input, sizeof(input));
              mbedtls_platform_zeroize(encoded, sizeof(encoded));
              return -EIO;
            }
          size_t skip = (pos - source->prefix_size) % 4u;
          size_t take = encoded_size - skip;
          if (take > count - produced) take = count - produced;
          memcpy((char *)buffer + produced, encoded + skip, take);
          produced += take;
        }
    }
  mbedtls_platform_zeroize(input, sizeof(input));
  mbedtls_platform_zeroize(encoded, sizeof(encoded));
  source->offset += produced;
  *size = produced;
  return 0;
}

int bkcloud_asr_source_init(struct bkcloud_asr_source_s *source,
                           const char *model, const uint8_t *pcm, size_t size)
{
  int ret;
  if (source == NULL) return -EINVAL;
  bkcloud_asr_source_clear(source);
  if (pcm == NULL) return -EINVAL;
  ret = bkcloud_asr_size(model, size, &source->length);
  if (ret < 0) return ret;
  ret = prefix(model, source->prefix, sizeof(source->prefix));
  if (ret < 0) { bkcloud_asr_source_clear(source); return ret; }
  source->prefix_size = ret;
  source->pcm = pcm;
  source->pcm_size = size;
  uint8_t *wav = source->wav;
  memcpy(wav, "RIFF", 4); put32(wav + 4, size + 36);
  memcpy(wav + 8, "WAVEfmt ", 8); put32(wav + 16, 16);
  wav[20] = 1; wav[22] = 1;
  put32(wav + 24, 16000); put32(wav + 28, 32000);
  wav[32] = 2; wav[34] = 16;
  memcpy(wav + 36, "data", 4); put32(wav + 40, size);
  return 0;
}

int bkcloud_asr_body(void *buffer, size_t *size, const void **data,
                    size_t requested, void *context)
{
  struct bkcloud_asr_source_s *source = context;
  uint8_t input[3];
  unsigned char encoded[5];
  size_t produced = 0;
  if (size == NULL || data == NULL) return -EINVAL;
  size_t capacity = *size;
  *size = 0;
  *data = buffer;
  if (source == NULL || source->pcm == NULL || buffer == NULL ||
      source->offset > source->length) return -EINVAL;
  size_t count = source->length - source->offset;
  if (count > requested) count = requested;
  if (count > capacity) count = capacity;
  if (count == 0 && requested && source->offset < source->length)
    return -ENOSPC;
  const size_t audio_end = source->length - (sizeof(g_suffix) - 1);
  while (produced < count)
    {
      size_t pos = source->offset + produced;
      if (pos < source->prefix_size)
        ((char *)buffer)[produced++] = source->prefix[pos];
      else if (pos >= audio_end)
        ((char *)buffer)[produced++] = g_suffix[pos - audio_end];
      else
        {
          size_t index = (pos - source->prefix_size) / 4 * 3;
          size_t bytes = source->pcm_size + 44 - index;
          if (bytes > 3) bytes = 3;
          for (size_t i = 0; i < bytes; i++)
            input[i] = index + i < 44 ? source->wav[index + i] :
                       source->pcm[index + i - 44];
          size_t encoded_size;
          int ret = mbedtls_base64_encode(encoded, sizeof(encoded),
                                          &encoded_size, input, bytes);
          if (ret != 0)
            {
              mbedtls_platform_zeroize(input, sizeof(input));
              mbedtls_platform_zeroize(encoded, sizeof(encoded));
              return -EIO;
            }
          size_t skip = (pos - source->prefix_size) % 4;
          size_t take = encoded_size - skip;
          if (take > count - produced) take = count - produced;
          memcpy((char *)buffer + produced, encoded + skip, take);
          produced += take;
        }
    }
  mbedtls_platform_zeroize(input, sizeof(input));
  mbedtls_platform_zeroize(encoded, sizeof(encoded));
  source->offset += produced;
  *size = produced;
  return 0;
}

int bkcloud_asr_write(const char *model, const uint8_t *pcm, size_t size,
                      bkcloud_write_t write, void *context)
{
  struct bkcloud_asr_source_s source;
  char buffer[1024];
  int ret;
  if (write == NULL) return -EINVAL;
  ret = bkcloud_asr_source_init(&source, model, pcm, size);
  while (ret == 0 && source.offset < source.length)
    {
      size_t count = sizeof(buffer);
      const void *data;
      ret = bkcloud_asr_body(buffer, &count, &data, count, &source);
      if (ret == 0) ret = write(context, data, count);
    }
  bkcloud_asr_source_clear(&source);
  mbedtls_platform_zeroize(buffer, sizeof(buffer));
  return ret > 0 ? -EIO : ret;
}

static bool space(char c)
{ return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* Bound parser recursion before entering cJSON; forbid embedded NUL strings
 * whose decoded length cannot be represented by cJSON's C-string interface.
 */
static bool shallow_json(const char *json, size_t size)
{
  unsigned int depth = 0;
  bool quoted = false;
  for (size_t i = 0; i < size; i++)
    {
      char c = json[i];
      if (quoted && c == '\\')
        {
          if (++i >= size) return false;
          if (size - i >= 5 && memcmp(json + i, "u0000", 5) == 0)
            return false;
          continue;
        }
      if (c == '"') quoted = !quoted;
      if (!quoted && (c == '{' || c == '[') && ++depth > 16) return false;
      if (!quoted && (c == '}' || c == ']'))
        { if (depth == 0) return false; depth--; }
    }
  return !quoted && depth == 0;
}

bool bkcloud_json_safe(const char *json, size_t size)
{
  return json != NULL && size > 0 && size <= 65536 &&
         !memchr(json, 0, size) && shallow_json(json, size);
}

int bkcloud_text_parse(const char *json, size_t size,
                      char *text, size_t capacity)
{
  const char *end = NULL;
  cJSON *root, *choices, *choice, *index, *reason, *message, *content, *tools;
  size_t length;
  int ret = -EBADMSG;
  if (text == NULL || capacity == 0) return -EINVAL;
  memset(text, 0, capacity);
  if (!bkcloud_json_safe(json, size))
    return -EBADMSG;
  root = cJSON_ParseWithLengthOpts(json, size, &end, false);
  if (root == NULL) return -EBADMSG;
  while (end < json + size && space(*end)) end++;
  if (end != json + size || !cJSON_IsObject(root) ||
      cJSON_GetObjectItemCaseSensitive(root, "error") != NULL) goto out;
  choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
  if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) != 1) goto out;
  choice = cJSON_GetArrayItem(choices, 0);
  index = cJSON_GetObjectItemCaseSensitive(choice, "index");
  reason = cJSON_GetObjectItemCaseSensitive(choice, "finish_reason");
  message = cJSON_GetObjectItemCaseSensitive(choice, "message");
  if (!cJSON_IsNumber(index) || index->valuedouble != 0 ||
      !cJSON_IsString(reason) || strcmp(reason->valuestring, "stop") ||
      !cJSON_IsObject(message)) goto out;
  tools = cJSON_GetObjectItemCaseSensitive(message, "tool_calls");
  if (tools != NULL && !cJSON_IsNull(tools) &&
      !(cJSON_IsArray(tools) && cJSON_GetArraySize(tools) == 0)) goto out;
  content = cJSON_GetObjectItemCaseSensitive(message, "content");
  if (!cJSON_IsString(content)) goto out;
  const char *start = content->valuestring;
  while (space(*start)) start++;
  length = strlen(start);
  while (length > 0 && space(start[length - 1])) length--;
  if (length == 0 || length > BKCLOUD_TEXT_MAX) goto out;
  if (length >= capacity) { ret = -ENOSPC; goto out; }
  memcpy(text, start, length);
  ret = 0;
out:
  cJSON_Delete(root);
  return ret;
}
