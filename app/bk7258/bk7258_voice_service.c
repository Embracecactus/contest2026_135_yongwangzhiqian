/****************************************************************************
 * app/bk7258/bk7258_voice_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-resident authorized-voice service.  RPMsg receive callbacks only copy
 * and validate requests; file I/O and audio streaming run in a worker task.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_VOICE_SERVICE

#include "bk7258_product_lifecycle.h"
#include "bk7258_voice_pack.h"
#include "bk7258_voice_product.h"
#include "bk7258_voice_protocol.h"
#ifdef CONFIG_BK7258_PREFERENCES
#  include "bk7258_preferences.h"
#endif
#include "bk7258_voice_ptt.h"
#include "bk7258_voice_turn_audio.h"
#ifdef CONFIG_BK7258_VOICE_TLS
#  include "bk7258_voice_runtime.h"
#endif
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
#  include "bk7258_voice_feedback.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <media_player.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <arch/chip/bk7258_amp.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/clock.h>

#define BKVOICE_STREAM_BYTES 2048u
#define BKVOICE_PLAYER_OPTIONS \
  "format=s16le:sample_rate=16000:ch_layout=mono"
#ifdef CONFIG_BK7258_VOICE_TLS
#  define BKVOICE_TRANSPORT_STATUS "tls-wss"
#else
#  define BKVOICE_TRANSPORT_STATUS "not-installed"
#endif
struct bkvoice_service_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  spinlock_t request_lock;
  sem_t request_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  volatile uint32_t endpoint_generation;
  bool active;
  bool replay_valid;
  uint32_t active_generation;
  struct bkvoice_rpc_request_s active_request;
  struct bkvoice_rpc_request_s last_request;
  struct bkvoice_rpc_response_s last_response;
  struct bkvoice_turn_audio_s turn_audio;
  struct bkvoice_ptt_s ptt;
};

static struct bkvoice_service_s g_bkvoice_service =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = SP_UNLOCKED,
};

#ifndef CONFIG_MEDIA
extern void bk7258_agent_media_player_link(void);
extern void bk7258_agent_media_recorder_link(void);
#endif

static int bkvoice_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static void bkvoice_advance_generation(struct bkvoice_service_s *service)
{
  uint32_t generation = __atomic_add_fetch(&service->endpoint_generation,
                                           1u, __ATOMIC_ACQ_REL);

  if (generation == 0)
    {
      generation = 1;
    }

  __atomic_store_n(&service->endpoint_generation, generation,
                   __ATOMIC_RELEASE);
}

static void bkvoice_first_error(int *first, int ret)
{
  if (*first == 0 && ret < 0)
    {
      *first = ret;
    }
}

static int bkvoice_read_exact(int fd, void *buffer, size_t size)
{
  uint8_t *cursor = buffer;
  size_t done = 0;

  while (done < size)
    {
      ssize_t nread = read(fd, cursor + done, size - done);

      if (nread < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return bkvoice_errno();
        }

      if (nread == 0)
        {
          return -ENODATA;
        }

      done += (size_t)nread;
    }

  return OK;
}

static int bkvoice_write_player(void *player, const uint8_t *buffer,
                                size_t size)
{
  size_t written = 0;

  while (written < size)
    {
      ssize_t ret = media_player_write_data(player, buffer + written,
                                            size - written);

      if (ret < 0)
        {
          return (int)ret;
        }

      if (ret == 0)
        {
          return -EIO;
        }

      written += (size_t)ret;
    }

  return OK;
}

static bool bkvoice_connection_valid(struct bkvoice_service_s *service,
                                     uint32_t generation)
{
  return __atomic_load_n(&service->endpoint_created, __ATOMIC_ACQUIRE) &&
         __atomic_load_n(&service->endpoint_generation, __ATOMIC_ACQUIRE) ==
         generation;
}

static int bkvoice_play_wav(struct bkvoice_service_s *service,
                            uint32_t generation, const char *path,
                            struct bkvoice_wav_info_s *wav)
{
  uint8_t *buffer = NULL;
  uint32_t remaining;
  void *player = NULL;
  bool prepared = false;
#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  bool feedback_active = false;
#endif
  int fd = -1;
  int first = 0;
  int ret;

  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    {
      return bkvoice_errno();
    }

  ret = bkvoice_wav_parse(fd, wav);
  if (ret < 0)
    {
      first = ret;
      goto out;
    }

  buffer = malloc(BKVOICE_STREAM_BYTES);
  if (buffer == NULL)
    {
      first = -ENOMEM;
      goto out;
    }

  player = media_player_open(MEDIA_STREAM_MUSIC);
  if (player == NULL)
    {
      first = bkvoice_errno();
      goto out;
    }

  ret = media_player_prepare(player, NULL, BKVOICE_PLAYER_OPTIONS);
  if (ret < 0)
    {
      first = ret;
      goto out;
    }

  prepared = true;
  ret = media_player_start(player);
  if (ret < 0)
    {
      first = ret;
      goto out;
    }

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  bk7258_voice_feedback_report(NULL, BKVOICE_TURN_PLAYING);
  feedback_active = true;
#endif

  remaining = wav->data_bytes;
  while (remaining > 0)
    {
      size_t chunk = remaining < BKVOICE_STREAM_BYTES ?
                     remaining : BKVOICE_STREAM_BYTES;

      if (!bkvoice_connection_valid(service, generation))
        {
          first = -ECANCELED;
          break;
        }

      ret = bkvoice_read_exact(fd, buffer, chunk);
      if (ret < 0)
        {
          first = ret;
          break;
        }

      ret = bkvoice_write_player(player, buffer, chunk);
      if (ret < 0)
        {
          first = ret;
          break;
        }

      remaining -= (uint32_t)chunk;
    }

out:
  if (player != NULL && prepared)
    {
      ret = media_player_stop(player);
      bkvoice_first_error(&first, ret);
    }

  if (player != NULL)
    {
      ret = media_player_close(player, 0);
      bkvoice_first_error(&first, ret);
    }

  free(buffer);
  if (fd >= 0 && close(fd) < 0)
    {
      bkvoice_first_error(&first, bkvoice_errno());
    }

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  if (feedback_active)
    {
      bk7258_voice_feedback_report(NULL, BKVOICE_TURN_IDLE);
    }
#endif

  return first;
}

static void bkvoice_make_response(
  struct bkvoice_rpc_response_s *response,
  const struct bkvoice_rpc_request_s *request, int status)
{
  memset(response, 0, sizeof(*response));
  response->magic = BKVOICE_RPC_MAGIC;
  response->version = BKVOICE_RPC_VERSION;
  response->command = BKVOICE_RPC_RESPONSE;
  response->session = request->session;
  response->sequence = request->sequence;
  response->status = status;
}

static int bkvoice_send(struct bkvoice_service_s *service,
                        const struct bkvoice_rpc_response_s *response)
{
  int ret = nxmutex_lock(&service->endpoint_lock);

  if (ret < 0)
    {
      return ret;
    }

  if (!__atomic_load_n(&service->endpoint_created, __ATOMIC_ACQUIRE) ||
      !is_rpmsg_ept_ready(&service->endpoint))
    {
      ret = -ENOTCONN;
    }
  else
    {
      ret = rpmsg_trysend(&service->endpoint, response,
                          sizeof(*response));
    }

  nxmutex_unlock(&service->endpoint_lock);
  return ret;
}

#ifdef CONFIG_BK7258_PREFERENCES
static int bkvoice_preferences_request(
  const struct bkvoice_rpc_request_s *request,
  struct bkvoice_rpc_response_s *response)
{
  struct bk7258_preferences_s preferences;
  unsigned int volume = 0;
  const char *value = request->manifest;
  int ret = OK;

  if (request->command == BKVOICE_RPC_PREFS_VOLUME)
    {
      /* Validate again at the AP boundary; the CP shell is not the owner. */

      if (*value == '\0' || strlen(value) > 3)
        {
          return -EINVAL;
        }

      for (; *value != '\0'; value++)
        {
          if (*value < '0' || *value > '9')
            {
              return -EINVAL;
            }

          volume = volume * 10u + (unsigned int)(*value - '0');
        }

      ret = bk7258_preferences_set_volume(volume);
    }
  else if (request->command == BKVOICE_RPC_PREFS_PERSONA)
    {
      ret = bk7258_preferences_set_persona(value);
    }

  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_preferences_get(&preferences);
  if (ret < 0)
    {
      return ret;
    }

  response->result.preferences.volume_percent = preferences.volume_percent;
  response->result.preferences.persona = preferences.persona;
  response->result.preferences.default_flags =
    (preferences.volume_is_default ? BKVOICE_PREFS_DEFAULT_VOLUME : 0) |
    (preferences.persona_is_default ? BKVOICE_PREFS_DEFAULT_PERSONA : 0);
  return OK;
}
#endif

static int bkvoice_handle_request(
  struct bkvoice_service_s *service, uint32_t generation,
  const struct bkvoice_rpc_request_s *request,
  struct bkvoice_rpc_response_s *response)
{
  struct bkvoice_pack_info_s pack;
  struct bkvoice_wav_info_s wav;
  struct stat status;
  int ret = OK;

  bkvoice_make_response(response, request, OK);
  response->flags = BKVOICE_STATUS_SERVICE_READY |
                    BKVOICE_STATUS_LOCAL_ONLY |
                    BKVOICE_STATUS_PTT_OWNER_READY;
  if (stat("/dev/mmcsd0", &status) == 0)
    {
      response->flags |= BKVOICE_STATUS_BLOCK_PRESENT;
    }

  if (request->command == BKVOICE_RPC_STATUS)
    {
#ifdef CONFIG_BK7258_VOICE_TLS
      bkvoice_runtime_status(response);
#endif
      return OK;
    }

#ifdef CONFIG_BK7258_VOICE_HIL_TEST
  if (request->command == BKVOICE_RPC_HIL_CAPTURE)
    {
      response->status = bkvoice_runtime_command(request, response);
      return response->status;
    }
#endif

  if (request->command >= BKVOICE_RPC_CONFIG_BEGIN &&
      request->command <= BKVOICE_RPC_DISCONNECT)
    {
#ifdef CONFIG_BK7258_VOICE_TLS
      response->status = bkvoice_runtime_command(request, response);
#else
      response->status = -ENOSYS;
#endif
      return response->status;
    }

#ifdef CONFIG_BK7258_VOICE_TLS
  if ((request->command >= BKVOICE_RPC_PREFS_GET &&
       request->command <= BKVOICE_RPC_PREFS_PERSONA) ?
      bkvoice_runtime_settings_busy() : bkvoice_runtime_busy())
    {
      response->status = -EBUSY;
      return response->status;
    }
#endif

  if (request->command >= BKVOICE_RPC_PREFS_GET &&
      request->command <= BKVOICE_RPC_PREFS_PERSONA)
    {
#ifdef CONFIG_BK7258_PREFERENCES
      response->status = bkvoice_preferences_request(request, response);
#else
      response->status = -ENOSYS;
#endif
      return response->status;
    }

  memset(&pack, 0, sizeof(pack));
  ret = bkvoice_pack_load(request->manifest,
                          request->command == BKVOICE_RPC_PLAY ?
                          request->clip_id : NULL,
                          &pack);
  response->result.pack.version = pack.version;
  response->result.pack.clip_count = pack.clip_count;
  response->result.pack.error_line = pack.error_line;
  memcpy(response->speaker_id, pack.speaker_id,
         sizeof(response->speaker_id));
  response->speaker_id[sizeof(response->speaker_id) - 1u] = '\0';
  if (ret < 0 || request->command == BKVOICE_RPC_VERIFY)
    {
      response->status = ret;
      return ret;
    }

  syslog(LOG_NOTICE,
         "BKVOICE SYNTHETIC speaker_id=%s clip=%s\n",
         pack.speaker_id, request->clip_id);
  memset(&wav, 0, sizeof(wav));
  ret = bkvoice_play_wav(service, generation, pack.clip_path, &wav);
  response->status = ret;
  response->data_bytes = wav.data_bytes;
  if (ret >= 0)
    {
      response->duration_ms =
        (uint32_t)((uint64_t)wav.data_bytes * 1000u /
                   (BKVOICE_SAMPLE_RATE *
                    (BKVOICE_BITS_PER_SAMPLE / 8u)));
    }

  return ret;
}

static int bkvoice_worker(int argc, char **argv)
{
  struct bkvoice_service_s *service = &g_bkvoice_service;

  (void)argc;
  (void)argv;

  for (; ; )
    {
      struct bkvoice_rpc_request_s request;
      struct bkvoice_rpc_response_s response;
      uint32_t generation;
      irqstate_t flags;
      bool active;

      (void)nxsem_tickwait_uninterruptible(&service->request_sem,
                                            MSEC2TICK(20));

#ifdef CONFIG_BK7258_VOICE_TLS
      bkvoice_runtime_step(__atomic_load_n(&service->endpoint_created,
                                           __ATOMIC_ACQUIRE));
#endif

#ifndef CONFIG_BK7258_VOICE_TLS
      if (bkvoice_turn_poll(&service->ptt.turn) < 0)
        {
          syslog(LOG_WARNING, "BKVOICE playback completion failed\n");
        }
#endif

      flags = spin_lock_irqsave(&service->request_lock);
      active = service->active;
      memcpy(&request, &service->active_request, sizeof(request));
      generation = service->active_generation;
      spin_unlock_irqrestore(&service->request_lock, flags);

      if (!active)
        {
          continue;
        }

      if (bkvoice_connection_valid(service, generation))
        {
          (void)bkvoice_handle_request(service, generation, &request,
                                        &response);
        }
      else
        {
          bkvoice_make_response(&response, &request, -ENOTCONN);
        }

      flags = spin_lock_irqsave(&service->request_lock);
      /* Secret upload chunks are retried by bounded offset/readback in the
       * uploader, never retained in the general RPC replay cache.
       */

      service->replay_valid =
        request.command != BKVOICE_RPC_CONFIG_DATA &&
        bkvoice_connection_valid(service, generation);
      if (service->replay_valid)
        {
          memcpy(&service->last_request, &request, sizeof(request));
          memcpy(&service->last_response, &response, sizeof(response));
        }

      memset(&service->active_request, 0, sizeof(service->active_request));
      service->active = false;
      spin_unlock_irqrestore(&service->request_lock, flags);

      /* Never wait for a TX buffer.  A lost response is replayed when the
       * client retries the same session/sequence request.
       */

      if (bkvoice_connection_valid(service, generation))
        {
          (void)bkvoice_send(service, &response);
        }

      {
        volatile unsigned char *secret = (void *)&request;
        for (size_t i = 0; i < sizeof(request); i++)
          {
            secret[i] = 0;
          }
      }
    }

  return OK;
}

static bool bkvoice_request_valid(
  const struct bkvoice_rpc_request_s *request)
{
  bool manifest_terminated;
  bool clip_terminated;

  manifest_terminated = memchr(request->manifest, '\0',
                               sizeof(request->manifest)) != NULL;
  clip_terminated = memchr(request->clip_id, '\0',
                           sizeof(request->clip_id)) != NULL;
  if (!manifest_terminated || !clip_terminated || request->session == 0 ||
      request->sequence == 0)
    {
      return false;
    }

  if (request->command == BKVOICE_RPC_STATUS ||
      request->command == BKVOICE_RPC_PREFS_GET)
    {
      return request->manifest[0] == '\0' && request->clip_id[0] == '\0';
    }

#ifdef CONFIG_BK7258_VOICE_HIL_TEST
  if (request->command == BKVOICE_RPC_HIL_CAPTURE)
    {
      return request->manifest[0] != '\0' && request->clip_id[0] == '\0';
    }
#endif

  if (request->command == BKVOICE_RPC_CONFIG_BEGIN ||
      request->command == BKVOICE_RPC_PREFS_VOLUME ||
      request->command == BKVOICE_RPC_PREFS_PERSONA)
    {
      return request->manifest[0] != '\0' && request->clip_id[0] == '\0';
    }

  if (request->command == BKVOICE_RPC_CONFIG_DATA)
    {
      return request->manifest[0] != '\0' && request->clip_id[0] != '\0';
    }

  if (request->command >= BKVOICE_RPC_CONFIG_COMMIT &&
      request->command <= BKVOICE_RPC_DISCONNECT)
    {
      return request->manifest[0] == '\0' && request->clip_id[0] == '\0';
    }

  if (request->command == BKVOICE_RPC_VERIFY)
    {
      return request->manifest[0] != '\0' && request->clip_id[0] == '\0';
    }

  return request->command == BKVOICE_RPC_PLAY &&
         request->manifest[0] != '\0' && request->clip_id[0] != '\0';
}

static int bkvoice_service_cb(struct rpmsg_endpoint *endpoint, void *data,
                              size_t len, uint32_t src, void *priv)
{
  struct bkvoice_service_s *service = priv;
  const struct bkvoice_rpc_request_s *request = data;
  struct bkvoice_rpc_response_s response;
  irqstate_t flags;
  bool replay = false;
  bool duplicate = false;

  (void)endpoint;
  (void)src;

  if (request == NULL || len != sizeof(*request) ||
      request->magic != BKVOICE_RPC_MAGIC ||
      request->version != BKVOICE_RPC_VERSION)
    {
      return -EINVAL;
    }

  if (!bkvoice_request_valid(request))
    {
      bkvoice_make_response(&response, request, -EINVAL);
      return bkvoice_send(service, &response);
    }

  flags = spin_lock_irqsave(&service->request_lock);
  if (service->replay_valid &&
      request->session == service->last_request.session &&
      request->sequence == service->last_request.sequence)
    {
      if (memcmp(request, &service->last_request, sizeof(*request)) != 0)
        {
          spin_unlock_irqrestore(&service->request_lock, flags);
          bkvoice_make_response(&response, request, -EPROTO);
          return bkvoice_send(service, &response);
        }

      memcpy(&response, &service->last_response, sizeof(response));
      replay = true;
    }
  else if (service->active)
    {
      duplicate = memcmp(request, &service->active_request,
                         sizeof(*request)) == 0;
      spin_unlock_irqrestore(&service->request_lock, flags);
      if (duplicate)
        {
          return OK;
        }

      bkvoice_make_response(&response, request, -EBUSY);
      return bkvoice_send(service, &response);
    }
  else
    {
      memcpy(&service->active_request, request, sizeof(*request));
      service->active_generation = __atomic_load_n(
        &service->endpoint_generation, __ATOMIC_ACQUIRE);
      service->active = true;
    }

  spin_unlock_irqrestore(&service->request_lock, flags);
  if (replay)
    {
      return bkvoice_send(service, &response);
    }

  return nxsem_post(&service->request_sem);
}

static bool bkvoice_ns_match(struct rpmsg_device *rdev, void *priv,
                             const char *name, uint32_t dest)
{
  const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv;
  (void)dest;
  return cpuname != NULL && strcmp(cpuname, "cp") == 0 &&
         strcmp(name, BKVOICE_RPC_ENDPOINT) == 0;
}

static void bkvoice_service_unbind(struct rpmsg_endpoint *endpoint)
{
  struct bkvoice_service_s *service = endpoint->priv;
  irqstate_t flags;

  if (service == NULL)
    {
      return;
    }

  __atomic_store_n(&service->endpoint_created, false, __ATOMIC_RELEASE);
  bkvoice_advance_generation(service);
  flags = spin_lock_irqsave(&service->request_lock);
  service->replay_valid = false;
  spin_unlock_irqrestore(&service->request_lock, flags);
  (void)nxsem_post(&service->request_sem);
}

static void bkvoice_ns_bind(struct rpmsg_device *rdev, void *priv,
                            const char *name, uint32_t dest)
{
  struct bkvoice_service_s *service = priv;
  int ret;

  ret = nxmutex_lock(&service->endpoint_lock);
  if (ret < 0)
    {
      return;
    }

  if (!__atomic_load_n(&service->endpoint_created, __ATOMIC_ACQUIRE))
    {
      service->endpoint.priv = service;
      if (service->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&service->endpoint);
        }

      ret = rpmsg_create_ept(&service->endpoint, rdev, name,
                             RPMSG_ADDR_ANY, dest,
                             bkvoice_service_cb, bkvoice_service_unbind);
      if (ret >= 0)
        {
          bkvoice_advance_generation(service);
          __atomic_store_n(&service->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&service->endpoint_lock);
}

static void bkvoice_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  struct bkvoice_service_s *service = priv;
  const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL || strcmp(cpuname, "cp") != 0)
    {
      return;
    }

  bkvoice_service_unbind(&service->endpoint);
  if (nxmutex_lock(&service->endpoint_lock) >= 0)
    {
      if (service->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&service->endpoint);
        }

      memset(&service->endpoint, 0, sizeof(service->endpoint));

      nxmutex_unlock(&service->endpoint_lock);
    }
}

int bk7258_voice_service_prepare(void)
{
  return OK;
}

int bk7258_voice_service_start(void)
{
  struct bkvoice_service_s *service = &g_bkvoice_service;
  const struct bkvoice_capture_source_ops_s *source_ops;
  const struct bkvoice_turn_audio_ops_s *audio_ops;
  const struct bkvoice_ptt_worker_config_s worker_config =
  {
    .stack_size = CONFIG_BK7258_VOICE_CAPTURE_STACKSIZE,
    .priority = CONFIG_BK7258_VOICE_CAPTURE_PRIORITY,
    .join_timeout_ms = CONFIG_BK7258_VOICE_CAPTURE_JOIN_TIMEOUT_MS,
  };
  const struct bkvoice_turn_limits_s turn_limits =
  {
    .capture_timeout_ms = CONFIG_BK7258_VOICE_CAPTURE_TIMEOUT_MS,
    .waiting_tts_timeout_ms = CONFIG_BK7258_VOICE_WAITING_TTS_TIMEOUT_MS,
    .playback_timeout_ms = CONFIG_BK7258_VOICE_PLAYBACK_TIMEOUT_MS,
    .audio_frame_bytes = BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
  };
  uint32_t boot_generation;
  bool callback_registered = false;
  bool ptt_initialized = false;
  bool semaphore_initialized = false;
  pid_t pid;
  int ret;

  ret = nxmutex_lock(&service->init_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (__atomic_load_n(&service->initialized, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&service->init_lock);
      return OK;
    }

#ifndef CONFIG_MEDIA
  bk7258_agent_media_player_link();
  bk7258_agent_media_recorder_link();
#endif
  ret = bkvoice_turn_audio_initialize(&service->turn_audio);
  if (ret >= 0)
    {
      audio_ops = bkvoice_turn_audio_ops();
      source_ops = bkvoice_turn_audio_capture_source_ops();
      boot_generation = __atomic_load_n(
        &bk7258_ap_boot_state()->generation, __ATOMIC_ACQUIRE);
      ret = audio_ops != NULL && source_ops != NULL ? 0 : -ENOSYS;
      if (ret >= 0 && boot_generation == 0)
        {
          ret = -EAGAIN;
        }

      if (ret >= 0)
        {
          ret = bkvoice_ptt_initialize(
            &service->ptt, audio_ops, &service->turn_audio,
            source_ops, &service->turn_audio, &turn_limits,
            &worker_config, boot_generation);
          ptt_initialized = ret >= 0;
        }

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
      if (ret >= 0)
        {
          int feedback_ret = bk7258_voice_feedback_start();

          if (feedback_ret >= 0)
            {
              feedback_ret = bkvoice_turn_set_state_observer(
                &service->ptt.turn, bk7258_voice_feedback_report, NULL);
            }

          if (feedback_ret < 0)
            {
              syslog(LOG_WARNING,
                     "BKVOICE EYES start ret=%d best_effort=1\n",
                     feedback_ret);
            }
        }
#endif
    }

  if (ret >= 0)
    {
      ret = nxsem_init(&service->request_sem, 0, 0);
    }
  if (ret >= 0)
    {
      semaphore_initialized = true;
    }

#ifdef CONFIG_BK7258_VOICE_TLS
  if (ret >= 0)
    {
      ret = bkvoice_runtime_initialize(&service->ptt, boot_generation,
                                       &service->request_sem);
    }
#endif

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&service->request_sem, SEM_PRIO_NONE);
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(service, NULL,
                                    bkvoice_device_destroy,
                                    bkvoice_ns_match, bkvoice_ns_bind);
      callback_registered = ret >= 0;
    }

  if (ret >= 0)
    {
      pid = task_create("bkvoice-svc", CONFIG_BK7258_VOICE_SERVICE_PRIORITY,
                        CONFIG_BK7258_VOICE_SERVICE_STACKSIZE,
                        bkvoice_worker, NULL);
      if (pid < 0)
        {
          ret = bkvoice_errno();
        }
    }

  if (ret >= 0)
    {
      __atomic_store_n(&service->initialized, true, __ATOMIC_RELEASE);
      syslog(LOG_INFO,
             "BKVOICE SERVICE READY endpoint=%s persona=%s name=%s "
             "disclosure=%s ptt_owner=ready eyes=best-effort "
             "transport=%s\n",
             BKVOICE_RPC_ENDPOINT, BKVOICE_PRODUCT_PERSONA_ID,
             BKVOICE_PRODUCT_DISPLAY_NAME, BKVOICE_PRODUCT_DISCLOSURE,
             BKVOICE_TRANSPORT_STATUS);
    }
  else
    {
#ifdef CONFIG_BK7258_VOICE_TLS
      (void)bkvoice_runtime_uninitialize();
#endif
      if (callback_registered)
        {
          rpmsg_unregister_callback(service, NULL,
                                    bkvoice_device_destroy,
                                    bkvoice_ns_match, bkvoice_ns_bind);
        }

      if (nxmutex_lock(&service->endpoint_lock) >= 0)
        {
          __atomic_store_n(&service->endpoint_created, false,
                           __ATOMIC_RELEASE);
          bkvoice_advance_generation(service);
          if (service->endpoint.rdev != NULL)
            {
              rpmsg_destroy_ept(&service->endpoint);
            }

          memset(&service->endpoint, 0, sizeof(service->endpoint));
          nxmutex_unlock(&service->endpoint_lock);
        }

      if (semaphore_initialized)
        {
          (void)nxsem_destroy(&service->request_sem);
        }

      if (ptt_initialized)
        {
          (void)bkvoice_ptt_uninitialize(&service->ptt);
        }

      service->active = false;
      service->replay_valid = false;
    }

  nxmutex_unlock(&service->init_lock);
  return ret;
}

#endif /* CONFIG_BK7258_VOICE_SERVICE */
