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
#include "bk7258_voice_protocol.h"

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

#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

#define BKVOICE_STREAM_BYTES 2048u
#define BKVOICE_PLAYER_OPTIONS \
  "format=s16le:sample_rate=16000:ch_layout=mono"

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
};

static struct bkvoice_service_s g_bkvoice_service =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = SP_UNLOCKED,
};

extern void bk7258_agent_media_player_link(void);

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
                    BKVOICE_STATUS_LOCAL_ONLY;
  if (stat("/dev/mmcsd0", &status) == 0)
    {
      response->flags |= BKVOICE_STATUS_BLOCK_PRESENT;
    }

  if (request->command == BKVOICE_RPC_STATUS)
    {
      return OK;
    }

  memset(&pack, 0, sizeof(pack));
  ret = bkvoice_pack_load(request->manifest,
                          request->command == BKVOICE_RPC_PLAY ?
                          request->clip_id : NULL,
                          &pack);
  response->pack_version = pack.version;
  response->clip_count = pack.clip_count;
  response->error_line = pack.error_line;
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

      if (nxsem_wait_uninterruptible(&service->request_sem) < 0)
        {
          continue;
        }

      flags = spin_lock_irqsave(&service->request_lock);
      memcpy(&request, &service->active_request, sizeof(request));
      generation = service->active_generation;
      spin_unlock_irqrestore(&service->request_lock, flags);

      (void)bkvoice_handle_request(service, generation, &request, &response);

      flags = spin_lock_irqsave(&service->request_lock);
      memcpy(&service->last_request, &request, sizeof(request));
      memcpy(&service->last_response, &response, sizeof(response));
      service->replay_valid = true;
      service->active = false;
      spin_unlock_irqrestore(&service->request_lock, flags);

      /* Never wait for a TX buffer.  A lost response is replayed when the
       * client retries the same session/sequence request.
       */

      (void)bkvoice_send(service, &response);
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

  if (request->command == BKVOICE_RPC_STATUS)
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
      ret = rpmsg_create_ept(&service->endpoint, rdev, name,
                             RPMSG_ADDR_ANY, dest,
                             bkvoice_service_cb, NULL);
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

  __atomic_store_n(&service->endpoint_created, false, __ATOMIC_RELEASE);
  bkvoice_advance_generation(service);
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
  bool callback_registered = false;
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

  bk7258_agent_media_player_link();
  ret = nxsem_init(&service->request_sem, 0, 0);
  if (ret >= 0)
    {
      semaphore_initialized = true;
    }

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
      syslog(LOG_INFO, "BKVOICE SERVICE READY endpoint=%s\n",
             BKVOICE_RPC_ENDPOINT);
    }
  else
    {
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

      service->active = false;
      service->replay_valid = false;
    }

  nxmutex_unlock(&service->init_lock);
  return ret;
}

#endif /* CONFIG_BK7258_VOICE_SERVICE */
