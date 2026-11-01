/****************************************************************************
 * apps/bk7258/bk7258_aliyun_tts.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/* Experiment-only Alibaba Cloud Qwen continuous-text TTS provider. The API
 * key is delivered by an authenticated BKTEST operator, held only in RAM
 * and erased immediately after the verified WebSocket upgrade succeeds.
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

#include "bk7258_aliyun_tts.h"

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

#define ALIYUN_HEADER_MAX 4096u
#define ALIYUN_EVENT_MAX 4096u
#define ALIYUN_PCM_CHUNK 2048u
#define ALIYUN_SEGMENT_MAX 320u
#define ALIYUN_CONNECT_MS 15000u
#define ALIYUN_FIRST_PCM_MS 15000u
#define ALIYUN_IDLE_PCM_MS 10000u
#define ALIYUN_TEXT_WAIT_MS 20000u
#define ALIYUN_TOTAL_MS 120000u
#define ALIYUN_MODEL "qwen-audio-3.1-tts-flash"
#define ALIYUN_VOICE "longanhuan_v3.1"
#define ALIYUN_WORKSPACE "llm-d1hvy2bes12kocx6"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aliyun_session_s
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
  uint64_t ready_ms;
  uint64_t first_text_ms;
  size_t pcm_bytes;
  char task_id[37];
  int error;
  bool receiver_started;
  bool ready;
  bool finish_sent;
  bool final;
  bool first_pcm;
  bool active;
  bool claimed;
  bool early;
  bool run_task_sent;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct
{
  struct bkvoice_config_s trust;
  struct bkvoice_tls_s tls;
  char api_key[BKALIYUN_TTS_API_KEY_MAX + 1u];
  char previous_backend[32];
  atomic_bool canceled;
  bool configured;
  bool initialized;
  bool consumed;
  bool preconnect;
} g_aliyun;

/* One task and one receiver, used earlier when the experiment is enabled.
 * Persistent synchronization avoids copying/freeing a session during
 * cancel.
 */

static pthread_mutex_t g_session_owner = PTHREAD_MUTEX_INITIALIZER;
static struct aliyun_session_s g_session =
{
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .tx_lock = PTHREAD_MUTEX_INITIALIZER,
  .changed = PTHREAD_COND_INITIALIZER,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int connect_session(struct aliyun_session_s *session);
static int send_run_task(struct aliyun_session_s *session);
static int random_task_id(char id[37]);

static uint64_t now_ms(void)
{
  return bkvoice_config_now_ms(NULL);
}

static int canceled(void)
{
  return atomic_load(&g_aliyun.canceled) ? -ECANCELED : 0;
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
          ops->send(&g_aliyun.tls, data + offset, size - offset, deadline);
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
          ops->recv(&g_aliyun.tls, data + offset, size - offset, deadline);
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
  if (ops->random(&g_aliyun.tls, nonce, sizeof(nonce)) != 0 ||
      mbedtls_base64_encode(key, sizeof(key), &key_size, nonce,
                            sizeof(nonce)) != 0)
    {
      return -EIO;
    }

  char challenge[sizeof(key) + sizeof(guid)];
  memcpy(challenge, key, key_size);
  memcpy(challenge + key_size, guid, sizeof(guid) - 1u);
  if (ops->sha1(&g_aliyun.tls, (const uint8_t *)challenge,
                key_size + sizeof(guid) - 1u, digest) != 0 ||
      mbedtls_base64_encode(accept, sizeof(accept), &accept_size, digest,
                            sizeof(digest)) != 0)
    {
      return -EIO;
    }

  size_t request_size = strlen(g_aliyun.api_key) + 1024u;
  char *request = malloc(request_size);
  if (!request)
    {
      return -ENOMEM;
    }

  int count = snprintf(
      request, request_size,
      "GET " BKALIYUN_TTS_PATH " HTTP/1.1\r\n"
      "Host: " BKALIYUN_TTS_HOST "\r\n"
      "Authorization: Bearer %s\r\n"
      "X-DashScope-WorkSpace: " ALIYUN_WORKSPACE "\r\n"
      "User-Agent: shaniu-bk7258-r8\r\n"
      "Upgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: %.*s\r\nSec-WebSocket-Version: 13\r\n\r\n",
      g_aliyun.api_key, (int)key_size, key);
  int ret = count <= 0 || (size_t)count >= request_size
                ? -EOVERFLOW
                : send_all((const uint8_t *)request, (size_t)count,
                           now_ms() + ALIYUN_CONNECT_MS);
  mbedtls_platform_zeroize(request, request_size);
  free(request);
  if (ret)
    {
      return ret;
    }

  char response[ALIYUN_HEADER_MAX + 1u];
  size_t used = 0;
  do
    {
      if (used == ALIYUN_HEADER_MAX)
        {
          return -EOVERFLOW;
        }

      ret = recv_exact((uint8_t *)response + used, 1,
                       now_ms() + ALIYUN_CONNECT_MS);
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

  if (bkvoice_tls_ops()->random(&g_aliyun.tls, mask, sizeof(mask)) != 0)
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

static int ws_send_locked(struct aliyun_session_s *session, uint8_t opcode,
                          const uint8_t *data, size_t size)
{
  pthread_mutex_lock(&session->tx_lock);
  int ret = ws_send(opcode, data, size, session->deadline);
  pthread_mutex_unlock(&session->tx_lock);
  return ret;
}

static void session_fail(struct aliyun_session_s *session, int error)
{
  pthread_mutex_lock(&session->lock);
  if (!session->error)
    {
      session->error = error ? error : -EIO;
    }

  pthread_cond_broadcast(&session->changed);
  pthread_mutex_unlock(&session->lock);
}

static int result_type(cJSON *root)
{
  cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
  cJSON *output = cJSON_IsObject(payload)
                      ? cJSON_GetObjectItemCaseSensitive(payload, "output")
                      : NULL;
  cJSON *type = cJSON_IsObject(output)
                    ? cJSON_GetObjectItemCaseSensitive(output, "type")
                    : NULL;
  if (!cJSON_IsString(type))
    {
      return 0;
    }

  if (!strcmp(type->valuestring, "sentence-begin"))
    {
      return 1;
    }

  if (!strcmp(type->valuestring, "sentence-synthesis"))
    {
      return 2;
    }

  if (!strcmp(type->valuestring, "sentence-end"))
    {
      return 3;
    }

  return 0;
}

static int parse_event(struct aliyun_session_s *session,
                       const uint8_t *data, size_t size)
{
  if (!size || size > ALIYUN_EVENT_MAX || memchr(data, '\0', size))
    {
      return -EPROTO;
    }

  char text[ALIYUN_EVENT_MAX + 1u];
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

  cJSON *header = cJSON_GetObjectItemCaseSensitive(root, "header");
  cJSON *task = cJSON_IsObject(header)
                    ? cJSON_GetObjectItemCaseSensitive(header, "task_id")
                    : NULL;
  cJSON *event = cJSON_IsObject(header)
                     ? cJSON_GetObjectItemCaseSensitive(header, "event")
                     : NULL;
  int ret = !cJSON_IsString(task) || !cJSON_IsString(event) ||
                    strcmp(task->valuestring, session->task_id)
                ? -EPROTO
                : 0;
  if (!ret)
    {
      pthread_mutex_lock(&session->lock);
      if (!strcmp(event->valuestring, "task-started"))
        {
          session->ready = true;
          session->ready_ms = now_ms();
          agent_trace_stage(session->request, "aliyun_task_started", 1, 0);
        }
      else if (!strcmp(event->valuestring, "result-generated"))
        {
          agent_trace_stage(session->request, "aliyun_result",
                            result_type(root), 0);
        }
      else if (!strcmp(event->valuestring, "task-finished"))
        {
          if (!session->finish_sent)
            {
              ret = -EPROTO;
            }
          else
            {
              session->final = true;
            }

          agent_trace_stage(session->request, "aliyun_task_finished",
                            session->pcm_bytes, ret);
        }
      else if (!strcmp(event->valuestring, "task-failed"))
        {
          ret = -EREMOTEIO;
        }
      else
        {
          ret = -EPROTO;
        }

      pthread_cond_broadcast(&session->changed);
      pthread_mutex_unlock(&session->lock);
    }

  cJSON_Delete(root);
  return ret;
}

static uint64_t receive_deadline(struct aliyun_session_s *session)
{
  uint64_t now = now_ms();
  pthread_mutex_lock(&session->lock);
  uint64_t idle;
  if (session->ready && !session->first_text_ms)
    {
      idle = session->ready_ms + ALIYUN_TEXT_WAIT_MS;
    }
  else if (session->first_text_ms && !session->first_pcm)
    {
      idle = session->first_text_ms + ALIYUN_FIRST_PCM_MS;
    }
  else
    {
      idle = now + (session->first_pcm ? ALIYUN_IDLE_PCM_MS
                                       : ALIYUN_FIRST_PCM_MS);
    }

  pthread_mutex_unlock(&session->lock);
  return idle < session->deadline ? idle : session->deadline;
}

static int receive_payload(struct aliyun_session_s *session, uint8_t opcode,
                           uint64_t size, bool final)
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
      if (!final || size > ALIYUN_EVENT_MAX)
        {
          return -EPROTO;
        }

      uint8_t event[ALIYUN_EVENT_MAX];
      int ret = recv_exact(event, (size_t)size, receive_deadline(session));
      return ret ? ret : parse_event(session, event, (size_t)size);
    }

  if (opcode != 0x2u || !final || (size & 1u))
    {
      return -EPROTO;
    }

  pthread_mutex_lock(&session->lock);
  voice_tts_chunk_cb pcm_cb = session->pcm_cb;
  void *pcm_context = session->pcm_context;
  pthread_mutex_unlock(&session->lock);
  if (!pcm_cb)
    {
      return -EPROTO;
    }

  uint8_t pcm[ALIYUN_PCM_CHUNK];
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
          agent_trace_stage(session->request, "aliyun_first_pcm", count, 0);
        }
      else
        {
          agent_trace_stage(session->request, "aliyun_pcm_gap",
                            arrived - session->last_pcm_ms, 0);
        }

      session->last_pcm_ms = arrived;
      pcm_cb(pcm, count, 0, pcm_context);
      session->pcm_bytes += count;
      size -= count;
      if ((ret = canceled()) != 0)
        {
          return ret;
        }
    }

  return 0;
}

static int receive_frame(struct aliyun_session_s *session)
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
  struct aliyun_session_s *session = context;
  int ret = canceled();
  if (!ret)
    {
      ret = bkvoice_tls_prepare_request(&g_aliyun.tls);
    }

  if (!ret)
    {
      ret = bkvoice_config_trusted_time(&g_aliyun.trust);
    }

  if (!ret)
    {
      ret = random_task_id(session->task_id);
    }

  if (!ret)
    {
      ret = connect_session(session);
    }

  if (!ret)
    {
      /* A partial run-task send is ambiguous: never replay it. */

      pthread_mutex_lock(&session->lock);
      session->run_task_sent = true;
      pthread_mutex_unlock(&session->lock);
      agent_trace_stage(session->request, "aliyun_run_task_begin", 0, 0);
      ret = send_run_task(session);
      agent_trace_stage(session->request, "aliyun_run_task_end", 0, ret);
    }
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

static int wait_ready(struct aliyun_session_s *session)
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

static int random_task_id(char id[37])
{
  static const char hex[] = "0123456789abcdef";
  uint8_t random[16];
  if (bkvoice_tls_ops()->random(&g_aliyun.tls, random, sizeof(random)) != 0)
    {
      return -EIO;
    }

  random[6] = (random[6] & 0x0fu) | 0x40u;
  random[8] = (random[8] & 0x3fu) | 0x80u;
  size_t out = 0;
  for (size_t i = 0; i < sizeof(random); i++)
    {
      if (i == 4 || i == 6 || i == 8 || i == 10)
        {
          id[out++] = '-';
        }

      id[out++] = hex[random[i] >> 4];
      id[out++] = hex[random[i] & 15];
    }

  id[out] = '\0';
  return 0;
}

static int send_json(struct aliyun_session_s *session, cJSON *root)
{
  if (!root)
    {
      return -ENOMEM;
    }

  char *wire = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!wire)
    {
      return -ENOMEM;
    }

  size_t size = strlen(wire);
  int ret = size > UINT16_MAX ? -EOVERFLOW
                              : ws_send_locked(session, 0x1u,
                                               (const uint8_t *)wire, size);
  mbedtls_platform_zeroize(wire, size);
  free(wire);
  return ret;
}

static cJSON *event_root(struct aliyun_session_s *session,
                         const char *action, cJSON **payload)
{
  cJSON *root = cJSON_CreateObject();
  cJSON *header = root ? cJSON_AddObjectToObject(root, "header") : NULL;
  *payload = root ? cJSON_AddObjectToObject(root, "payload") : NULL;
  if (!root || !header || !*payload ||
      !cJSON_AddStringToObject(header, "action", action) ||
      !cJSON_AddStringToObject(header, "task_id", session->task_id) ||
      !cJSON_AddStringToObject(header, "streaming", "duplex"))
    {
      cJSON_Delete(root);
      return NULL;
    }

  return root;
}

static int send_run_task(struct aliyun_session_s *session)
{
  cJSON *payload = NULL;
  cJSON *root = event_root(session, "run-task", &payload);
  cJSON *parameters =
      payload ? cJSON_AddObjectToObject(payload, "parameters") : NULL;
  cJSON *input = payload ? cJSON_AddObjectToObject(payload, "input") : NULL;
  if (!root || !parameters || !input ||
      !cJSON_AddStringToObject(payload, "task_group", "audio") ||
      !cJSON_AddStringToObject(payload, "task", "tts") ||
      !cJSON_AddStringToObject(payload, "function", "SpeechSynthesizer") ||
      !cJSON_AddStringToObject(payload, "model", ALIYUN_MODEL) ||
      !cJSON_AddStringToObject(parameters, "text_type", "PlainText") ||
      !cJSON_AddStringToObject(parameters, "voice", ALIYUN_VOICE) ||
      !cJSON_AddStringToObject(parameters, "format", "pcm") ||
      !cJSON_AddNumberToObject(parameters, "sample_rate", 16000) ||
      !cJSON_AddNumberToObject(parameters, "volume", 50) ||
      !cJSON_AddNumberToObject(parameters, "rate", 1.0) ||
      !cJSON_AddNumberToObject(parameters, "pitch", 1.0) ||
      !cJSON_AddBoolToObject(parameters, "enable_ssml", false))
    {
      cJSON_Delete(root);
      return -ENOMEM;
    }

  return send_json(session, root);
}

static int send_text(struct aliyun_session_s *session, const char *text,
                     size_t size)
{
  if (!text || !size || strlen(text) != size)
    {
      return -EINVAL;
    }

  cJSON *payload = NULL;
  cJSON *root = event_root(session, "continue-task", &payload);
  cJSON *input = payload ? cJSON_AddObjectToObject(payload, "input") : NULL;
  if (!root || !input || !cJSON_AddStringToObject(input, "text", text))
    {
      cJSON_Delete(root);
      return -ENOMEM;
    }

  int ret = send_json(session, root);
  if (!ret)
    {
      pthread_mutex_lock(&session->lock);
      if (!session->first_text_ms)
        {
          session->first_text_ms = now_ms();
        }

      pthread_mutex_unlock(&session->lock);
    }

  return ret;
}

static int send_finish(struct aliyun_session_s *session, bool cancel)
{
  cJSON *payload = NULL;
  cJSON *root = event_root(session, "finish-task", &payload);
  cJSON *input = payload ? cJSON_AddObjectToObject(payload, "input") : NULL;
  if (!root || !input ||
      (cancel && !cJSON_AddStringToObject(input, "directive", "cancel")))
    {
      cJSON_Delete(root);
      return -ENOMEM;
    }

  return send_json(session, root);
}

static int resolve_peer(uint64_t request)
{
  struct addrinfo hints =
  {
    .ai_family = AF_INET,
    .ai_socktype = SOCK_STREAM
  };

  struct addrinfo *addresses = NULL;
  uint64_t begin = now_ms();
  int resolved = getaddrinfo(BKALIYUN_TTS_HOST, NULL, &hints, &addresses);
  int ret = resolved || !addresses ? -EHOSTUNREACH : 0;
  if (!ret)
    {
      g_aliyun.tls.config.peer_address =
          ((struct sockaddr_in *)addresses->ai_addr)->sin_addr;
    }

  if (addresses)
    {
      freeaddrinfo(addresses);
    }

  agent_trace_stage(request, "aliyun_dns", now_ms() - begin, ret);
  return ret;
}

static int connect_session(struct aliyun_session_s *session)
{
  agent_trace_stage(session->request, "aliyun_ws_begin", 0, 0);
  int ret = canceled();
  if (!ret)
    {
      ret = resolve_peer(session->request);
    }

  if (!ret)
    {
      ret = bkvoice_tls_ops()->open_verified(
          &g_aliyun.tls, BKALIYUN_TTS_HOST, BKALIYUN_TTS_PORT,
          now_ms() + ALIYUN_CONNECT_MS);
    }

  agent_trace_stage(session->request, "aliyun_tls_ready", 0, ret);
  if (!ret)
    {
      agent_trace_stage(session->request, "aliyun_ws_upgrade_begin", 0, 0);
      ret = websocket_upgrade();
      agent_trace_stage(session->request, "aliyun_ws_upgrade_end", 0, ret);
    }

  /* A failed speculative connection has not created a synthesis task.
   * Retain the RAM-only token for at most one on-demand connection
   * fallback.
   */

  if (!ret || canceled())
    {
      mbedtls_platform_zeroize(g_aliyun.api_key, sizeof(g_aliyun.api_key));
    }

  return ret;
}

/* Called with g_session_owner held. Network/crypto preparation stays in the
 * receiver, so the LLM caller only starts the existing worker.
 */

static int start_session_locked(uint64_t request, bool early)
{
  if (!g_aliyun.initialized || g_aliyun.consumed)
    {
      return -ESTALE;
    }

  g_aliyun.consumed = true;
  pthread_mutex_lock(&g_session.lock);
  g_session.pcm_cb = NULL;
  g_session.pcm_context = NULL;
  g_session.request = request;
  g_session.deadline = now_ms() + ALIYUN_TOTAL_MS;
  g_session.last_pcm_ms = 0;
  g_session.ready_ms = 0;
  g_session.first_text_ms = 0;
  g_session.pcm_bytes = 0;
  g_session.error = 0;
  g_session.receiver_started = false;
  g_session.ready = false;
  g_session.finish_sent = false;
  g_session.final = false;
  g_session.first_pcm = false;
  g_session.active = true;
  g_session.claimed = false;
  g_session.early = early;
  g_session.run_task_sent = false;
  pthread_mutex_unlock(&g_session.lock);
  int ret = canceled();
  agent_trace_stage(request, "aliyun_preconnect", g_aliyun.preconnect, ret);
  if (!ret)
    {
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      ret = pthread_attr_setstacksize(&attr, 24 * 1024);
      if (!ret)
        {
          ret = pthread_create(&g_session.receiver, &attr, receiver_run,
                               &g_session);
        }

      pthread_attr_destroy(&attr);
      if (ret)
        {
          ret = -ret;
        }
      else
        {
          g_session.receiver_started = true;
        }
    }

  if (ret)
    {
      session_fail(&g_session, ret);
    }

  return ret;
}

static void stop_session_locked(void)
{
  if (!g_session.active)
    {
      return;
    }

  atomic_store(&g_aliyun.canceled, true);
  if (g_aliyun.initialized)
    {
      (void)bkvoice_tls_ops()->interrupt(&g_aliyun.tls);
    }

  if (g_session.receiver_started)
    {
      (void)pthread_join(g_session.receiver, NULL);
    }

  pthread_mutex_lock(&g_session.lock);
  g_session.active = false;
  pthread_mutex_unlock(&g_session.lock);
  agent_trace_stage(g_session.request, "aliyun_prewarm_closed", 0,
                    g_session.error);
}

int bkagent_aliyun_tts_preconnect(uint64_t request)
{
  if (!request || !g_aliyun.preconnect)
    {
      return 0;
    }

  const char *backend = voice_tts_get_backend();
  if (!backend || strcmp(backend, "aliyun-bktest"))
    {
      return 0;
    }

  /* A speculative optimization must never wait behind teardown. */

  int locked = pthread_mutex_trylock(&g_session_owner);
  if (locked)
    {
      return locked == EBUSY ? 0 : -locked;
    }

  int ret = g_session.active ? (g_session.request == request ? 0 : -ESTALE)
                             : start_session_locked(request, true);
  pthread_mutex_unlock(&g_session_owner);
  return ret;
}

void bkagent_aliyun_tts_preconnect_cancel(uint64_t request)
{
  pthread_mutex_lock(&g_session.lock);
  bool active = g_session.active && g_session.request == request;
  pthread_mutex_unlock(&g_session.lock);
  if (active)
    {
      atomic_store(&g_aliyun.canceled, true);
      (void)bkvoice_tls_ops()->interrupt(&g_aliyun.tls);
    }
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

static int aliyun_init(void)
{
  if (!g_aliyun.configured || g_aliyun.initialized)
    {
      return -ENOKEY;
    }

  struct bkvoice_tls_config_s config =
{
    .peer_address = g_aliyun.trust.peer_address,
    .server_ca = &g_aliyun.trust.ca,
    .server_auth_only = true,
    .trusted_time = bkvoice_config_trusted_time,
    .now_ms = bkvoice_config_now_ms,
    .clock_context = &g_aliyun.trust,
    .connect_observe = connect_observe,
#ifdef CONFIG_BK7258_TTS_ISOLATION
    .observe = tls_observe,
#endif
  };

  int ret = bkvoice_tls_initialize(&g_aliyun.tls, &config);
  if (!ret)
    {
      g_aliyun.initialized = true;
    }

  return ret;
}

static void aliyun_deinit(void)
{
  pthread_mutex_lock(&g_session_owner);
  stop_session_locked();
  if (g_aliyun.initialized)
    {
      (void)bkvoice_tls_ops()->close(&g_aliyun.tls);
      (void)bkvoice_tls_uninitialize(&g_aliyun.tls);
      g_aliyun.initialized = false;
    }

  pthread_mutex_unlock(&g_session_owner);
}

static int aliyun_prepare(void)
{
  pthread_mutex_lock(&g_session_owner);
  int ret =
      !g_aliyun.initialized || (g_aliyun.consumed && !g_session.active)
          ? -ESTALE
          : canceled();
  pthread_mutex_unlock(&g_session_owner);
  return ret;
}

static int synthesize_text_stream(voice_tts_text_cb text_cb,
                                  void *text_context,
                                  voice_tts_chunk_cb pcm_cb,
                                  void *pcm_context)
{
  if (!text_cb || !pcm_cb || !g_aliyun.initialized)
    {
      return -EINVAL;
    }

  uint64_t request = voice_channel_request_id();
  pthread_mutex_lock(&g_session_owner);
  int ret = g_session.active ? 0 : start_session_locked(request, false);
  struct aliyun_session_s *session = &g_session;
  pthread_mutex_lock(&session->lock);
  if (!ret && (session->request != request || session->claimed))
    {
      ret = -ESTALE;
    }

  if (!ret)
    {
      session->claimed = true;
      session->pcm_cb = pcm_cb;
      session->pcm_context = pcm_context;
    }

  pthread_mutex_unlock(&session->lock);
  pthread_mutex_unlock(&g_session_owner);
  agent_trace_stage(request, "aliyun_session_claim", 0, ret);
  if (!ret)
    {
      ret = wait_ready(session);
    }

  /* Fall back once only if no run-task was even attempted. The old receiver
   * is joined and its socket closed first; no text or synthesis is
   * replayed. An in-progress handshake already follows the normal bounded
   * ready wait.
   */

  pthread_mutex_lock(&g_session_owner);
  pthread_mutex_lock(&session->lock);
  bool fallback = ret && !canceled() && session->early &&
                  session->request == request && !session->run_task_sent &&
                  g_aliyun.api_key[0];
  pthread_mutex_unlock(&session->lock);
  if (fallback)
    {
      if (session->receiver_started)
        {
          (void)pthread_join(session->receiver, NULL);
        }

      (void)bkvoice_tls_ops()->close(&g_aliyun.tls);
      pthread_mutex_lock(&session->lock);
      session->active = false;
      pthread_mutex_unlock(&session->lock);
      g_aliyun.consumed = false;
      agent_trace_stage(request, "aliyun_connect_fallback", 1, ret);
      ret = start_session_locked(request, false);
      pthread_mutex_lock(&session->lock);
      session->claimed = true;
      session->pcm_cb = pcm_cb;
      session->pcm_context = pcm_context;
      pthread_mutex_unlock(&session->lock);
    }

  pthread_mutex_unlock(&g_session_owner);
  if (fallback && !ret)
    {
      ret = wait_ready(session);
    }

  unsigned int segments = 0;
  while (!ret)
    {
      char text[ALIYUN_SEGMENT_MAX + 1u];
      size_t size = 0;
      int done = 0;
      ret = text_cb(text, sizeof(text), &size, &done, text_context);
      if (ret || done)
        {
          break;
        }

      if (!size || size > ALIYUN_SEGMENT_MAX || text[size] != '\0')
        {
          ret = -EPROTO;
          break;
        }

      ret = send_text(session, text, size);
      if (!ret)
        {
          agent_trace_stage(session->request, "aliyun_text_submit", size,
                            ++segments);
        }
    }

  if (!ret)
    {
      pthread_mutex_lock(&session->lock);
      session->finish_sent = true;
      pthread_cond_broadcast(&session->changed);
      pthread_mutex_unlock(&session->lock);
      ret = send_finish(session, false);
    }

  if (ret)
    {
      pthread_mutex_lock(&session->lock);
      bool ready = session->ready;
      pthread_mutex_unlock(&session->lock);
      if (ready)
        {
          (void)send_finish(session, true);
        }

      atomic_store(&g_aliyun.canceled, true);
      (void)bkvoice_tls_ops()->interrupt(&g_aliyun.tls);
    }

  pthread_mutex_lock(&g_session_owner);
  if (session->receiver_started)
    {
      int joined = pthread_join(session->receiver, NULL);
      if (!ret && joined)
        {
          ret = -joined;
        }
    }

  pthread_mutex_lock(&session->lock);
  if (!ret && session->error)
    {
      ret = session->error;
    }

  if (!ret && (!session->final || !session->first_pcm || !segments))
    {
      ret = -EPROTO;
    }

  pthread_mutex_unlock(&session->lock);
  if (!ret)
    {
      int ws_closed = ws_send_locked(session, 0x8u, NULL, 0);
      agent_trace_stage(session->request, "aliyun_ws_close", 0, ws_closed);
      pcm_cb(NULL, 0, 1, pcm_context);
    }

  int closed = bkvoice_tls_ops()->close(&g_aliyun.tls);
  mbedtls_platform_zeroize(g_aliyun.api_key, sizeof(g_aliyun.api_key));
  if (!ret && closed)
    {
      ret = closed;
    }

  pthread_mutex_lock(&session->lock);
  session->active = false;
  pthread_mutex_unlock(&session->lock);
  pthread_mutex_unlock(&g_session_owner);
  agent_trace_stage(session->request, "aliyun_stream_end",
                    session->pcm_bytes, ret);
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

static int aliyun_cancel(void)
{
  atomic_store(&g_aliyun.canceled, true);
  return g_aliyun.initialized ? bkvoice_tls_ops()->interrupt(&g_aliyun.tls)
                              : 0;
}

static const voice_tts_ops_t g_aliyun_ops =
{
  .name = "aliyun-bktest",
  .max_streams = 1,
  .init = aliyun_init,
  .prepare_request = aliyun_prepare,
  .synthesize_stream = synthesize_stream,
  .synthesize_text_stream = synthesize_text_stream,
  .get_capabilities = capabilities,
  .cancel = aliyun_cancel,
  .deinit = aliyun_deinit,
};

int bkagent_aliyun_tts_register(void)
{
  return voice_tts_register(&g_aliyun_ops);
}

static void configured_clear(void)
{
  aliyun_deinit();
  bkvoice_config_clear(&g_aliyun.trust);
  mbedtls_platform_zeroize(g_aliyun.api_key, sizeof(g_aliyun.api_key));
  g_aliyun.configured = false;
  g_aliyun.consumed = false;
  g_aliyun.preconnect = false;
  atomic_store(&g_aliyun.canceled, false);
}

int bkagent_aliyun_tts_clear(void)
{
  if (voice_tts_is_busy())
    {
      return -EBUSY;
    }

  const char *active = voice_tts_get_backend();
  if (active && !strcmp(active, g_aliyun_ops.name))
    {
      if (!g_aliyun.previous_backend[0])
        {
          return -ENOKEY;
        }

      int ret = voice_tts_set_backend(g_aliyun.previous_backend);
      if (ret)
        {
          return ret;
        }
    }

  configured_clear();
  mbedtls_platform_zeroize(g_aliyun.previous_backend,
                           sizeof(g_aliyun.previous_backend));
  return 0;
}

int bkagent_aliyun_tts_configure(const void *trust, size_t trust_size,
                                 const char *api_key, size_t key_size,
                                 bool preconnect)
{
  if (voice_tts_is_busy())
    {
      return -EBUSY;
    }

  if (!api_key || key_size < 8 || key_size > BKALIYUN_TTS_API_KEY_MAX ||
      strnlen(api_key, key_size + 1u) != key_size)
    {
      return -EINVAL;
    }

  for (size_t i = 0; i < key_size; i++)
    {
      if ((unsigned char)api_key[i] < 0x21 ||
          (unsigned char)api_key[i] > 0x7e)
        {
          return -EINVAL;
        }
    }

  int ret = 0;
  if (g_aliyun.configured)
    {
      ret = bkagent_aliyun_tts_clear();
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
  if (!ret && (strcmp(parsed.host, BKALIYUN_TTS_HOST) ||
               parsed.port != BKALIYUN_TTS_PORT))
    {
      ret = -EINVAL;
    }

  if (ret)
    {
      bkvoice_config_clear(&parsed);
      return ret;
    }

  const char *previous = voice_tts_get_backend();
  if (!previous || (!strcmp(previous, g_aliyun_ops.name)) ||
      strlen(previous) >= sizeof(g_aliyun.previous_backend))
    {
      bkvoice_config_clear(&parsed);
      return -EPROTO;
    }

  g_aliyun.trust = parsed;
  memset(&parsed, 0, sizeof(parsed));
  memcpy(g_aliyun.api_key, api_key, key_size);
  g_aliyun.api_key[key_size] = '\0';
  strcpy(g_aliyun.previous_backend, previous);
  g_aliyun.configured = true;
  g_aliyun.consumed = false;
  g_aliyun.preconnect = preconnect;
  ret = voice_tts_set_backend(g_aliyun_ops.name);
  if (ret)
    {
      configured_clear();
      (void)voice_tts_set_backend(previous);
      mbedtls_platform_zeroize(g_aliyun.previous_backend,
                               sizeof(g_aliyun.previous_backend));
    }

  return ret;
}
