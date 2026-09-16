/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_agent_cloud.h"
#include "bk7258_cloud_audio.h"
#include "bk7258_preferences.h"
#include "bk7258_voice_config.h"
#include "bk7258_voice_tls.h"
#include "voice/voice_asr.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <mbedtls/platform_util.h>

/* This adapter owns only one selected service configuration and one
 * synchronous ASR request at a time. Capture, turns, history and playback
 * remain owned by the official Agent and Media frameworks. */
struct cloud_settings_s
{
  struct bkvoice_config_s trust;
  struct bkcloud_config_s service;
};

struct cloud_request_s
{
  const struct cloud_settings_s *settings;
  struct bkvoice_tls_s tls;
};

static pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_request_lock = PTHREAD_MUTEX_INITIALIZER;
static struct cloud_settings_s *g_selected;
static uint8_t g_active_dialect;
static int g_backend_result = -ENOKEY;

static void settings_clear(struct cloud_settings_s *settings)
{
  if (settings == NULL)
    {
      return;
    }

  bkvoice_config_clear(&settings->trust);
  bkcloud_config_clear(&settings->service);
  mbedtls_platform_zeroize(settings, sizeof(*settings));
  free(settings);
}

static int cloud_open(void *context, const char *host, uint16_t port,
                      uint64_t deadline)
{
  struct cloud_request_s *request = context;
  struct addrinfo hints =
  {
    .ai_family = AF_INET,
    .ai_socktype = SOCK_STREAM,
  };
  struct addrinfo *addresses = NULL;
  int resolved;

  resolved = getaddrinfo(host, NULL, &hints, &addresses);
  if (resolved != 0 || addresses == NULL)
    {
      return -EHOSTUNREACH;
    }

  request->tls.config.peer_address =
    ((struct sockaddr_in *)addresses->ai_addr)->sin_addr;
  freeaddrinfo(addresses);
  return bkvoice_tls_ops()->open_verified(&request->tls, host, port, deadline);
}

static ssize_t cloud_send(void *context, const uint8_t *data, size_t size,
                          uint64_t deadline)
{
  struct cloud_request_s *request = context;
  return bkvoice_tls_ops()->send(&request->tls, data, size, deadline);
}

static ssize_t cloud_recv(void *context, uint8_t *data, size_t size,
                          uint64_t deadline)
{
  struct cloud_request_s *request = context;
  return bkvoice_tls_ops()->recv(&request->tls, data, size, deadline);
}

static int cloud_close(void *context)
{
  struct cloud_request_s *request = context;
  return bkvoice_tls_ops()->close(&request->tls);
}

static const struct bkvoice_wss_tls_ops_s g_transport =
{
  .open_verified = cloud_open,
  .send = cloud_send,
  .recv = cloud_recv,
  .close = cloud_close,
};

static int request_prepare(struct cloud_request_s *request,
                           const struct cloud_settings_s *settings)
{
  struct bkvoice_tls_config_s tls =
  {
    .peer_address = settings->trust.peer_address,
    .server_ca = &settings->trust.ca,
    .server_auth_only = true,
    .trusted_time = bkvoice_config_trusted_time,
    .now_ms = bkvoice_config_now_ms,
    .clock_context = (void *)&settings->trust,
  };
  int ret;

  memset(request, 0, sizeof(*request));
  request->settings = settings;
  ret = bkvoice_config_trusted_time((void *)&settings->trust);
  if (ret == 0)
    {
      ret = bkvoice_tls_initialize(&request->tls, &tls);
    }

  return ret;
}

static void request_release(struct cloud_request_s *request)
{
  if (!request->tls.initialized)
    {
      return;
    }

  if (request->tls.opened || request->tls.socket_open)
    {
      (void)bkvoice_tls_ops()->close(&request->tls);
    }

  (void)bkvoice_tls_uninitialize(&request->tls);
  memset(request, 0, sizeof(*request));
}

static int asr_init(uint8_t dialect)
{
  pthread_mutex_lock(&g_config_lock);
  if (g_selected == NULL)
    {
      g_backend_result = -ENOKEY;
    }
  else if (g_selected->service.dialect != dialect)
    {
      g_backend_result = -EINVAL;
    }
  else
    {
      g_active_dialect = dialect;
      g_backend_result = 0;
    }
  pthread_mutex_unlock(&g_config_lock);
  return g_backend_result;
}

static int asr_mimo_init(void)
{
  return asr_init(2);
}

static int asr_openai_init(void)
{
  return asr_init(1);
}

static void asr_deinit(void)
{
  g_active_dialect = 0;
}

static int recognize(const unsigned char *pcm, size_t size,
                     char *text, size_t capacity)
{
  struct cloud_request_s request;
  struct bkcloud_client_s *client = NULL;
  const struct cloud_settings_s *settings;
  const char *backend;
  int ret;

  if (pcm == NULL || size == 0 || text == NULL || capacity == 0)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_request_lock);
  pthread_mutex_lock(&g_config_lock);
  settings = g_selected;
  if (settings == NULL || g_active_dialect == 0 ||
      settings->service.dialect != g_active_dialect)
    {
      settings = NULL;
    }
  pthread_mutex_unlock(&g_config_lock);

  if (settings == NULL)
    {
      ret = -ENOKEY;
      goto out;
    }

  backend = settings->service.dialect == 2 ? "mimo" : "openai-audio";
  client = calloc(1, sizeof(*client));
  if (client == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  ret = request_prepare(&request, settings);
  if (ret == 0)
    {
      ret = bkcloud_recognize(client, &settings->service, &g_transport,
                              &request,
                              bkvoice_config_now_ms(NULL) + 60000u,
                              pcm, size, text, capacity);
      request_release(&request);
    }

  if (ret != 0)
    {
      text[0] = '\0';
    }

  syslog(LOG_INFO, "AGENT ASR backend=%s mode=batch ret=%d\n",
         backend, ret);
out:
  if (client != NULL)
    {
      mbedtls_platform_zeroize(client, sizeof(*client));
      free(client);
    }
  pthread_mutex_unlock(&g_request_lock);
  return ret;
}

static const voice_asr_ops_t g_asr_mimo =
{
  .name = "mimo",
  .init = asr_mimo_init,
  .recognize = recognize,
  .deinit = asr_deinit,
};

static const voice_asr_ops_t g_asr_openai =
{
  .name = "openai-audio",
  .init = asr_openai_init,
  .recognize = recognize,
  .deinit = asr_deinit,
};

int bkagent_cloud_register(void)
{
  int ret;

  ret = voice_asr_register(&g_asr_mimo);
  if (ret != 0)
    {
      return ret;
    }

  return voice_asr_register(&g_asr_openai);
}

int bkagent_cloud_activate_asr(uint8_t dialect)
{
  const char *name;
  int ret;

  if (dialect == 1)
    {
      name = "openai-audio";
    }
  else if (dialect == 2)
    {
      name = "mimo";
    }
  else
    {
      return -ENOTSUP;
    }

  pthread_mutex_lock(&g_request_lock);
  ret = voice_asr_set_backend(name);
  if (ret == 0)
    {
      ret = g_backend_result;
    }
  pthread_mutex_unlock(&g_request_lock);
  return ret;
}

int bkagent_cloud_models_get(struct bkcloud_models_s *models)
{
  int ret;

  if (models == NULL)
    {
      return -EINVAL;
    }

  memset(models, 0, sizeof(*models));
  pthread_mutex_lock(&g_config_lock);
  if (g_selected != NULL)
    {
      memcpy(models->asr_model, g_selected->service.asr_model,
             sizeof(models->asr_model));
      memcpy(models->chat_model, g_selected->service.chat_model,
             sizeof(models->chat_model));
      memcpy(models->tts_model, g_selected->service.tts_model,
             sizeof(models->tts_model));
    }
  ret = g_selected != NULL ? 0 : -ENOKEY;
  pthread_mutex_unlock(&g_config_lock);
  return ret;
}

int bkagent_cloud_configure(const void *trust, size_t trust_size,
                           const void *cloud, size_t cloud_size)
{
  struct cloud_settings_s *next;
  struct cloud_settings_s *previous;
  struct bkcloud_models_s models;
  int selected;
  int ret;

  next = calloc(1, sizeof(*next));
  if (next == NULL)
    {
      return -ENOMEM;
    }

  ret = bkcloud_config_decode(&next->service, cloud, cloud_size);
  if (ret == 0)
    {
      selected = bk7258_preferences_cloud_models_get(&models);
      if (selected == 0)
        {
          memcpy(next->service.asr_model, models.asr_model,
                 sizeof(models.asr_model));
          memcpy(next->service.chat_model, models.chat_model,
                 sizeof(models.chat_model));
          memcpy(next->service.tts_model, models.tts_model,
                 sizeof(models.tts_model));
        }
      else if (selected != -ENOENT)
        {
          ret = selected;
        }
      mbedtls_platform_zeroize(&models, sizeof(models));
    }

  if (ret == 0)
    {
      ret = bkvoice_config_load(&next->trust, trust, trust_size);
    }

  if (ret == 0 &&
      (strcmp(next->trust.host, next->service.host) != 0 ||
       next->trust.port != next->service.port))
    {
      ret = -EINVAL;
    }

  if (ret != 0)
    {
      settings_clear(next);
      return ret;
    }

  pthread_mutex_lock(&g_request_lock);
  pthread_mutex_lock(&g_config_lock);
  previous = g_selected;
  g_selected = next;
  g_active_dialect = 0;
  g_backend_result = -ENOKEY;
  pthread_mutex_unlock(&g_config_lock);
  settings_clear(previous);
  pthread_mutex_unlock(&g_request_lock);
  return 0;
}

int bkagent_cloud_verify_service(void)
{
  struct cloud_request_s request;
  const struct cloud_settings_s *settings;
  int closed;
  int ret;

  pthread_mutex_lock(&g_request_lock);
  pthread_mutex_lock(&g_config_lock);
  settings = g_selected;
  pthread_mutex_unlock(&g_config_lock);
  if (settings == NULL)
    {
      ret = -ENOKEY;
      goto out;
    }

  ret = request_prepare(&request, settings);
  if (ret == 0)
    {
      ret = cloud_open(&request, settings->service.host,
                       settings->service.port,
                       bkvoice_config_now_ms(NULL) + 15000u);
      if (ret == 0)
        {
          closed = cloud_close(&request);
          if (closed != 0)
            {
              ret = closed;
            }
        }
      request_release(&request);
    }

  syslog(LOG_INFO, "AGENT service TLS verified=%d result=%d\n",
         ret == 0, ret);
out:
  pthread_mutex_unlock(&g_request_lock);
  return ret;
}
