/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_audio.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <mbedtls/platform_util.h>
#ifdef __NuttX__
#  include <netutils/cJSON.h>
#else
#  include <cJSON.h>
#endif

#define BKCLOUD_REQUEST_CAPACITY 65536u

bool bkcloud_audio_valid_text(const char *text)
{
  size_t size;
  if (text == NULL) return false;
  size = strnlen(text, BKCLOUD_TEXT_MAX + 1);
  return size > 0 && size <= BKCLOUD_TEXT_MAX;
}

static int bkcloud_request_serialize(cJSON *root, char **body,
                                     size_t *body_size, size_t *body_capacity)
{
  size_t capacity = 256;
  *body = NULL;
  *body_size = 0;
  *body_capacity = 0;
  for (;;)
    {
      char *request = cJSON_malloc(capacity);
      if (request == NULL) return -ENOMEM;
      if (cJSON_PrintPreallocated(root, request, capacity, false))
        {
          *body = request;
          *body_size = strlen(request);
          *body_capacity = capacity;
          return 0;
        }
      mbedtls_platform_zeroize(request, capacity);
      cJSON_free(request);
      if (capacity == BKCLOUD_REQUEST_CAPACITY) return -E2BIG;
      capacity *= 2;
      if (capacity > BKCLOUD_REQUEST_CAPACITY)
        capacity = BKCLOUD_REQUEST_CAPACITY;
    }
}

static void bkcloud_request_clear(char **body, size_t *body_size,
                                  size_t *body_capacity)
{
  if (*body != NULL)
    {
      mbedtls_platform_zeroize(*body, *body_capacity);
      cJSON_free(*body);
    }
  *body = NULL;
  *body_size = 0;
  *body_capacity = 0;
}

int bkcloud_recognize(struct bkcloud_client_s *client,
                      const struct bkcloud_config_s *config,
                      const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                      uint64_t deadline_ms, const uint8_t *pcm, size_t pcm_size,
                      char *text, size_t capacity)
{
  int ret;
  if (text == NULL || capacity == 0) return -EINVAL;
  memset(text, 0, capacity);
  if (client == NULL || config == NULL) return -EINVAL;
  memset(client, 0, sizeof(*client));
  if (config->dialect != 1 && config->dialect != 2) return -ENOTSUP;
  ret = bkcloud_asr_source_init(&client->source, config->asr_model, pcm,
                                pcm_size);
  if (ret == 0)
    ret = bkcloud_http_post(&client->http, config, "chat/completions", tls,
                            tls_context, deadline_ms, bkcloud_asr_body,
                            &client->source, client->source.length,
                            client->response, sizeof(client->response));
  if (ret == 0)
    ret = bkcloud_text_parse(client->response, client->http.received,
                             text, capacity);
  bkcloud_asr_source_clear(&client->source);
  mbedtls_platform_zeroize(client->response, sizeof(client->response));
  return ret;
}

static bool add_message(cJSON *messages, const char *role, const char *content)
{
  cJSON *message = cJSON_CreateObject();
  if (message == NULL) return false;
  if (!cJSON_AddStringToObject(message, "role", role) ||
      !cJSON_AddStringToObject(message, "content", content) ||
      !cJSON_AddItemToArray(messages, message))
    { cJSON_Delete(message); return false; }
  return true;
}

static int synthesize_pcm(struct bkcloud_client_s *client,
                          const struct bkcloud_config_s *config,
                          const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                          uint64_t deadline_ms, const char *text,
                          bkcloud_write_t pcm, void *context)
{
  int ret = -ENOMEM;
  char *body_data = NULL;
  size_t body_size = 0;
  size_t body_capacity = 0;
  cJSON *root;
  memset(client, 0, sizeof(*client));
  root = cJSON_CreateObject();
  if (!root) return ret;
  if (!cJSON_AddStringToObject(root, "model", config->tts_model) ||
      !cJSON_AddStringToObject(root, "input", text) ||
      !cJSON_AddStringToObject(root, "voice", config->tts_voice[0] ?
                               config->tts_voice : "alloy") ||
      !cJSON_AddStringToObject(root, "response_format", "pcm")) goto done;
  ret = bkcloud_request_serialize(root, &body_data, &body_size, &body_capacity);
  if (ret != 0) goto done;
  ret = bkcloud_http_pcm(&client->http, config, tls, tls_context, deadline_ms,
                          body_data, body_size, pcm, context, 8u * 1024u * 1024u);
done:
  cJSON_Delete(root);
  bkcloud_request_clear(&body_data, &body_size, &body_capacity);
  return ret;
}

static int tts_events(void *context, const void *data, size_t size)
{
  struct bkcloud_tts_s *decoder = context;
  int ret = bkcloud_tts_feed(decoder, data, size);
  if (ret != 0 || !decoder->done) return ret;
  ret = bkcloud_tts_finish(decoder);
  return ret == 0 ? BKCLOUD_HTTP_STREAM_COMPLETE : ret;
}

int bkcloud_synthesize(struct bkcloud_client_s *client,
                       struct bkcloud_tts_s *decoder,
                       const struct bkcloud_config_s *config,
                       const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                       uint64_t deadline_ms, const char *text,
                       bkcloud_write_t pcm, void *context)
{
  cJSON *root = NULL;
  cJSON *messages;
  cJSON *audio;
  char *body_data = NULL;
  size_t body_size = 0;
  size_t body_capacity = 0;
  int ret = -ENOMEM;
  if (!client || !decoder || !config || !pcm || !bkcloud_audio_valid_text(text))
    return -EINVAL;
  if (config->dialect == 1)
    return synthesize_pcm(client, config, tls, tls_context, deadline_ms, text,
                          pcm, context);
  if (config->dialect != 2) return -ENOTSUP;
  memset(client, 0, sizeof(*client));
  bkcloud_tts_init(decoder, pcm, context);
  root = cJSON_CreateObject();
  if (!root) goto out;
  messages = cJSON_AddArrayToObject(root, "messages");
  audio = cJSON_AddObjectToObject(root, "audio");
  if (!messages || !audio ||
      !cJSON_AddStringToObject(root, "model", config->tts_model) ||
      !cJSON_AddBoolToObject(root, "stream", true) ||
      !cJSON_AddStringToObject(audio, "format", "pcm16") ||
      !cJSON_AddStringToObject(audio, "voice", config->tts_voice[0] ?
                               config->tts_voice : "mimo_default") ||
      !add_message(messages, "assistant", text)) goto out;
  ret = bkcloud_request_serialize(root, &body_data, &body_size, &body_capacity);
  if (ret != 0) goto out;
  cJSON_Delete(root);
  root = NULL;
  ret = bkcloud_http_events(&client->http, config, tls, tls_context, deadline_ms,
                            body_data, body_size, tts_events, decoder,
                            8u * 1024u * 1024u);
  if (ret == 0) ret = bkcloud_tts_finish(decoder);
out:
  syslog(LOG_INFO, "BKVOICE TTS stream ret=%d pcm_bytes=%lu stopped=%d "
         "done=%d parser=%d events=%lu first_event_pcm=%lu max_event_pcm=%lu\n",
         ret, (unsigned long)decoder->total, decoder->stopped, decoder->done,
         decoder->error, (unsigned long)decoder->audio_events,
         (unsigned long)decoder->first_audio_bytes,
         (unsigned long)decoder->max_audio_bytes);
  cJSON_Delete(root);
  bkcloud_tts_clear(decoder);
  bkcloud_request_clear(&body_data, &body_size, &body_capacity);
  return ret;
}
