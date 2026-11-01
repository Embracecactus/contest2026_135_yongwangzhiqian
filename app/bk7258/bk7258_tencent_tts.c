/****************************************************************************
 * apps/bk7258/bk7258_tencent_tts.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Experiment-only Tencent Cloud continuous-text TTS provider. Long-term API
 * credentials never enter this module: an authenticated local operator
 * sends one short-lived, single-use signed path through the existing BKTEST
 * channel.
 */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_tencent_tts.h"

#include "bk7258_engineering_test.h"
#include "bk7258_voice_config.h"
#include "bk7258_voice_tls.h"
#include "core/agent_trace.h"
#include "voice/voice_channel.h"
#include "voice/voice_tts.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#include <netutils/cJSON.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TENCENT_HEADER_MAX 4096u
#define TENCENT_TEXT_EVENT_MAX 4096u
#define TENCENT_PCM_CHUNK 2048u
#define TENCENT_SEGMENT_MAX 320u
#define TENCENT_CONNECT_MS 15000u
#define TENCENT_FIRST_PCM_MS 15000u
#define TENCENT_IDLE_PCM_MS 10000u
#define TENCENT_TOTAL_MS 120000u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct tencent_session_s
{
  pthread_mutex_t lock;
  pthread_mutex_t tx_lock;
  pthread_cond_t changed;
  pthread_t receiver;
  voice_tts_chunk_cb pcm_cb;
  void *pcm_context;
  uint64_t request;
  uint64_t deadline;
  uint64_t last_pcm_ms;
  size_t pcm_bytes;
  int error;
  bool receiver_started;
  bool ready;
  bool complete_sent;
  bool final;
  bool first_pcm;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct
{
  struct bkvoice_config_s trust;
  struct bkvoice_tls_s tls;
  char path[BKTENCENT_TTS_PATH_MAX + 1u];
  char session[129];
  char previous_backend[32];
  atomic_bool canceled;
  bool configured;
  bool initialized;
  bool consumed;
} g_tencent;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t now_ms(void)
{
  return bkvoice_config_now_ms(NULL);
}

static int canceled(void)
{
  return atomic_load(&g_tencent.canceled) ? -ECANCELED : 0;
}

static int send_all(const uint8_t *data, size_t size, uint64_t deadline)
{
  const struct bkvoice_wss_tls_ops_s *ops = bkvoice_tls_ops();
  for (size_t offset = 0; offset < size; )
    {
      int ret = canceled();
      if (ret)
        {
          return ret;
        }

      ssize_t sent =
          ops->send(&g_tencent.tls, data + offset, size - offset, deadline);
      if (sent <= 0 || (size_t)sent > size - offset)
        {
          return sent < 0 ? (int)sent : -EPIPE;
        }

      offset += (size_t)sent;
    }

  return 0;
}

static int recv_exact(uint8_t *data, size_t size, uint64_t deadline)
{
  const struct bkvoice_wss_tls_ops_s *ops = bkvoice_tls_ops();
  for (size_t offset = 0; offset < size; )
    {
      int ret = canceled();
      if (ret)
        {
          return ret;
        }

      ssize_t received =
          ops->recv(&g_tencent.tls, data + offset, size - offset, deadline);
      if (received <= 0 || (size_t)received > size - offset)
        {
          return received < 0 ? (int)received : -ECONNRESET;
        }

      offset += (size_t)received;
    }

  return 0;
}

static int token_header(const char *value, const char *token)
{
  size_t target = strlen(token);
  while (*value)
    {
      while (*value == ' ' || *value == '\t' || *value == ',')
        {
          value++;
        }

      const char *end = value;
      while (*end && *end != ',')
        {
          end++;
        }

      const char *trimmed = end;
      while (trimmed > value &&
             (trimmed[(-1)] == ' ' || trimmed[(-1)] == '\t'))
        {
          trimmed--;
        }

      if ((size_t)(trimmed - value) == target &&
          strncasecmp(value, token, target) == 0)
        {
          return 1;
        }

      value = *end ? end + 1 : end;
    }

  return 0;
}

static int websocket_upgrade(void)
{
  const struct bkvoice_wss_tls_ops_s *ops = bkvoice_tls_ops();
  uint8_t nonce[16];
  uint8_t digest[20];
  uint8_t key[32];
  uint8_t accept[32];
  size_t key_size = 0;
  size_t accept_size = 0;
  static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  if (ops->random(&g_tencent.tls, nonce, sizeof(nonce)) != 0 ||
      mbedtls_base64_encode(key, sizeof(key), &key_size, nonce,
                            sizeof(nonce)) != 0)
    {
      return -EIO;
    }

  char challenge[sizeof(key) + sizeof(guid)];
  memcpy(challenge, key, key_size);
  memcpy(challenge + key_size, guid, sizeof(guid) - 1u);
  if (ops->sha1(&g_tencent.tls, (const uint8_t *)challenge,
                key_size + sizeof(guid) - 1u, digest) != 0 ||
      mbedtls_base64_encode(accept, sizeof(accept), &accept_size, digest,
                            sizeof(digest)) != 0)
    {
      return -EIO;
    }

  size_t request_size = strlen(g_tencent.path) + 512u;
  char *request = malloc(request_size);
  if (!request)
    {
      return -ENOMEM;
    }

  int count = snprintf(
      request, request_size,
      "GET %s HTTP/1.1\r\nHost: " BKTENCENT_TTS_HOST "\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: %.*s\r\nSec-WebSocket-Version: 13\r\n\r\n",
      g_tencent.path, (int)key_size, key);
  int ret = count <= 0 || (size_t)count >= request_size
                ? -EOVERFLOW
                : send_all((const uint8_t *)request, (size_t)count,
                           now_ms() + TENCENT_CONNECT_MS);
  mbedtls_platform_zeroize(request, request_size);
  free(request);
  if (ret)
    {
      return ret;
    }

  char response[TENCENT_HEADER_MAX + 1u];
  size_t used = 0;
  do
    {
      if (used == TENCENT_HEADER_MAX)
        {
          return -EOVERFLOW;
        }

      ret = recv_exact((uint8_t *)response + used, 1,
                       now_ms() + TENCENT_CONNECT_MS);
      if (ret)
        {
          return ret;
        }

      used++;
    }
  while (used < 4 || memcmp(response + used - 4, "\r\n\r\n", 4));
  response[used] = '\0';
  char *status_end = strstr(response, "\r\n");
  if (!status_end || status_end - response < 12 ||
      memcmp(response, "HTTP/1.1 101", 12) ||
      (status_end != response + 12 && response[12] != ' '))
    {
      return -EPROTO;
    }

  int upgrade = 0;
  int connection = 0;
  int accepted = 0;
  char *line = status_end;
  while (line && line[2] != '\r')
    {
      line += 2;
      char *end = strstr(line, "\r\n");
      if (!end)
        {
          return -EPROTO;
        }

      *end = '\0';
      char *colon = strchr(line, ':');
      if (!colon)
        {
          return -EPROTO;
        }

      *colon++ = '\0';
      while (*colon == ' ' || *colon == '\t')
        {
          colon++;
        }

      if (!strcasecmp(line, "Upgrade"))
        {
          upgrade = token_header(colon, "websocket");
        }
      else if (!strcasecmp(line, "Connection"))
        {
          connection = token_header(colon, "Upgrade");
        }
      else if (!strcasecmp(line, "Sec-WebSocket-Accept"))
        {
          accepted = strlen(colon) == accept_size &&
                     !memcmp(colon, accept, accept_size);
        }

      line = end;
    }

  return upgrade && connection && accepted ? 0 : -EPROTO;
}

static int ws_send(uint8_t opcode, const uint8_t *data, size_t size,
                   uint64_t deadline)
{
  if (size > UINT16_MAX || ((opcode & 8u) && size > 125u))
    {
      return -EOVERFLOW;
    }

  uint8_t header[8];
  uint8_t mask[4];
  uint8_t scratch[512];
  size_t header_size = 2;
  header[0] = 0x80u | opcode;
  if (size < 126u)
    {
      header[1] = 0x80u | (uint8_t)size;
    }
  else
    {
      header[1] = 0x80u | 126u;
      header[2] = (uint8_t)(size >> 8);
      header[3] = (uint8_t)size;
      header_size = 4;
    }

  if (bkvoice_tls_ops()->random(&g_tencent.tls, mask, sizeof(mask)) != 0)
    {
      return -EIO;
    }

  memcpy(header + header_size, mask, sizeof(mask));
  header_size += sizeof(mask);
  int ret = send_all(header, header_size, deadline);
  for (size_t offset = 0; !ret && offset < size; )
    {
      size_t count = size - offset;
      if (count > sizeof(scratch))
        {
          count = sizeof(scratch);
        }

      for (size_t i = 0; i < count; i++)
        {
          scratch[i] = data[offset + i] ^ mask[(offset + i) & 3u];
        }

      ret = send_all(scratch, count, deadline);
      offset += count;
    }

  return ret;
}

static int ws_send_locked(struct tencent_session_s *session, uint8_t opcode,
                          const uint8_t *data, size_t size)
{
  pthread_mutex_lock(&session->tx_lock);
  int ret = ws_send(opcode, data, size, session->deadline);
  pthread_mutex_unlock(&session->tx_lock);
  return ret;
}

static void session_fail(struct tencent_session_s *session, int error)
{
  pthread_mutex_lock(&session->lock);
  if (!session->error)
    {
      session->error = error ? error : -EIO;
    }

  pthread_cond_broadcast(&session->changed);
  pthread_mutex_unlock(&session->lock);
}

static int parse_event(struct tencent_session_s *session,
                       const uint8_t *data, size_t size)
{
  if (!size || size > TENCENT_TEXT_EVENT_MAX || memchr(data, '\0', size))
    {
      return -EPROTO;
    }

  char text[TENCENT_TEXT_EVENT_MAX + 1u];
  memcpy(text, data, size);
  text[size] = '\0';
  const char *end = NULL;
  cJSON *root = cJSON_ParseWithLengthOpts(text, size, &end, false);
  while (end && end < text + size &&
         (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
    {
      end++;
    }

  if (!root || end != text + size || !cJSON_IsObject(root))
    {
      cJSON_Delete(root);
      return -EPROTO;
    }

  cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
  cJSON *session_id = cJSON_GetObjectItemCaseSensitive(root, "session_id");
  cJSON *ready = cJSON_GetObjectItemCaseSensitive(root, "ready");
  cJSON *heartbeat = cJSON_GetObjectItemCaseSensitive(root, "heartbeat");
  cJSON *final = cJSON_GetObjectItemCaseSensitive(root, "final");
  int ret = !cJSON_IsNumber(code) || code->valuedouble != code->valueint ||
                    !cJSON_IsString(session_id) ||
                    strcmp(session_id->valuestring, g_tencent.session)
                ? -EPROTO
            : code->valueint ? -EREMOTEIO
                             : 0;
  if (!ret && ready &&
      (!cJSON_IsNumber(ready) ||
       (ready->valueint != 0 && ready->valueint != 1)))
    {
      ret = -EPROTO;
    }

  if (!ret && heartbeat &&
      (!cJSON_IsNumber(heartbeat) ||
       (heartbeat->valueint != 0 && heartbeat->valueint != 1)))
    {
      ret = -EPROTO;
    }

  if (!ret && final &&
      (!cJSON_IsNumber(final) ||
       (final->valueint != 0 && final->valueint != 1)))
    {
      ret = -EPROTO;
    }

  if (!ret)
    {
      pthread_mutex_lock(&session->lock);
      if (ready && ready->valueint)
        {
          session->ready = true;
          agent_trace_stage(session->request, "tencent_ready", 1, 0);
        }

      if (final && final->valueint)
        {
          if (!session->complete_sent)
            {
              ret = -EPROTO;
            }
          else
            {
              session->final = true;
            }

          agent_trace_stage(session->request, "tencent_final",
                            session->pcm_bytes, ret);
        }

      pthread_cond_broadcast(&session->changed);
      pthread_mutex_unlock(&session->lock);
    }

  cJSON_Delete(root);
  return ret;
}

static uint64_t receive_deadline(struct tencent_session_s *session)
{
  uint64_t now = now_ms();
  uint64_t idle = now + (session->first_pcm ? TENCENT_IDLE_PCM_MS
                                            : TENCENT_FIRST_PCM_MS);
  return idle < session->deadline ? idle : session->deadline;
}

static int receive_payload(struct tencent_session_s *session,
                           uint8_t opcode, uint64_t size, bool final)
{
  if ((opcode & 8u) != 0)
    {
      if (!final || size > 125u)
        {
          return -EPROTO;
        }

      uint8_t control[125];
      int ret =
          recv_exact(control, (size_t)size, receive_deadline(session));
      if (ret)
        {
          return ret;
        }

      if (opcode == 0x8u)
        {
          return session->final ? 1 : -ECONNRESET;
        }

      if (opcode == 0x9u)
        {
          return ws_send_locked(session, 0xau, control, (size_t)size);
        }

      return opcode == 0xau ? 0 : -EPROTO;
    }

  if (opcode == 0x1u)
    {
      if (!final || size > TENCENT_TEXT_EVENT_MAX)
        {
          return -EPROTO;
        }

      uint8_t event[TENCENT_TEXT_EVENT_MAX];
      int ret = recv_exact(event, (size_t)size, receive_deadline(session));
      return ret ? ret : parse_event(session, event, (size_t)size);
    }

  if (opcode != 0x2u || !final || (size & 1u))
    {
      return -EPROTO;
    }

  uint8_t pcm[TENCENT_PCM_CHUNK];
  while (size)
    {
      size_t count = size > sizeof(pcm) ? sizeof(pcm) : (size_t)size;
      int ret = recv_exact(pcm, count, receive_deadline(session));
      if (ret)
        {
          return ret;
        }

      uint64_t arrived = now_ms();
      if (!session->first_pcm)
        {
          session->first_pcm = true;
          agent_trace_stage(session->request, "tencent_first_pcm", count,
                            0);
        }
      else
        {
          agent_trace_stage(session->request, "tencent_pcm_gap",
                            arrived - session->last_pcm_ms, 0);
        }

      session->last_pcm_ms = arrived;
      session->pcm_cb(pcm, count, 0, session->pcm_context);
      session->pcm_bytes += count;
      size -= count;
      if ((ret = canceled()) != 0)
        {
          return ret;
        }
    }

  return 0;
}

static int receive_frame(struct tencent_session_s *session)
{
  uint8_t header[2];
  int ret = recv_exact(header, sizeof(header), receive_deadline(session));
  if (ret)
    {
      return ret;
    }

  bool final = (header[0] & 0x80u) != 0;
  uint8_t opcode = header[0] & 0x0fu;
  if ((header[0] & 0x70u) || (header[1] & 0x80u))
    {
      return -EPROTO;
    }

  uint64_t size = header[1] & 0x7fu;
  if (size == 126u)
    {
      uint8_t extended[2];
      ret =
          recv_exact(extended, sizeof(extended), receive_deadline(session));
      if (ret)
        {
          return ret;
        }

      size = ((uint64_t)extended[0] << 8) | extended[1];
      if (size < 126u)
        {
          return -EPROTO;
        }
    }
  else if (size == 127u)
    {
      uint8_t extended[8];
      ret =
          recv_exact(extended, sizeof(extended), receive_deadline(session));
      if (ret)
        {
          return ret;
        }

      if (extended[0] & 0x80u)
        {
          return -EPROTO;
        }

      size = 0;
      for (unsigned int i = 0; i < sizeof(extended); i++)
        {
          size = (size << 8) | extended[i];
        }

      if (size <= UINT16_MAX || size > UINT32_MAX)
        {
          return -EPROTO;
        }
    }

  return receive_payload(session, opcode, size, final);
}

static void *receiver_run(void *context)
{
  struct tencent_session_s *session = context;
  int ret = 0;
  while (!ret)
    {
      pthread_mutex_lock(&session->lock);
      bool final = session->final;
      pthread_mutex_unlock(&session->lock);
      if (final)
        {
          break;
        }

      ret = receive_frame(session);
      if (ret == 1)
        {
          ret = 0;
        }
    }

  if (ret)
    {
      session_fail(session, ret);
    }

  return NULL;
}

static int wait_ready(struct tencent_session_s *session)
{
  pthread_mutex_lock(&session->lock);
  while (!session->ready && !session->error && !canceled())
    {
      struct timespec until;
      clock_gettime(CLOCK_REALTIME, &until);
      until.tv_sec += 1;
      (void)pthread_cond_timedwait(&session->changed, &session->lock,
                                   &until);
      if (now_ms() >= session->deadline)
        {
          session->error = -ETIMEDOUT;
        }
    }

  int ret = session->error ? session->error : canceled();
  pthread_mutex_unlock(&session->lock);
  return ret;
}

static int random_message_id(char id[33])
{
  static const char hex[] = "0123456789abcdef";
  uint8_t random[16];
  if (bkvoice_tls_ops()->random(&g_tencent.tls, random, sizeof(random)) !=
      0)
    {
      return -EIO;
    }

  for (size_t i = 0; i < sizeof(random); i++)
    {
      id[2 * i] = hex[random[i] >> 4];
      id[2 * i + 1] = hex[random[i] & 15];
    }

  id[32] = '\0';
  return 0;
}

static int send_action(struct tencent_session_s *session,
                       const char *action, const char *data,
                       size_t data_size)
{
  char message_id[33];
  int ret = random_message_id(message_id);
  if (ret)
    {
      return ret;
    }

  cJSON *root = cJSON_CreateObject();
  if (!root ||
      !cJSON_AddStringToObject(root, "session_id", g_tencent.session) ||
      !cJSON_AddStringToObject(root, "message_id", message_id) ||
      !cJSON_AddStringToObject(root, "action", action) ||
      !cJSON_AddStringToObject(root, "data", data ? data : ""))
    {
      cJSON_Delete(root);
      return -ENOMEM;
    }

  char *wire = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!wire)
    {
      return -ENOMEM;
    }

  size_t size = strlen(wire);
  if ((data && strlen(data) != data_size) || size > UINT16_MAX)
    {
      ret = -EPROTO;
    }
  else
    {
      ret = ws_send_locked(session, 0x1u, (const uint8_t *)wire, size);
    }

  mbedtls_platform_zeroize(wire, size);
  free(wire);
  return ret;
}

static int resolve_peer(void)
{
  struct addrinfo hints =
  {
    .ai_family = AF_INET,
    .ai_socktype = SOCK_STREAM
  };

  struct addrinfo *addresses = NULL;
  uint64_t begin = now_ms();
  int resolved = getaddrinfo(BKTENCENT_TTS_HOST, NULL, &hints, &addresses);
  int ret = resolved || !addresses ? -EHOSTUNREACH : 0;
  if (!ret)
    {
      g_tencent.tls.config.peer_address =
          ((struct sockaddr_in *)addresses->ai_addr)->sin_addr;
    }

  if (addresses)
    {
      freeaddrinfo(addresses);
    }

  agent_trace_stage(voice_channel_request_id(), "tencent_dns",
                    now_ms() - begin, ret);
  return ret;
}

static void connect_observe(void *context, unsigned int kind,
                            uint64_t begin, uint64_t end, int result)
{
  (void)context;
  agent_audio_observe(voice_channel_request_id(), kind, begin, end, 0,
                      result < 0 ? (uint32_t)-result : 0);
}

#ifdef CONFIG_BK7258_TTS_ISOLATION
static void tls_observe(void *context, unsigned int kind, uint64_t begin,
                        uint64_t end, int result)
{
  (void)context;
  uint64_t request = voice_channel_request_id();
  if (request)
    {
      agent_audio_observe(request, kind, begin, end,
                          result > 0 ? (uint32_t)result : 0,
                          result < 0 ? (uint32_t)-result : 0);
    }
}
#endif

static int tencent_init(void)
{
  if (!g_tencent.configured || g_tencent.initialized)
    {
      return -ENOKEY;
    }

  struct bkvoice_tls_config_s config =
{
    .peer_address = g_tencent.trust.peer_address,
    .server_ca = &g_tencent.trust.ca,
    .server_auth_only = true,
    .trusted_time = bkvoice_config_trusted_time,
    .now_ms = bkvoice_config_now_ms,
    .clock_context = &g_tencent.trust,
    .connect_observe = connect_observe,
#ifdef CONFIG_BK7258_TTS_ISOLATION
    .observe = tls_observe,
#endif
  };

  int ret = bkvoice_tls_initialize(&g_tencent.tls, &config);
  if (!ret)
    {
      g_tencent.initialized = true;
    }

  return ret;
}

static void tencent_deinit(void)
{
  if (g_tencent.initialized)
    {
      (void)bkvoice_tls_ops()->close(&g_tencent.tls);
      (void)bkvoice_tls_uninitialize(&g_tencent.tls);
      g_tencent.initialized = false;
    }
}

static int tencent_prepare(void)
{
  if (!g_tencent.initialized || g_tencent.consumed)
    {
      return -ESTALE;
    }

  atomic_store(&g_tencent.canceled, false);
  int ret = bkvoice_tls_prepare_request(&g_tencent.tls);
  if (!ret)
    {
      ret = bkvoice_config_trusted_time(&g_tencent.trust);
    }

  return ret;
}

static int synthesize_text_stream(voice_tts_text_cb text_cb,
                                  void *text_context,
                                  voice_tts_chunk_cb pcm_cb,
                                  void *pcm_context)
{
  if (!text_cb || !pcm_cb || !g_tencent.initialized || g_tencent.consumed)
    {
      return -EINVAL;
    }

  g_tencent.consumed = true;
  struct tencent_session_s session =
{
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .tx_lock = PTHREAD_MUTEX_INITIALIZER,
    .changed = PTHREAD_COND_INITIALIZER,
    .pcm_cb = pcm_cb,
    .pcm_context = pcm_context,
    .request = voice_channel_request_id(),
    .deadline = now_ms() + TENCENT_TOTAL_MS,
  };

  int ret = resolve_peer();
  if (!ret)
    {
      ret = bkvoice_tls_ops()->open_verified(
          &g_tencent.tls, BKTENCENT_TTS_HOST, BKTENCENT_TTS_PORT,
          now_ms() + TENCENT_CONNECT_MS);
    }

  if (!ret)
    {
      ret = websocket_upgrade();
    }

  /* The signed query is single-use and no longer needed after upgrade. */

  mbedtls_platform_zeroize(g_tencent.path, sizeof(g_tencent.path));
  if (!ret)
    {
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      ret = pthread_attr_setstacksize(&attr, 24 * 1024);
      if (!ret)
        {
          ret = pthread_create(&session.receiver, &attr, receiver_run,
                               &session);
        }

      pthread_attr_destroy(&attr);
      if (ret)
        {
          ret = -ret;
        }
      else
        {
          session.receiver_started = true;
        }
    }

  if (!ret)
    {
      ret = wait_ready(&session);
    }

  unsigned int segments = 0;
  while (!ret)
    {
      char text[TENCENT_SEGMENT_MAX + 1u];
      size_t size = 0;
      int done = 0;
      ret = text_cb(text, sizeof(text), &size, &done, text_context);
      if (ret || done)
        {
          break;
        }

      if (!size || size > TENCENT_SEGMENT_MAX || text[size] != '\0')
        {
          ret = -EPROTO;
          break;
        }

      ret = send_action(&session, "ACTION_SYNTHESIS", text, size);
      if (!ret)
        {
          agent_trace_stage(session.request, "tencent_text_submit", size,
                            ++segments);
        }
    }

  if (!ret)
    {
      pthread_mutex_lock(&session.lock);
      session.complete_sent = true;
      pthread_cond_broadcast(&session.changed);
      pthread_mutex_unlock(&session.lock);
      ret = send_action(&session, "ACTION_COMPLETE", "", 0);
    }

  if (ret)
    {
      (void)send_action(&session, "ACTION_RESET", "", 0);
      atomic_store(&g_tencent.canceled, true);
      (void)bkvoice_tls_ops()->interrupt(&g_tencent.tls);
    }

  if (session.receiver_started)
    {
      int joined = pthread_join(session.receiver, NULL);
      if (!ret && joined)
        {
          ret = -joined;
        }
    }

  pthread_mutex_lock(&session.lock);
  if (!ret && session.error)
    {
      ret = session.error;
    }

  if (!ret && (!session.final || !session.first_pcm || !segments))
    {
      ret = -EPROTO;
    }

  pthread_mutex_unlock(&session.lock);
  if (!ret)
    {
      /* FINAL transfers closure ownership to the client. A failed close
       * frame is diagnostic only because all authenticated PCM is already
       * complete; the verified TLS channel is closed immediately below in
       * either case.
       */

      int ws_closed = ws_send_locked(&session, 0x8u, NULL, 0);
      agent_trace_stage(session.request, "tencent_ws_close", 0, ws_closed);
      pcm_cb(NULL, 0, 1, pcm_context);
    }

  int closed = bkvoice_tls_ops()->close(&g_tencent.tls);
  if (!ret && closed)
    {
      ret = closed;
    }

  pthread_cond_destroy(&session.changed);
  pthread_mutex_destroy(&session.tx_lock);
  pthread_mutex_destroy(&session.lock);
  agent_trace_stage(session.request, "tencent_stream_end",
                    session.pcm_bytes, ret);
  return ret;
}

struct single_text_s
{
  const char *text;
  bool sent;
};

static int single_text(char *text, size_t capacity, size_t *size,
                       int *is_last, void *context)
{
  struct single_text_s *source = context;
  if (source->sent)
    {
      *size = 0;
      *is_last = 1;
      return 0;
    }

  size_t length = strlen(source->text);
  if (!length || length >= capacity)
    {
      return -EOVERFLOW;
    }

  memcpy(text, source->text, length + 1u);
  *size = length;
  *is_last = 0;
  source->sent = true;
  return 0;
}

static int synthesize_stream(const char *text, voice_tts_chunk_cb callback,
                             void *context)
{
  struct single_text_s source =
  {
    .text = text
  };

  return synthesize_text_stream(single_text, &source, callback, context);
}

static int capabilities(voice_tts_capabilities_t *caps)
{
  if (!caps)
    {
      return -EINVAL;
    }

  *caps = (voice_tts_capabilities_t)
    {
      .location = VOICE_TTS_LOCATION_REMOTE,
      .needs_network = true,
      .sample_rate = 16000,
      .batch_sample_rate = 16000,
      .channels = 1,
      .bits = 16,
    };

  return 0;
}

static int tencent_cancel(void)
{
  atomic_store(&g_tencent.canceled, true);
  return g_tencent.initialized
             ? bkvoice_tls_ops()->interrupt(&g_tencent.tls)
             : 0;
}

static const voice_tts_ops_t g_tencent_ops =
{
  .name = "tencent-bktest",
  .max_streams = 1,
  .init = tencent_init,
  .prepare_request = tencent_prepare,
  .synthesize_stream = synthesize_stream,
  .synthesize_text_stream = synthesize_text_stream,
  .get_capabilities = capabilities,
  .cancel = tencent_cancel,
  .deinit = tencent_deinit,
};

int bkagent_tencent_tts_register(void)
{
  return voice_tts_register(&g_tencent_ops);
}

static int query_value(const char *path, const char *name, char *value,
                       size_t capacity)
{
  const char *query = strchr(path, '?');
  size_t name_size = strlen(name);
  if (!query)
    {
      return -EINVAL;
    }

  for (const char *field = query + 1; *field; )
    {
      const char *end = strchr(field, '&');
      if (!end)
        {
          end = field + strlen(field);
        }

      const char *equal = memchr(field, '=', (size_t)(end - field));
      if (equal && (size_t)(equal - field) == name_size &&
          !memcmp(field, name, name_size))
        {
          size_t size = (size_t)(end - equal - 1);
          if (!size || size >= capacity)
            {
              return -EOVERFLOW;
            }

          memcpy(value, equal + 1, size);
          value[size] = '\0';
          return 0;
        }

      field = *end ? end + 1 : end;
    }

  return -ENOENT;
}

static int signed_path_validate(const char *path, size_t size,
                                char session[129])
{
  if (!path || !size || size > BKTENCENT_TTS_PATH_MAX ||
      strnlen(path, size + 1u) != size ||
      strncmp(path, "/stream_wsv2?", 13))
    {
      return -EINVAL;
    }

  for (size_t i = 0; i < size; i++)
    {
      if ((unsigned char)path[i] < 0x21 || (unsigned char)path[i] > 0x7e ||
          path[i] == '#' || path[i] == '\\')
        {
          return -EINVAL;
        }
    }

  char value[129];
  int ret = query_value(path, "Action", value, sizeof(value));
  if (ret || strcmp(value, "TextToStreamAudioWSv2"))
    {
      return -EINVAL;
    }

  ret = query_value(path, "Codec", value, sizeof(value));
  if (ret || strcmp(value, "pcm"))
    {
      return -EINVAL;
    }

  ret = query_value(path, "SampleRate", value, sizeof(value));
  if (ret || strcmp(value, "16000"))
    {
      return -EINVAL;
    }

  ret = query_value(path, "Signature", value, sizeof(value));
  if (ret)
    {
      return -EINVAL;
    }

  ret = query_value(path, "SessionId", session, 129);
  if (ret)
    {
      return ret;
    }

  for (size_t i = 0; session[i]; i++)
    {
      if (!((session[i] >= 'a' && session[i] <= 'z') ||
            (session[i] >= 'A' && session[i] <= 'Z') ||
            (session[i] >= '0' && session[i] <= '9') || session[i] == '-'))
        {
          return -EINVAL;
        }
    }

  return 0;
}

static void configured_clear(void)
{
  tencent_deinit();
  bkvoice_config_clear(&g_tencent.trust);
  mbedtls_platform_zeroize(g_tencent.path, sizeof(g_tencent.path));
  mbedtls_platform_zeroize(g_tencent.session, sizeof(g_tencent.session));
  g_tencent.configured = false;
  g_tencent.consumed = false;
  atomic_store(&g_tencent.canceled, false);
}

int bkagent_tencent_tts_clear(void)
{
  if (voice_tts_is_busy())
    {
      return -EBUSY;
    }

  const char *active = voice_tts_get_backend();
  if (active && !strcmp(active, g_tencent_ops.name))
    {
      if (!g_tencent.previous_backend[0])
        {
          return -ENOKEY;
        }

      int ret = voice_tts_set_backend(g_tencent.previous_backend);
      if (ret)
        {
          return ret;
        }
    }

  configured_clear();
  mbedtls_platform_zeroize(g_tencent.previous_backend,
                           sizeof(g_tencent.previous_backend));
  return 0;
}

int bkagent_tencent_tts_configure(const void *trust, size_t trust_size,
                                  const char *signed_path, size_t path_size)
{
  if (voice_tts_is_busy())
    {
      return -EBUSY;
    }

  char session[129] =
  {
    0
  };

  int ret = signed_path_validate(signed_path, path_size, session);
  if (ret)
    {
      return ret;
    }

  if (g_tencent.configured)
    {
      ret = bkagent_tencent_tts_clear();
      if (ret)
        {
          return ret;
        }
    }

  struct bkvoice_config_s parsed =
  {
    0
  };

  ret = bkvoice_config_load(&parsed, trust, trust_size);
  if (!ret && (strcmp(parsed.host, BKTENCENT_TTS_HOST) ||
               parsed.port != BKTENCENT_TTS_PORT))
    {
      ret = -EINVAL;
    }

  if (ret)
    {
      bkvoice_config_clear(&parsed);
      return ret;
    }

  const char *previous = voice_tts_get_backend();
  if (!previous || (!strcmp(previous, g_tencent_ops.name)) ||
      strlen(previous) >= sizeof(g_tencent.previous_backend))
    {
      bkvoice_config_clear(&parsed);
      return -EPROTO;
    }

  g_tencent.trust = parsed;
  memset(&parsed, 0, sizeof(parsed));
  memcpy(g_tencent.path, signed_path, path_size);
  g_tencent.path[path_size] = '\0';
  memcpy(g_tencent.session, session, strlen(session) + 1u);
  strcpy(g_tencent.previous_backend, previous);
  g_tencent.configured = true;
  g_tencent.consumed = false;
  ret = voice_tts_set_backend(g_tencent_ops.name);
  if (ret)
    {
      configured_clear();
      (void)voice_tts_set_backend(previous);
      mbedtls_platform_zeroize(g_tencent.previous_backend,
                               sizeof(g_tencent.previous_backend));
    }

  return ret;
}
