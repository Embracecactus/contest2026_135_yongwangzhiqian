/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_tts.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#ifdef __NuttX__
#  include <netutils/cJSON.h>
#else
#  include <cJSON.h>
#endif

/* The provider does not fix SSE audio chunk sizes. Bound JSON working memory
 * separately from the 90-second PCM budget and grow only for received data.
 * Decode base64 into a small reusable block instead of a second full chunk.
 */
#define BKCLOUD_TTS_EVENT_MAX (256u * 1024u)

static void release_buffers(struct bkcloud_tts_s *tts)
{
  if (tts->line)
    {
      mbedtls_platform_zeroize(tts->line, tts->line_capacity + 1);
      free(tts->line);
    }
  if (tts->event)
    {
      mbedtls_platform_zeroize(tts->event, tts->event_capacity + 1);
      free(tts->event);
    }
  tts->line = tts->event = NULL;
  tts->line_capacity = tts->event_capacity = 0;
  tts->line_size = tts->event_size = 0;
}

static int reserve(char **buffer, size_t *capacity, size_t used, size_t need)
{
  if (need > BKCLOUD_TTS_EVENT_MAX) return -E2BIG;
  if (need <= *capacity) return 0;
  size_t next = *capacity ? *capacity : 4096;
  while (next < need) next *= 2;
  char *grown = malloc(next + 1);
  if (!grown) return -ENOMEM;
  if (*buffer)
    {
      memcpy(grown, *buffer, used);
      mbedtls_platform_zeroize(*buffer, *capacity + 1);
      free(*buffer);
    }
  grown[used] = 0;
  *buffer = grown;
  *capacity = next;
  return 0;
}

void bkcloud_tts_clear(struct bkcloud_tts_s *tts)
{
  if (!tts) return;
  release_buffers(tts);
  mbedtls_platform_zeroize(tts, sizeof(*tts));
}
void bkcloud_tts_init(struct bkcloud_tts_s *tts, bkcloud_write_t write, void *context)
{
  /* Initialize fresh storage; active streams must be cleared before reuse. */
  if (tts) memset(tts, 0, sizeof(*tts));
  if (tts) { tts->write = write; tts->context = context; }
}

static int event(struct bkcloud_tts_s *tts)
{
  cJSON *root = NULL, *choices, *choice, *index, *delta, *audio, *data, *reason;
  int ret = -EBADMSG;
  if (tts->done) return -EPROTO;
  if (tts->event_size == 6 && !memcmp(tts->event, "[DONE]", 6))
    {
      if (!tts->stopped || !tts->total) return -EBADMSG;
      tts->done = true;
      return 0;
    }
  if (!bkcloud_json_safe(tts->event, tts->event_size)) return -EBADMSG;
  const char *end;
  root = cJSON_ParseWithLengthOpts(tts->event, tts->event_size, &end, false);
  if (!root) return -EBADMSG;
  while (end < tts->event + tts->event_size &&
         (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end++;
  if (end != tts->event + tts->event_size || !cJSON_IsObject(root) ||
      cJSON_GetObjectItemCaseSensitive(root, "error")) goto out;
  choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
  if (!cJSON_IsArray(choices)) goto out;
  if (cJSON_GetArraySize(choices) == 0) { ret = 0; goto out; }
  if (tts->stopped || cJSON_GetArraySize(choices) != 1) goto out;
  choice = cJSON_GetArrayItem(choices, 0);
  index = cJSON_GetObjectItemCaseSensitive(choice, "index");
  delta = cJSON_GetObjectItemCaseSensitive(choice, "delta");
  reason = cJSON_GetObjectItemCaseSensitive(choice, "finish_reason");
  if (!cJSON_IsNumber(index) || index->valuedouble != 0 ||
      !cJSON_IsObject(delta)) goto out;
  if (reason && !cJSON_IsNull(reason) &&
      (!cJSON_IsString(reason) || strcmp(reason->valuestring, "stop"))) goto out;
  audio = cJSON_GetObjectItemCaseSensitive(delta, "audio");
  if (audio && !cJSON_IsNull(audio))
    {
      data = cJSON_GetObjectItemCaseSensitive(audio, "data");
      if (!cJSON_IsObject(audio) || !cJSON_IsString(data)) goto out;
      const char *encoded = data->valuestring;
      size_t size = strlen(encoded), padding = 0, bytes = 0;
      if (size % 4) goto out;
      for (size_t i = 0; i < size; i++)
        {
          char c = encoded[i];
          if (c == '=') { if (++padding > 2) goto out; }
          else if (padding || !((c >= 'A' && c <= 'Z') ||
                   (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                   c == '+' || c == '/')) goto out;
        }
      size_t decoded = size / 4 * 3 - padding;
      if (decoded % 2 || decoded > 90u * 24000u * 2u - tts->total) goto out;
      for (size_t offset = 0; offset < size; )
        {
          size_t chunk = size - offset;
          if (chunk > 4096) chunk = 4096;
          if (mbedtls_base64_decode(tts->pcm, sizeof(tts->pcm), &bytes,
              (const unsigned char *)encoded + offset, chunk) != 0)
            { ret = -EBADMSG; goto out; }
          ret = tts->write(tts->context, tts->pcm, bytes);
          if (ret != 0) { if (ret > 0) ret = -EIO; goto out; }
          tts->total += bytes;
          offset += chunk;
        }
    }
  if (cJSON_IsString(reason)) tts->stopped = true;
  ret = 0;
out:
  mbedtls_platform_zeroize(tts->pcm, sizeof(tts->pcm));
  cJSON_Delete(root);
  return ret;
}

static int line(struct bkcloud_tts_s *tts)
{
  size_t size = tts->line_size;
  if (size && tts->line[size - 1] == '\r') size--;
  if (!size)
    {
      if (!tts->event_size) return 0;
      tts->event_size--; /* Remove the final SSE data newline. */
      int ret = event(tts);
      mbedtls_platform_zeroize(tts->event, tts->event_size);
      tts->event_size = 0;
      return ret;
    }
  if (size >= 5 && !memcmp(tts->line, "data:", 5))
    {
      size_t start = 5;
      if (size > start && tts->line[start] == ' ') start++;
      int ret = reserve(&tts->event, &tts->event_capacity, tts->event_size,
                         tts->event_size + size - start + 1);
      if (ret) return ret;
      memcpy(tts->event + tts->event_size, tts->line + start, size - start);
      tts->event_size += size - start;
      tts->event[tts->event_size++] = '\n';
    }
  return 0; /* Ignore SSE comments and fields other than data. */
}

int bkcloud_tts_feed(void *context, const void *data, size_t size)
{
  struct bkcloud_tts_s *tts = context;
  const uint8_t *bytes = data;
  if (!tts || !tts->write || (!data && size)) return -EINVAL;
  if (tts->error) return tts->error;
  for (size_t i = 0; i < size; i++)
    {
      if (bytes[i] == '\n')
        {
          tts->error = line(tts);
          if (tts->line) mbedtls_platform_zeroize(tts->line, tts->line_size);
          tts->line_size = 0;
        }
      else if (!bytes[i]) tts->error = -EBADMSG;
      else
        {
          tts->error = reserve(&tts->line, &tts->line_capacity,
                               tts->line_size, tts->line_size + 1);
          if (!tts->error) tts->line[tts->line_size++] = bytes[i];
        }
      if (tts->error) { release_buffers(tts); return tts->error; }
    }
  if (tts->done && !tts->line_size && !tts->event_size) release_buffers(tts);
  return 0;
}
int bkcloud_tts_finish(struct bkcloud_tts_s *tts)
{
  if (!tts) return -EINVAL;
  if (tts->error) return tts->error;
  return tts->done && !tts->line_size && !tts->event_size ? 0 : -EBADMSG;
}
