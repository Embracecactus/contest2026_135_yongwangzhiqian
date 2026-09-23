/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_agent_cloud.h"
#include "bk7258_cloud_audio.h"
#include "bk7258_voice_config.h"
#include "bk7258_voice_tls.h"
#include "bk7258_preferences.h"
#include "voice/voice_asr.h"
#include "voice/funasr_asr.h"
#include "voice/voice_tts.h"
#include "agent_config.h"
#include "infra/config_store.h"
#include "llm/llm_proxy.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <mbedtls/platform_util.h>

/* Immutable selected trust/config with a locked TLS session cache.
 * Reference counts include the configuration owner and each active backend.
 * Active TLS contexts and sockets are never shared between backends. */
struct cloud_settings_s
{
  struct bkvoice_config_s trust;
  struct bkcloud_config_s service;
  /* The configuration snapshot caches the TLS session only, never audio,
   * answers or a live socket.
   */
  mbedtls_ssl_session session;
  mbedtls_x509_time valid_from;
  mbedtls_x509_time valid_to;
  bool session_valid;
  unsigned int references;
};

struct cloud_backend_s
{
  struct cloud_settings_s *settings;
  struct bkcloud_config_s service;
  struct bkvoice_tls_s tls;
  mbedtls_x509_time session_from;
  mbedtls_x509_time session_to;
  bool session_offered;
  atomic_bool canceled;
};

static pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;
static struct cloud_settings_s *g_selected;
static struct cloud_backend_s g_asr;
static struct cloud_backend_s g_stream_asr;
static struct cloud_backend_s g_tts;
static struct cloud_backend_s g_llm;
static atomic_int g_thinking = ATOMIC_VAR_INIT(-1);

void bkagent_cloud_set_thinking(bool enabled)
{
  atomic_store(&g_thinking, enabled ? 1 : 0);
}

int bkagent_cloud_get_thinking(bool *enabled)
{
  if (!enabled) return -EINVAL;
  pthread_mutex_lock(&g_config_lock);
  int ret = !g_selected ? -EAGAIN :
            g_selected->service.dialect == 2 ? 0 : -ENOTSUP;
  pthread_mutex_unlock(&g_config_lock);
  int mode = atomic_load(&g_thinking);
  if (!ret && mode < 0) ret = -EAGAIN;
  if (!ret) *enabled = mode != 0;
  return ret;
}

static void settings_release(struct cloud_settings_s *settings)
{
  if (!settings) return;
  pthread_mutex_lock(&g_config_lock);
  bool last = --settings->references == 0;
  pthread_mutex_unlock(&g_config_lock);
  if (last)
    {
      mbedtls_ssl_session_free(&settings->session);
      bkvoice_config_clear(&settings->trust);
      bkcloud_config_clear(&settings->service);
      mbedtls_platform_zeroize(settings, sizeof(*settings));
      free(settings);
    }
}

static int certificate_time_compare(const mbedtls_x509_time *a,
                                     const mbedtls_x509_time *b)
{
  const int left[] = { a->year, a->mon, a->day, a->hour, a->min, a->sec };
  const int right[] = { b->year, b->mon, b->day, b->hour, b->min, b->sec };
  for (unsigned int i = 0; i < sizeof(left) / sizeof(left[0]); i++)
    if (left[i] != right[i]) return left[i] < right[i] ? -1 : 1;
  return 0;
}

static void certificate_time_limit(const mbedtls_x509_crt *chain,
                                    mbedtls_x509_time *from,
                                    mbedtls_x509_time *to)
{
  for (; chain; chain = chain->next)
    {
      if (!chain->raw.len) continue;
      if (certificate_time_compare(from, &chain->valid_from) < 0)
        *from = chain->valid_from;
      if (certificate_time_compare(to, &chain->valid_to) > 0)
        *to = chain->valid_to;
    }
}

static int session_load(void *context, mbedtls_ssl_context *ssl,
                         const char *host, uint16_t port)
{
  struct cloud_backend_s *backend = context;
  struct cloud_settings_s *settings = backend->settings;
  backend->session_offered = false;
  if (strcmp(host, settings->service.host) || port != settings->service.port)
    return -EPERM;
  pthread_mutex_lock(&g_config_lock);
  if (settings->session_valid &&
      (mbedtls_x509_time_is_future(&settings->valid_from) ||
       mbedtls_x509_time_is_past(&settings->valid_to)))
    {
      /* A resumed handshake does not resend the certificate chain, so an
       * expired authentication result must not be reused.
       */
      settings->session_valid = false;
      mbedtls_ssl_session_free(&settings->session);
      mbedtls_ssl_session_init(&settings->session);
    }
  bool offered = settings->session_valid;
  int ret = offered ? mbedtls_ssl_set_session(ssl, &settings->session) : 0;
  if (offered && ret == 0)
    {
      /* Each connection keeps its own time bounds and is unaffected by
       * another backend updating the shared cache.
       */
      backend->session_from = settings->valid_from;
      backend->session_to = settings->valid_to;
      backend->session_offered = true;
    }
  pthread_mutex_unlock(&g_config_lock);
  syslog(LOG_INFO, "AGENT TLS session offered=%d result=%d\n", offered, ret);
  return ret == 0 ? 0 :
         ret == MBEDTLS_ERR_SSL_ALLOC_FAILED ? -ENOMEM : -EKEYREJECTED;
}

static int session_save(void *context, const mbedtls_ssl_context *ssl,
                         const char *host, uint16_t port)
{
  struct cloud_backend_s *backend = context;
  struct cloud_settings_s *settings = backend->settings;
  const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(ssl);
  if (!peer || strcmp(host, settings->service.host) ||
      port != settings->service.port) return -EKEYREJECTED;
  if (backend->session_offered &&
      (mbedtls_x509_time_is_future(&backend->session_from) ||
       mbedtls_x509_time_is_past(&backend->session_to)))
    return -EKEYREJECTED;
  mbedtls_x509_time from = peer->valid_from;
  mbedtls_x509_time to = peer->valid_to;
  certificate_time_limit(peer, &from, &to);
  certificate_time_limit(&settings->trust.ca, &from, &to);
  if (backend->session_offered)
    {
      /* The resumed session copied by mbedTLS keeps the leaf certificate
       * only, so the time bounds of the original full chain are retained.
       * The trust configuration is immutable inside that snapshot; a
       * configuration replacement creates an empty cache and frees the old
       * one.
       */
      if (certificate_time_compare(&from, &backend->session_from) < 0)
        from = backend->session_from;
      if (certificate_time_compare(&to, &backend->session_to) > 0)
        to = backend->session_to;
    }
  /* The CA bundle may contain rotating roots that did not take part in this
   * verification; when the conservative intersection is unusable, only the
   * cache is disabled. It neither overrides the full verification mbedTLS
   * already completed nor caches a wider authentication window.
   */
  bool reusable = !mbedtls_x509_time_is_future(&from) &&
                  !mbedtls_x509_time_is_past(&to);
  pthread_mutex_lock(&g_config_lock);
  mbedtls_ssl_session_free(&settings->session);
  mbedtls_ssl_session_init(&settings->session);
  settings->session_valid = reusable &&
                            mbedtls_ssl_get_session(ssl, &settings->session) == 0;
  settings->valid_from = from;
  settings->valid_to = to;
  pthread_mutex_unlock(&g_config_lock);
  return 0;
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
      .session_load = session_load,
      .session_save = session_save,
      .session_context = backend,
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
  struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
  struct addrinfo *addresses = NULL;
  int resolved = getaddrinfo(host, NULL, &hints, &addresses);
  if (resolved != 0 || !addresses) return -EHOSTUNREACH;
  backend->tls.config.peer_address =
    ((struct sockaddr_in *)addresses->ai_addr)->sin_addr;
  freeaddrinfo(addresses);
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

/* Opt-in reference ASR endpoint. This has its own TLS connection and never
 * receives the LLM API key. The installed trust bundle and verified time are
 * still mandatory; a different endpoint cannot reuse the cloud TLS session.
 */
static int stream_asr_prepare(void *context)
{
  return request_prepare(context);
}

static int stream_asr_random(void *context, uint8_t *data, size_t size)
{
  return bkvoice_tls_ops()->random(
    &((struct cloud_backend_s *)context)->tls, data, size);
}

static int stream_asr_sha1(void *context, const uint8_t *data, size_t size,
                           uint8_t digest[20])
{
  return bkvoice_tls_ops()->sha1(
    &((struct cloud_backend_s *)context)->tls, data, size, digest);
}

static int stream_asr_cancel(void *context)
{
  return request_cancel(context);
}

static const funasr_asr_transport_t g_stream_transport =
{
  .prepare_request = stream_asr_prepare,
  .open_verified = cloud_open,
  .send = cloud_send,
  .recv = cloud_recv,
  .interrupt = stream_asr_cancel,
  .close = cloud_close,
  .random = stream_asr_random,
  .sha1 = stream_asr_sha1,
  .now_ms = bkvoice_config_now_ms,
};

int bkagent_cloud_prepare_asr(const char *name)
{
  if (!name) return -EINVAL;
  if (strcmp(name, "funasr")) return 0;
  if (voice_asr_is_busy()) return -EBUSY;
  int ret = funasr_asr_recover();
  if (ret) return ret; /* A failed close still owns its transport. */
  char host[128] = { 0 }, path[128] = { 0 }, port_text[8] = { 0 };
  if (claw_config_get("asr_stream_host", host, sizeof(host)) || !host[0] ||
      claw_config_get("asr_stream_path", path, sizeof(path)) || !path[0] ||
      claw_config_get("asr_stream_port", port_text, sizeof(port_text)))
    return -ENOKEY;
  char *end;
  unsigned long port = strtoul(port_text, &end, 10);
  if (!port_text[0] || *end || !port || port > 65535) return -EINVAL;
  backend_release(&g_stream_asr);
  pthread_mutex_lock(&g_config_lock);
  uint8_t dialect = g_selected ? g_selected->service.dialect : 0;
  pthread_mutex_unlock(&g_config_lock);
  ret = backend_prepare(&g_stream_asr, dialect);
  if (ret) return ret;
  g_stream_asr.tls.config.session_load = NULL;
  g_stream_asr.tls.config.session_save = NULL;
  g_stream_asr.tls.config.session_context = NULL;
  ret = funasr_asr_configure(&g_stream_transport, &g_stream_asr,
                            host, (uint16_t)port, path);
  if (ret) backend_release(&g_stream_asr);
  return ret;
}

static int asr_mimo_prepare(void) { return backend_prepare(&g_asr, 2); }
static int asr_audio_prepare(void) { return backend_prepare(&g_asr, 1); }
static int tts_mimo_prepare(void) { return backend_prepare(&g_tts, 2); }
static int tts_audio_prepare(void) { return backend_prepare(&g_tts, 1); }

struct llm_body_s { const char *data; size_t size; size_t offset; };

static int llm_body(void *buffer, size_t *size, const void **data,
                    size_t requested, void *context)
{
  struct llm_body_s *body = context;
  (void)buffer;
  size_t count = body->size - body->offset;
  if (count > requested) count = requested;
  *data = body->data + body->offset;
  *size = count;
  body->offset += count;
  return 0;
}

static int llm_transport(const char *request, char *response, size_t capacity,
                         size_t *length, int *status, void *context,
                         int (*check)(void *), void *request_context)
{
  struct cloud_backend_s *backend = context;
  struct bkcloud_http_s *http = calloc(1, sizeof(*http));
  if (!http) return -ENOMEM;
  struct llm_body_s body = { .data = request, .size = strlen(request) };
  char *adapted = NULL;
  *length = 0;
  *status = 0;
  int ret = request_prepare(backend);
  if (!ret && check) ret = check(request_context);

  /* MiMo enables deep thinking by default. The product's restored/confirmed
   * device setting is used; the service default is kept while the
   * configuration is not ready, and an explicit thinking choice from the
   * official caller is preserved. Only the selected service's protocol
   * parameter is added here; model, messages, tools and session ownership are
   * untouched.
   */
  int thinking_mode = atomic_load(&g_thinking);
  if (!ret && backend->service.dialect == 2 && thinking_mode >= 0)
    {
      cJSON *root = cJSON_Parse(request);
      if (!cJSON_IsObject(root)) ret = -EPROTO;
      else if (!cJSON_GetObjectItemCaseSensitive(root, "thinking"))
        {
          cJSON *thinking = cJSON_AddObjectToObject(root, "thinking");
          const char *type = thinking_mode ? "enabled" : "disabled";
          if (!thinking || !cJSON_AddStringToObject(thinking, "type", type))
            ret = -ENOMEM;
          else
            {
              adapted = cJSON_PrintUnformatted(root);
              if (!adapted) ret = -ENOMEM;
              else
                {
                  body.data = adapted;
                  body.size = strlen(adapted);
                  syslog(LOG_INFO,
                    "AGENT LLM MiMo thinking=%s source=device-setting\n", type);
                }
            }
        }
      cJSON_Delete(root);
    }

  if (!ret) ret = bkcloud_http_post(http, &backend->service, "chat/completions",
    &g_transport, backend, bkvoice_config_now_ms(NULL) + 60000u,
    llm_body, &body, body.size, response, capacity);
  if (atomic_load(&backend->canceled)) ret = -ECANCELED;
  *status = http->status;
  if (!ret) *length = http->received;
  if (adapted)
    {
      mbedtls_platform_zeroize(adapted, body.size + 1);
      cJSON_free(adapted);
    }
  mbedtls_platform_zeroize(http, sizeof(*http));
  free(http);
  syslog(LOG_INFO, "AGENT LLM transport=verified-cloud status=%d ret=%d bytes=%zu\n",
         *status, ret, *length);
  return ret;
}

static int llm_cancel(void *context) { return request_cancel(context); }

struct llm_stream_sink_s
{
  const struct bkcloud_http_s *http;
  int (*receive)(void *, const char *, size_t);
  void *receive_context;
  int (*check)(void *);
  void *request_context;
};

static int llm_stream_receive(void *context, const void *data, size_t size)
{
  struct llm_stream_sink_s *sink = context;
  if (!sink->http->active || sink->http->active->http_status != 200)
    return -EPROTO;
  int ret = sink->check ? sink->check(sink->request_context) : 0;
  return ret ? ret : sink->receive(sink->receive_context, data, size);
}

static int llm_stream_transport(const char *request,
  int (*receive)(void *, const char *, size_t), void *receive_context,
  int *status, void *context, int (*check)(void *), void *request_context)
{
  struct cloud_backend_s *backend = context;
  struct bkcloud_http_s *http = calloc(1, sizeof(*http));
  if (!http) return -ENOMEM;
  *status = 0;
  struct llm_stream_sink_s sink =
    { http, receive, receive_context, check, request_context };
  int ret = request_prepare(backend);
  if (!ret && check) ret = check(request_context);
  cJSON *root = !ret ? cJSON_Parse(request) : NULL;
  char *adapted = NULL;
  if (!ret && !cJSON_IsObject(root)) ret = -EPROTO;
  int thinking = atomic_load(&g_thinking);
  if (!ret && backend->service.dialect == 2 && thinking >= 0 &&
      !cJSON_GetObjectItemCaseSensitive(root, "thinking"))
    {
      cJSON *item = cJSON_AddObjectToObject(root, "thinking");
      if (!item || !cJSON_AddStringToObject(item, "type",
                                            thinking ? "enabled" : "disabled"))
        ret = -ENOMEM;
    }
  if (!ret)
    {
      adapted = cJSON_PrintUnformatted(root);
      if (!adapted) ret = -ENOMEM;
    }
  if (!ret)
    ret = bkcloud_http_events(http, &backend->service, &g_transport, backend,
      bkvoice_config_now_ms(NULL) + 60000u, adapted, strlen(adapted),
      llm_stream_receive, &sink, 1024u * 1024u);
  if (atomic_load(&backend->canceled)) ret = -ECANCELED;
  *status = http->status;
  if (adapted)
    {
      mbedtls_platform_zeroize(adapted, strlen(adapted));
      cJSON_free(adapted);
    }
  cJSON_Delete(root);
  mbedtls_platform_zeroize(http, sizeof(*http));
  free(http);
  return ret;
}

int bkagent_cloud_activate_llm(void)
{
  if (llm_request_busy()) return -EBUSY;
  int ret = llm_clear_transport();
  if (ret) return ret;
  backend_release(&g_llm);
  pthread_mutex_lock(&g_config_lock);
  uint8_t dialect = g_selected ? g_selected->service.dialect : 0;
  pthread_mutex_unlock(&g_config_lock);
  ret = backend_prepare(&g_llm, dialect);
  if (!ret) ret = llm_set_transports(g_llm.service.chat_model, g_llm.service.host,
                                    llm_transport, llm_stream_transport,
                                    llm_cancel, &g_llm);
  if (ret) backend_release(&g_llm);
  return ret;
}

int bkagent_cloud_clear(void)
{
  if (voice_asr_is_busy() || voice_tts_is_busy() || llm_request_busy())
    return -EBUSY;
  int ret = funasr_asr_recover();
  if (ret) return ret;
  ret = llm_clear_transport();
  if (ret) return ret;
  backend_release(&g_llm);
  backend_release(&g_tts);
  backend_release(&g_asr);
  backend_release(&g_stream_asr);
  pthread_mutex_lock(&g_config_lock);
  struct cloud_settings_s *settings = g_selected;
  g_selected = NULL;
  pthread_mutex_unlock(&g_config_lock);
  settings_release(settings);
  return 0;
}

int bkagent_cloud_verify_service(void)
{
  struct cloud_backend_s *backend = &g_llm;
  int ret = request_prepare(backend);
  if (!ret) ret = cloud_open(backend, backend->service.host,
                             backend->service.port,
                             bkvoice_config_now_ms(NULL) + 15000u);
  int closed = cloud_close(backend);
  if (!ret) ret = closed;
  syslog(LOG_INFO, "AGENT service TLS verified=%d result=%d\n", ret == 0, ret);
  return ret;
}
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
  int16_t pending[3];
  uint8_t pending_count;
};

#define TTS_SOURCE_RATE 24000u
#define TTS_OUTPUT_RATE 16000u
#define TTS_RESAMPLE_BUFFER_SIZE 512u

static int pcm_write(struct pcm_output_s *output, const void *data, size_t size)
{
  if (atomic_load(&g_tts.canceled)) return -ECANCELED;
  if (!size) return 0;
  if (output->callback)
    {
      /* The official callback creates, prepares and feeds its Media player. */
      output->callback(data, size, 0, output->context);
      if (atomic_load(&g_tts.canceled)) return -ECANCELED;
    }
  else
    {
      if (size > output->capacity - output->used) return -ENOSPC;
      memcpy(output->buffer + output->used, data, size);
    }
  output->used += size;
  return atomic_load(&g_tts.canceled) ? -ECANCELED : 0;
}

static int16_t pcm_read_s16le(const unsigned char *data)
{
  return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void pcm_write_s16le(unsigned char *data, int16_t sample)
{
  uint16_t value = (uint16_t)sample;
  data[0] = value & 0xff;
  data[1] = value >> 8;
}

/* MiMo emits fixed 24 kHz mono PCM while the validated BK7258 speaker ABI is
 * fixed at 16 kHz.  Normalize at the service-adapter boundary so the official
 * Agent and Media path receive the format advertised by this backend.  The
 * exact 3:2 linear conversion keeps state across arbitrary HTTP/SSE chunks. */
static int pcm_frames(struct pcm_output_s *output, const void *data, size_t size)
{
  const unsigned char *input = data;
  unsigned char converted[TTS_RESAMPLE_BUFFER_SIZE];
  size_t converted_size = 0;
  int ret;

  if (atomic_load(&g_tts.canceled)) return -ECANCELED;
  if (size & 1u) return -EPROTO;
  while (size)
    {
      output->pending[output->pending_count++] = pcm_read_s16le(input);
      input += sizeof(int16_t);
      size -= sizeof(int16_t);
      if (output->pending_count != 3) continue;

      pcm_write_s16le(converted + converted_size, output->pending[0]);
      converted_size += sizeof(int16_t);
      pcm_write_s16le(converted + converted_size,
                      (int16_t)(((int32_t)output->pending[1] +
                                 (int32_t)output->pending[2]) / 2));
      converted_size += sizeof(int16_t);
      output->pending_count = 0;

      if (converted_size == sizeof(converted))
        {
          ret = pcm_write(output, converted, converted_size);
          if (ret != 0) return ret;
          converted_size = 0;
        }
    }

  return pcm_write(output, converted, converted_size);
}

static int pcm_finish(struct pcm_output_s *output)
{
  unsigned char converted[2 * sizeof(int16_t)];
  size_t converted_size = 0;

  if (output->pending_count)
    {
      pcm_write_s16le(converted, output->pending[0]);
      converted_size = sizeof(int16_t);
      if (output->pending_count == 2)
        {
          pcm_write_s16le(converted + converted_size, output->pending[1]);
          converted_size += sizeof(int16_t);
        }
      output->pending_count = 0;
    }

  return pcm_write(output, converted, converted_size);
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
  if (ret == 0) ret = pcm_finish(output);
  if (ret == 0 && (output->used == 0 || output->has_tail)) ret = -EPROTO;
  if (ret == 0 && output->callback)
    output->callback(NULL, 0, 1, output->context);
  if (decoder) bkcloud_tts_clear(decoder);
  if (client) mbedtls_platform_zeroize(client, sizeof(*client));
  free(decoder);
  free(client);
  syslog(LOG_INFO, "AGENT TTS backend=%s mode=%s source_rate=%u "
    "output_rate=%u ret=%d bytes=%zu\n",
    g_tts.settings->service.dialect == 2 ? "mimo" : "openai-audio",
    output->callback ? "audio-stream/full-text" : "batch",
    TTS_SOURCE_RATE, TTS_OUTPUT_RATE, ret, output->used);
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

static int capabilities(voice_tts_capabilities_t *caps)
{
  *caps = (voice_tts_capabilities_t)
    {
      .location = VOICE_TTS_LOCATION_REMOTE,
      .needs_network = true,
      .sample_rate = TTS_OUTPUT_RATE,
      .batch_sample_rate = TTS_OUTPUT_RATE,
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
    .get_capabilities = capabilities, \
    .cancel = tts_cancel, .deinit = tts_release }

static const voice_asr_ops_t g_asr_mimo = ASR_OPS("mimo", asr_mimo_prepare);
static const voice_asr_ops_t g_asr_audio = ASR_OPS("openai-audio", asr_audio_prepare);
static const voice_tts_ops_t g_tts_mimo = TTS_OPS("mimo", tts_mimo_prepare);
static const voice_tts_ops_t g_tts_audio = TTS_OPS("openai-audio", tts_audio_prepare);

int bkagent_cloud_register(void)
{
  int results[] = { voice_asr_register(&g_asr_mimo),
                    voice_asr_register(&g_asr_audio),
                    funasr_asr_register(),
                    voice_tts_register(&g_tts_mimo),
                    voice_tts_register(&g_tts_audio) };
  for (unsigned int i = 0; i < sizeof(results) / sizeof(results[0]); i++)
    if (results[i] != 0 && results[i] != -ENOKEY) return results[i];
  return 0;
}

int bkagent_cloud_models_get(struct bkcloud_models_s *models)
{
  if (!models) return -EINVAL;
  memset(models, 0, sizeof(*models));
  pthread_mutex_lock(&g_config_lock);
  if (g_selected)
    {
      memcpy(models->asr_model, g_selected->service.asr_model,
             sizeof(models->asr_model));
      memcpy(models->chat_model, g_selected->service.chat_model,
             sizeof(models->chat_model));
      memcpy(models->tts_model, g_selected->service.tts_model,
             sizeof(models->tts_model));
    }
  int ret = g_selected ? 0 : -ENOKEY;
  pthread_mutex_unlock(&g_config_lock);
  return ret;
}

static int configure(const void *trust, size_t trust_size,
                     const void *cloud, size_t cloud_size,
                     const struct bkcloud_models_s *candidate)
{
  if (voice_asr_is_busy() || voice_tts_is_busy() || llm_request_busy()) return -EBUSY;
  struct cloud_settings_s *next = calloc(1, sizeof(*next));
  if (!next) return -ENOMEM;
  mbedtls_ssl_session_init(&next->session);
  next->references = 1;
  int ret = bkcloud_config_decode(&next->service, cloud, cloud_size);
  if (!ret)
    {
      struct bkcloud_models_s models = { 0 };
      int selected = 0;
      if (candidate)
        {
          models = *candidate;
        }
      else
        {
          selected = bk7258_preferences_cloud_models_get(&models);
        }
      if (!selected)
        {
          memcpy(next->service.asr_model, models.asr_model, sizeof(models.asr_model));
          memcpy(next->service.chat_model, models.chat_model, sizeof(models.chat_model));
          memcpy(next->service.tts_model, models.tts_model, sizeof(models.tts_model));
        }
      else if (selected != -ENOENT) ret = selected;
      mbedtls_platform_zeroize(&models, sizeof(models));
    }
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

int bkagent_cloud_configure(const void *trust, size_t trust_size,
                           const void *cloud, size_t cloud_size)
{
  return configure(trust, trust_size, cloud, cloud_size, NULL);
}

int bkagent_cloud_configure_models(const void *trust, size_t trust_size,
                                  const void *cloud, size_t cloud_size,
                                  const struct bkcloud_models_s *models)
{
  if (!models) return -EINVAL;
  return configure(trust, trust_size, cloud, cloud_size, models);
}
