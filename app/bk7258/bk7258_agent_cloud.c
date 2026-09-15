/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_agent_cloud.h"
#include "bk7258_cloud_audio.h"
#include "bk7258_voice_config.h"
#include "bk7258_voice_tls.h"
#include "voice/voice_asr.h"
#include "voice/voice_tts.h"
#include "agent_config.h"
#include "infra/config_store.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <mbedtls/platform_util.h>

/* Immutable selected settings, shared only as read-only trust/config input.
 * Reference counts include the configuration owner and each active backend.
 * TLS state itself is never shared between ASR and TTS. */
struct cloud_settings_s
{
  struct bkvoice_config_s trust;
  struct bkcloud_config_s service;
  unsigned int references;
};

struct cloud_backend_s
{
  struct cloud_settings_s *settings;
  struct bkcloud_config_s service;
  struct bkvoice_tls_s tls;
  atomic_bool canceled;
};

static pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;
static struct cloud_settings_s *g_selected;
static struct cloud_backend_s g_asr;
static struct cloud_backend_s g_tts;

static void settings_release(struct cloud_settings_s *settings)
{
  if (!settings) return;
  pthread_mutex_lock(&g_config_lock);
  bool last = --settings->references == 0;
  pthread_mutex_unlock(&g_config_lock);
  if (last)
    {
      bkvoice_config_clear(&settings->trust);
      bkcloud_config_clear(&settings->service);
      free(settings);
    }
}

static void backend_release(struct cloud_backend_s *backend)
{
  if (!backend->settings) return;
  /* The official registry prevents deinit while a request/cancel uses TLS. */
  (void)bkvoice_tls_ops()->close(&backend->tls);
  (void)bkvoice_tls_uninitialize(&backend->tls);
  settings_release(backend->settings);
  backend->settings = NULL;
  bkcloud_config_clear(&backend->service);
}

static int tts_selection(struct bkcloud_config_s *service)
{
  char value[128] = { 0 };
  if (claw_config_get(AGENT_CFG_KEY_TTS_LOCATION, value, sizeof(value)) == 0 &&
      value[0] && strcmp(value, "remote")) return -EINVAL;
  if (claw_config_get(AGENT_CFG_KEY_TTS_BACKEND, value, sizeof(value)) == 0 &&
      value[0] && strcmp(value, service->dialect == 2 ? "mimo" : "openai-audio"))
    return -EINVAL;
  if (claw_config_get(AGENT_CFG_KEY_TTS_MODEL, value, sizeof(value)) == 0 && value[0])
    memcpy(service->tts_model, value, sizeof(value));
  if (claw_config_get(AGENT_CFG_KEY_TTS_VOICE, value, sizeof(value)) == 0 && value[0])
    {
      /* Only the existing authorized default voices have been evaluated.
       * Never send an unrecognized private/local voice identifier remotely. */
      if (strcmp(value, service->dialect == 2 ? "mimo_default" : "alloy"))
        return -ENOTSUP;
      memcpy(service->tts_voice, value, sizeof(value));
    }
  return 0;
}

static int backend_prepare(struct cloud_backend_s *backend, uint8_t dialect)
{
  pthread_mutex_lock(&g_config_lock);
  struct cloud_settings_s *settings = g_selected;
  if (settings && settings->service.dialect == dialect)
    settings->references++;
  else
    settings = NULL;
  pthread_mutex_unlock(&g_config_lock);
  if (!settings) return -ENOKEY;
  backend->service = settings->service;
  int ret = backend == &g_tts ? tts_selection(&backend->service) : 0;
  if (ret != 0)
    {
      bkcloud_config_clear(&backend->service);
      settings_release(settings);
      return ret;
    }
  struct bkvoice_tls_config_s tls =
    {
      .peer_address = settings->trust.peer_address,
      .server_ca = &settings->trust.ca,
      .server_auth_only = true,
      .trusted_time = bkvoice_config_trusted_time,
      .now_ms = bkvoice_config_now_ms,
      .clock_context = &settings->trust,
    };
  ret = bkvoice_tls_initialize(&backend->tls, &tls);
  if (ret != 0)
    {
      bkcloud_config_clear(&backend->service);
      settings_release(settings);
    }
  else backend->settings = settings;
  return ret;
}

static int request_prepare(struct cloud_backend_s *backend)
{
  if (!backend->settings) return -ENOKEY;
  atomic_store(&backend->canceled, false);
  return bkvoice_config_trusted_time(&backend->settings->trust);
}

static int request_cancel(struct cloud_backend_s *backend)
{
  atomic_store(&backend->canceled, true);
  return bkvoice_tls_ops()->interrupt(&backend->tls);
}

/* Preserve cancellation even across the transport's new-connection reset.
 * Connection and all I/O remain bounded by the original request deadline. */
static int cloud_open(void *context, const char *host, uint16_t port,
                      uint64_t deadline)
{
  struct cloud_backend_s *backend = context;
  if (atomic_load(&backend->canceled)) return -ECANCELED;
  int ret = bkvoice_tls_ops()->open_verified(&backend->tls, host, port, deadline);
  if (atomic_load(&backend->canceled))
    {
      (void)bkvoice_tls_ops()->close(&backend->tls);
      return -ECANCELED;
    }
  return ret;
}

static ssize_t cloud_send(void *context, const uint8_t *data, size_t size,
                          uint64_t deadline)
{
  struct cloud_backend_s *backend = context;
  if (atomic_load(&backend->canceled)) return -ECANCELED;
  return bkvoice_tls_ops()->send(&backend->tls, data, size, deadline);
}

static ssize_t cloud_recv(void *context, uint8_t *data, size_t size,
                          uint64_t deadline)
{
  struct cloud_backend_s *backend = context;
  if (atomic_load(&backend->canceled)) return -ECANCELED;
  return bkvoice_tls_ops()->recv(&backend->tls, data, size, deadline);
}

static int cloud_close(void *context)
{
  return bkvoice_tls_ops()->close(&((struct cloud_backend_s *)context)->tls);
}

static const struct bkvoice_wss_tls_ops_s g_transport =
{
  .open_verified = cloud_open,
  .send = cloud_send,
  .recv = cloud_recv,
  .close = cloud_close,
};

static int asr_mimo_prepare(void) { return backend_prepare(&g_asr, 2); }
static int asr_audio_prepare(void) { return backend_prepare(&g_asr, 1); }
static int tts_mimo_prepare(void) { return backend_prepare(&g_tts, 2); }
static int tts_audio_prepare(void) { return backend_prepare(&g_tts, 1); }
static int asr_request_prepare(void) { return request_prepare(&g_asr); }
static int tts_request_prepare(void) { return request_prepare(&g_tts); }
static int asr_cancel(void) { return request_cancel(&g_asr); }
static int tts_cancel(void) { return request_cancel(&g_tts); }
static void asr_release(void) { backend_release(&g_asr); }
static void tts_release(void) { backend_release(&g_tts); }

static int recognize(const unsigned char *pcm, size_t size,
                     char *text, size_t capacity)
{
  struct bkcloud_client_s *client = calloc(1, sizeof(*client));
  if (!client) return -ENOMEM;
  int ret = bkcloud_recognize(client, &g_asr.service,
    &g_transport, &g_asr, bkvoice_config_now_ms(NULL) + 60000u,
    pcm, size, text, capacity);
  if (atomic_load(&g_asr.canceled)) ret = -ECANCELED;
  if (ret != 0 && capacity) text[0] = '\0';
  mbedtls_platform_zeroize(client, sizeof(*client));
  free(client);
  syslog(LOG_INFO, "AGENT ASR backend=%s mode=batch ret=%d\n",
    g_asr.settings->service.dialect == 2 ? "mimo" : "openai-audio", ret);
  return ret;
}

struct pcm_output_s
{
  voice_tts_chunk_cb callback;
  void *context;
  unsigned char *buffer;
  size_t capacity;
  size_t used;
  unsigned char tail;
  bool has_tail;
};

static int pcm_frames(struct pcm_output_s *output, const void *data, size_t size)
{
  if (atomic_load(&g_tts.canceled)) return -ECANCELED;
  if (!size) return 0;
  if (output->callback)
    output->callback(data, size, 0, output->context);
  else
    {
      if (size > output->capacity - output->used) return -ENOSPC;
      memcpy(output->buffer + output->used, data, size);
    }
  output->used += size;
  return atomic_load(&g_tts.canceled) ? -ECANCELED : 0;
}

static int pcm_output(void *context, const void *data, size_t size)
{
  struct pcm_output_s *output = context;
  const unsigned char *bytes = data;
  int ret;
  if (!bytes && size) return -EPROTO;
  /* HTTP chunks are bytes, not guaranteed sample boundaries. */
  if (output->has_tail && size)
    {
      unsigned char sample[2] = { output->tail, *bytes++ };
      output->has_tail = false;
      size--;
      ret = pcm_frames(output, sample, sizeof(sample));
      if (ret != 0) return ret;
    }
  size_t aligned = size & ~(size_t)1;
  ret = pcm_frames(output, bytes, aligned);
  if (ret == 0 && size != aligned)
    {
      output->tail = bytes[aligned];
      output->has_tail = true;
    }
  return ret;
}

static int synthesize(struct pcm_output_s *output, const char *text)
{
  struct bkcloud_client_s *client = calloc(1, sizeof(*client));
  struct bkcloud_tts_s *decoder = calloc(1, sizeof(*decoder));
  int ret = -ENOMEM;
  if (client && decoder)
    ret = bkcloud_synthesize(client, decoder, &g_tts.service,
      &g_transport, &g_tts, bkvoice_config_now_ms(NULL) + 120000u,
      text, pcm_output, output);
  if (atomic_load(&g_tts.canceled)) ret = -ECANCELED;
  if (ret == 0 && (output->used == 0 || output->has_tail)) ret = -EPROTO;
  if (ret == 0 && output->callback)
    output->callback(NULL, 0, 1, output->context);
  if (decoder) bkcloud_tts_clear(decoder);
  if (client) mbedtls_platform_zeroize(client, sizeof(*client));
  free(decoder);
  free(client);
  syslog(LOG_INFO, "AGENT TTS backend=%s mode=%s rate=24000 ret=%d bytes=%zu\n",
    g_tts.settings->service.dialect == 2 ? "mimo" : "openai-audio",
    output->callback ? "audio-stream/full-text" : "batch", ret, output->used);
  return ret;
}

static int synthesize_stream(const char *text, voice_tts_chunk_cb callback,
                             void *context)
{
  struct pcm_output_s output = { .callback = callback, .context = context };
  return synthesize(&output, text);
}

static int synthesize_batch(const char *text, unsigned char *pcm,
                            size_t capacity, size_t *used)
{
  struct pcm_output_s output = { .buffer = pcm, .capacity = capacity };
  int ret = synthesize(&output, text);
  *used = ret == 0 ? output.used : 0;
  if (ret != 0) mbedtls_platform_zeroize(pcm, output.used);
  return ret;
}

static unsigned int output_rate(void) { return 24000; }
static int capabilities(voice_tts_capabilities_t *caps)
{
  *caps = (voice_tts_capabilities_t)
    {
      .location = VOICE_TTS_LOCATION_REMOTE,
      .needs_network = true,
      .streaming_output = true,
      .can_cancel = true,
      .sample_rate = 24000,
      .batch_sample_rate = 24000,
      .channels = 1,
      .bits = 16,
    };
  return 0;
}

#define ASR_OPS(label, prepare) \
  { .name = label, .init = prepare, .prepare_request = asr_request_prepare, \
    .recognize = recognize, .cancel = asr_cancel, .deinit = asr_release }
#define TTS_OPS(label, prepare) \
  { .name = label, .init = prepare, .prepare_request = tts_request_prepare, \
    .synthesize = synthesize_batch, .synthesize_stream = synthesize_stream, \
    .stream_sample_rate = output_rate, .get_capabilities = capabilities, \
    .cancel = tts_cancel, .deinit = tts_release }

static const voice_asr_ops_t g_asr_mimo = ASR_OPS("mimo", asr_mimo_prepare);
static const voice_asr_ops_t g_asr_audio = ASR_OPS("openai-audio", asr_audio_prepare);
static const voice_tts_ops_t g_tts_mimo = TTS_OPS("mimo", tts_mimo_prepare);
static const voice_tts_ops_t g_tts_audio = TTS_OPS("openai-audio", tts_audio_prepare);

int bkagent_cloud_register(void)
{
  int results[] = { voice_asr_register(&g_asr_mimo),
                    voice_asr_register(&g_asr_audio),
                    voice_tts_register(&g_tts_mimo),
                    voice_tts_register(&g_tts_audio) };
  for (unsigned int i = 0; i < sizeof(results) / sizeof(results[0]); i++)
    if (results[i] != 0 && results[i] != -ENOKEY) return results[i];
  return 0;
}

int bkagent_cloud_configure(const void *trust, size_t trust_size,
                           const void *cloud, size_t cloud_size)
{
  if (voice_asr_is_busy() || voice_tts_is_busy()) return -EBUSY;
  struct cloud_settings_s *next = calloc(1, sizeof(*next));
  if (!next) return -ENOMEM;
  next->references = 1;
  int ret = bkcloud_config_decode(&next->service, cloud, cloud_size);
  if (ret == 0) ret = bkvoice_config_load(&next->trust, trust, trust_size);
  if (ret == 0 && (strcmp(next->trust.host, next->service.host) ||
                   next->trust.port != next->service.port)) ret = -EINVAL;
  if (ret != 0) { settings_release(next); return ret; }
  pthread_mutex_lock(&g_config_lock);
  struct cloud_settings_s *previous = g_selected;
  g_selected = next;
  pthread_mutex_unlock(&g_config_lock);
  settings_release(previous);
  /* Selection is a separate official registry operation. Installing cloud
   * settings must never select cloud TTS over an explicitly local backend. */
  return 0;
}
