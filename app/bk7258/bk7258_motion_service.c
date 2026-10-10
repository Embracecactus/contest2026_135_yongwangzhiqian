/****************************************************************************
 * app/bk7258/bk7258_motion_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP motion service.  RPMsg callbacks only validate and queue; the sole
 * worker performs the public uORB open/ioctl/read/close sequence.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_MOTION_SERVICE

#include "bk7258_motion_core.h"
#include "bk7258_motion_service.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/irq.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/sensors/ioctl.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>
#include <nuttx/uorb.h>

#define BKMOTION_INTERVAL_US 10000u
#define BKMOTION_SETTLE_US   12000u

struct bkmotion_source_s
{
  int fd;
  int release_error;
};

struct bkmotion_server_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  mutex_t sample_lock;
  spinlock_t request_lock;
  sem_t request_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  bool active;
  bool pending;
  bool quiescing;
  bool io_active;
  uint32_t epoch;
  uint32_t request_epoch;
  uint32_t admission_epoch;
  uint32_t request_admission_epoch;
  bool replay_valid;
  struct bkmotion_rpc_request_s active_request;
  struct bkmotion_rpc_request_s last_request;
  struct bkmotion_rpc_response_s last_response;
  struct bkmotion_source_s source;
  bool polling;
  bool snapshot_valid;
  uint32_t poll_epoch;
  clock_t poll_at;
  struct bkmotion_rpc_response_s snapshot;
};

static struct bkmotion_server_s g_bkmotion_server =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .sample_lock = NXMUTEX_INITIALIZER,
  .request_lock = SP_UNLOCKED,
  .source =
  {
    .fd = -1,
  },
};

static int bkmotion_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static int bkmotion_open(void *context)
{
  struct bkmotion_source_s *source = context;
  uint32_t interval_us = BKMOTION_INTERVAL_US;
  int ret;

  if (source->fd >= 0)
    {
      return -EBUSY;
    }

  source->fd = open(CONFIG_BK7258_MOTION_DEVPATH, O_RDONLY | O_NONBLOCK);
  if (source->fd < 0)
    {
      return bkmotion_errno();
    }

  ret = ioctl(source->fd, SNIOC_SET_INTERVAL, (unsigned long)interval_us);
  if (ret < 0)
    {
      ret = bkmotion_errno();
      if (close(source->fd) < 0)
        __atomic_store_n(&source->release_error, bkmotion_errno(),
                         __ATOMIC_RELEASE);
      source->fd = -1;
      return ret;
    }

  nxsig_usleep(BKMOTION_SETTLE_US);
  return 0;
}

static int bkmotion_read(void *context, struct bkmotion_sample_s *sample)
{
  struct bkmotion_source_s *source = context;
  struct sensor_accel raw;
  ssize_t ret;

  if (source->fd < 0 || sample == NULL)
    {
      return -EINVAL;
    }

  memset(&raw, 0, sizeof(raw));
  ret = read(source->fd, &raw, sizeof(raw));
  if (ret < 0)
    {
      return bkmotion_errno();
    }

  if (ret != sizeof(raw))
    {
      return -EMSGSIZE;
    }

  sample->timestamp_us = raw.timestamp;
  sample->x = raw.x;
  sample->y = raw.y;
  sample->z = raw.z;
  sample->status = raw.status;
  return 0;
}

static int bkmotion_close(void *context)
{
  struct bkmotion_source_s *source = context;
  int fd = source->fd;

  source->fd = -1;
  if (fd < 0)
    {
      return -EBADF;
    }

  int ret = close(fd) < 0 ? bkmotion_errno() : 0;
  if (ret < 0)
    __atomic_store_n(&source->release_error, ret, __ATOMIC_RELEASE);
  return ret;
}

static const struct bkmotion_source_ops_s g_bkmotion_ops =
{
  .open = bkmotion_open,
  .read = bkmotion_read,
  .close = bkmotion_close,
};

int bk7258_motion_service_quiesce(bool stop)
{
  struct bkmotion_server_s *server = &g_bkmotion_server;
  irqstate_t flags = spin_lock_irqsave(&server->request_lock);
  int ret;

  if (stop)
    {
      /* Reopening admission cannot revive work accepted before this stop.
       * Saturate instead of wrapping into an old ticket's identity.
       */

      if (!server->quiescing && server->admission_epoch < UINT32_MAX)
        {
          server->admission_epoch++;
        }

      server->quiescing = true;
      server->replay_valid = false;
      server->polling = false;
      server->snapshot_valid = false;
    }

  ret = server->io_active ? -EBUSY :
    server->admission_epoch == UINT32_MAX ? -EOVERFLOW :
    __atomic_load_n(&server->source.release_error, __ATOMIC_ACQUIRE);
  if (!stop && ret == 0) server->quiescing = false;
  spin_unlock_irqrestore(&server->request_lock, flags);
  return ret;
}

static int bkmotion_collect(const struct bkmotion_rpc_request_s *request,
                            struct bkmotion_rpc_response_s *response,
                            uint32_t admission_epoch)
{
  struct bkmotion_server_s *server = &g_bkmotion_server;
  irqstate_t flags = spin_lock_irqsave(&server->request_lock);
  bool stopped = server->quiescing;
  bool revoked = server->admission_epoch != admission_epoch;
  spin_unlock_irqrestore(&server->request_lock, flags);
  if (stopped || revoked)
    {
      int error = stopped ? -ESHUTDOWN : -ECANCELED;
      bkmotion_rpc_make_response(response, request, error);
      return error;
    }

  int ret = nxmutex_lock(&server->sample_lock);
  if (ret < 0)
    {
      bkmotion_rpc_make_response(response, request, ret);
      return ret;
    }

  flags = spin_lock_irqsave(&server->request_lock);
  ret = server->quiescing ? -ESHUTDOWN :
    server->admission_epoch != admission_epoch ? -ECANCELED :
    __atomic_load_n(&server->source.release_error, __ATOMIC_ACQUIRE);
  if (ret == 0) server->io_active = true;
  spin_unlock_irqrestore(&server->request_lock, flags);
  if (ret == 0)
    {
      ret = bkmotion_rpc_handle_request(request, response, &g_bkmotion_ops,
                                       &server->source);
      flags = spin_lock_irqsave(&server->request_lock);
      server->io_active = false;
      /* Stop revokes publication, not the descriptor's cleanup obligation. */
      if (server->quiescing && ret == 0)
        {
          ret = -ECANCELED;
          bkmotion_rpc_make_response(response, request, ret);
        }

      spin_unlock_irqrestore(&server->request_lock, flags);
    }
  else
    {
      bkmotion_rpc_make_response(response, request, ret);
    }
  nxmutex_unlock(&server->sample_lock);
  return ret;
}

int bk7258_motion_service_sample(struct bkmotion_rpc_response_s *sample)
{
  const struct bkmotion_rpc_request_s request = {
    .magic = BKMOTION_RPC_MAGIC, .version = BKMOTION_RPC_VERSION,
    .command = BKMOTION_RPC_SAMPLE, .session = 1, .sequence = 1
  };
  if (!sample) return -EINVAL;
  if (!__atomic_load_n(&g_bkmotion_server.initialized, __ATOMIC_ACQUIRE))
    {
      bkmotion_rpc_make_response(sample, &request, -ENODEV);
      return -ENODEV;
    }
  /* Local Agent and CP requests share this single sampling owner and mutex;
   * no second collection service is created.
   */

  irqstate_t flags = spin_lock_irqsave(&g_bkmotion_server.request_lock);
  uint32_t admission_epoch = g_bkmotion_server.admission_epoch;
  spin_unlock_irqrestore(&g_bkmotion_server.request_lock, flags);
  return bkmotion_collect(&request, sample, admission_epoch);
}

static int bkmotion_send(struct bkmotion_server_s *server,
                         const struct bkmotion_rpc_response_s *response,
                         uint32_t epoch)
{
  irqstate_t flags;
  bool current;
  int ret = nxmutex_lock(&server->endpoint_lock);

  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&server->request_lock);
  current = server->epoch == epoch;
  spin_unlock_irqrestore(&server->request_lock, flags);

  if (!current ||
      !__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE) ||
      !is_rpmsg_ept_ready(&server->endpoint))
    {
      ret = -ENOTCONN;
    }
  else
    {
      ret = rpmsg_trysend(&server->endpoint, response, sizeof(*response));
    }

  nxmutex_unlock(&server->endpoint_lock);
  return ret;
}

static int bkmotion_worker(int argc, char **argv)
{
  struct bkmotion_server_s *server = &g_bkmotion_server;

  (void)argc;
  (void)argv;
  for (;;)
    {
      struct bkmotion_rpc_request_s request;
      struct bkmotion_rpc_response_s response;
      irqstate_t flags;
      uint32_t epoch;
      uint32_t admission_epoch;

      flags = spin_lock_irqsave(&server->request_lock);
      bool polling = server->polling && !server->quiescing;
      spin_unlock_irqrestore(&server->request_lock, flags);
      int waited = polling ? nxsem_tickwait_uninterruptible(
        &server->request_sem, MSEC2TICK(100)) :
        nxsem_wait_uninterruptible(&server->request_sem);
      if (waited < 0 && waited != -ETIMEDOUT)
        {
          continue;
        }

      flags = spin_lock_irqsave(&server->request_lock);
      if (server->polling && !server->quiescing &&
          clock_systime_ticks() - server->poll_at >= MSEC2TICK(100))
        {
          uint32_t poll_epoch = server->poll_epoch;
          admission_epoch = server->admission_epoch;
          server->poll_at = clock_systime_ticks();
          spin_unlock_irqrestore(&server->request_lock, flags);
          const struct bkmotion_rpc_request_s poll =
          {
            .magic = BKMOTION_RPC_MAGIC,
            .version = BKMOTION_RPC_VERSION,
            .command = BKMOTION_RPC_SAMPLE,
            .session = 1,
            .sequence = 1
          };

          (void)bkmotion_collect(&poll, &response, admission_epoch);
          flags = spin_lock_irqsave(&server->request_lock);
          if (server->polling && !server->quiescing &&
              server->poll_epoch == poll_epoch &&
              server->admission_epoch == admission_epoch)
            {
              server->snapshot = response;
              server->snapshot_valid = true;
            }
        }

      /* A disconnect may leave a semaphore token behind.  Only the pending
       * slot owns work; consuming another token must never execute it twice.
       */

      if (!server->active || !server->pending ||
          server->request_epoch != server->epoch ||
          !__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
        {
          spin_unlock_irqrestore(&server->request_lock, flags);
          continue;
        }

      server->pending = false;
      epoch = server->request_epoch;
      admission_epoch = server->request_admission_epoch;
      memcpy(&request, &server->active_request, sizeof(request));
      spin_unlock_irqrestore(&server->request_lock, flags);

      (void)bkmotion_collect(&request, &response, admission_epoch);

      flags = spin_lock_irqsave(&server->request_lock);
      /* Old I/O still closes its own fd, but must not publish a result or
       * clear a request accepted by a reconnected peer.
       */

      if (server->epoch == epoch && server->active &&
          server->request_epoch == epoch)
        {
          /* Collection may have finished before stop/resume, while this
           * result was still waiting to be committed to the response slot.
           */

          if (server->admission_epoch != admission_epoch)
            {
              bkmotion_rpc_make_response(&response, &request, -ECANCELED);
            }

          memcpy(&server->last_request, &request, sizeof(request));
          memcpy(&server->last_response, &response, sizeof(response));
          server->replay_valid = true;
          server->active = false;
        }
      spin_unlock_irqrestore(&server->request_lock, flags);
      (void)bkmotion_send(server, &response, epoch);
    }

  return 0;
}

int bk7258_motion_service_poll(bool active)
{
  struct bkmotion_server_s *server = &g_bkmotion_server;
  irqstate_t flags = spin_lock_irqsave(&server->request_lock);
  int ret = !server->initialized ? -ENODEV :
    server->quiescing ? -ESHUTDOWN :
    server->poll_epoch == UINT32_MAX ? -EOVERFLOW : 0;
  bool changed = server->polling != (active && ret == 0);

  if (changed)
    {
      if (server->poll_epoch < UINT32_MAX) server->poll_epoch++;
      server->polling = active && ret == 0 &&
                        server->poll_epoch != UINT32_MAX;
      server->snapshot_valid = false;
    }

  spin_unlock_irqrestore(&server->request_lock, flags);
  if (changed) (void)nxsem_post(&server->request_sem);
  return ret;
}

int bk7258_motion_service_snapshot(struct bkmotion_rpc_response_s *sample)
{
  struct bkmotion_server_s *server = &g_bkmotion_server;
  if (sample == NULL) return -EINVAL;
  irqstate_t flags = spin_lock_irqsave(&server->request_lock);
  int ret = server->snapshot_valid ? 0 : -ENODATA;
  if (ret == 0) *sample = server->snapshot;
  else memset(sample, 0, sizeof(*sample));
  spin_unlock_irqrestore(&server->request_lock, flags);
  return ret;
}

static int bkmotion_server_cb(struct rpmsg_endpoint *endpoint, void *data,
                              size_t len, uint32_t src, void *priv)
{
  struct bkmotion_server_s *server = priv;
  const struct bkmotion_rpc_request_s *request = data;
  struct bkmotion_rpc_response_s response;
  irqstate_t flags;
  uint32_t epoch;
  bool replay = false;
  bool duplicate;
  int ret;

  (void)endpoint;
  (void)src;
  if (request == NULL || len != sizeof(*request) ||
      request->magic != BKMOTION_RPC_MAGIC ||
      request->version != BKMOTION_RPC_VERSION)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&server->request_lock);
  epoch = server->epoch;
  spin_unlock_irqrestore(&server->request_lock, flags);

  if (!bkmotion_rpc_request_valid(request))
    {
      bkmotion_rpc_make_response(&response, request, -EINVAL);
      return bkmotion_send(server, &response, epoch);
    }

  if (request->command == BKMOTION_RPC_STATUS)
    {
      ret = bk7258_motion_service_snapshot(&response);
      if (ret < 0) bkmotion_rpc_make_response(&response, request, ret);
      response.session = request->session;
      response.sequence = request->sequence;
      return bkmotion_send(server, &response, epoch);
    }

  flags = spin_lock_irqsave(&server->request_lock);
  if (server->epoch != epoch ||
      !__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
    {
      spin_unlock_irqrestore(&server->request_lock, flags);
      return -ENOTCONN;
    }

  if (server->quiescing)
    {
      spin_unlock_irqrestore(&server->request_lock, flags);
      bkmotion_rpc_make_response(&response, request, -ESHUTDOWN);
      return bkmotion_send(server, &response, epoch);
    }

  if (server->replay_valid &&
      request->session == server->last_request.session &&
      request->sequence == server->last_request.sequence)
    {
      if (memcmp(request, &server->last_request, sizeof(*request)) != 0)
        {
          spin_unlock_irqrestore(&server->request_lock, flags);
          bkmotion_rpc_make_response(&response, request, -EPROTO);
          return bkmotion_send(server, &response, epoch);
        }

      memcpy(&response, &server->last_response, sizeof(response));
      replay = true;
    }
  else if (server->active)
    {
      duplicate = memcmp(request, &server->active_request,
                         sizeof(*request)) == 0;
      spin_unlock_irqrestore(&server->request_lock, flags);
      if (duplicate)
        {
          return 0;
        }

      bkmotion_rpc_make_response(&response, request, -EBUSY);
      return bkmotion_send(server, &response, epoch);
    }
  else
    {
      memcpy(&server->active_request, request, sizeof(*request));
      server->active = true;
      server->pending = true;
      server->request_epoch = epoch;
      server->request_admission_epoch = server->admission_epoch;
    }

  spin_unlock_irqrestore(&server->request_lock, flags);
  if (replay)
    {
      return bkmotion_send(server, &response, epoch);
    }

  ret = nxsem_post(&server->request_sem);
  if (ret >= 0)
    {
      return 0;
    }

  flags = spin_lock_irqsave(&server->request_lock);
  if (server->epoch == epoch && server->request_epoch == epoch)
    {
      server->active = false;
      server->pending = false;
    }
  spin_unlock_irqrestore(&server->request_lock, flags);
  bkmotion_rpc_make_response(&response, request, ret);
  return bkmotion_send(server, &response, epoch);
}

static void bkmotion_disconnect(struct bkmotion_server_s *server)
{
  irqstate_t flags;

  if (nxmutex_lock(&server->endpoint_lock) >= 0)
    {
      flags = spin_lock_irqsave(&server->request_lock);
      server->epoch++;
      server->active = false;
      server->pending = false;
      server->replay_valid = false;
      __atomic_store_n(&server->endpoint_created, false, __ATOMIC_RELEASE);
      spin_unlock_irqrestore(&server->request_lock, flags);

      if (server->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&server->endpoint);
        }

      memset(&server->endpoint, 0, sizeof(server->endpoint));
      nxmutex_unlock(&server->endpoint_lock);
    }
}

static void bkmotion_server_unbind(struct rpmsg_endpoint *endpoint)
{
  bkmotion_disconnect(endpoint->priv);
}

static bool bkmotion_ns_match(struct rpmsg_device *rdev, void *priv,
                              const char *name, uint32_t dest)
{
  const char *cpu = rpmsg_get_cpuname(rdev);

  (void)priv;
  (void)dest;
  return cpu != NULL && strcmp(cpu, "cp") == 0 &&
         strcmp(name, BKMOTION_RPC_ENDPOINT) == 0;
}

static void bkmotion_ns_bind(struct rpmsg_device *rdev, void *priv,
                             const char *name, uint32_t dest)
{
  struct bkmotion_server_s *server = priv;

  if (nxmutex_lock(&server->endpoint_lock) < 0)
    {
      return;
    }

  if (!__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
    {
      server->endpoint.priv = server;
      if (rpmsg_create_ept(&server->endpoint, rdev, name, RPMSG_ADDR_ANY,
                           dest, bkmotion_server_cb,
                           bkmotion_server_unbind) >= 0)
        {
          __atomic_store_n(&server->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&server->endpoint_lock);
}

static void bkmotion_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  const char *cpu = rpmsg_get_cpuname(rdev);

  if (cpu != NULL && strcmp(cpu, "cp") == 0)
    {
      bkmotion_disconnect(priv);
    }
}

int bk7258_motion_service_prepare(void)
{
  return 0;
}

int bk7258_motion_service_start(void)
{
  struct bkmotion_server_s *server = &g_bkmotion_server;
  bool sem_ready = false;
  bool callback_registered = false;
  pid_t pid;
  int ret;

  ret = nxmutex_lock(&server->init_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (__atomic_load_n(&server->initialized, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&server->init_lock);
      return 0;
    }

  ret = nxsem_init(&server->request_sem, 0, 0);
  if (ret >= 0)
    {
      sem_ready = true;
    }

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&server->request_sem, SEM_PRIO_NONE);
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(server, NULL, bkmotion_device_destroy,
                                    bkmotion_ns_match, bkmotion_ns_bind);
      callback_registered = ret >= 0;
    }

  if (ret >= 0)
    {
      pid = task_create("bkmotion-rpc", CONFIG_BK7258_MOTION_RPC_PRIORITY,
                        CONFIG_BK7258_MOTION_RPC_STACKSIZE, bkmotion_worker,
                        NULL);
      if (pid < 0)
        {
          ret = bkmotion_errno();
        }
    }

  if (ret >= 0)
    {
      __atomic_store_n(&server->initialized, true, __ATOMIC_RELEASE);
      syslog(LOG_INFO,
             "BKMOTION SERVICE READY endpoint=%s device=%s privacy=telemetry-only\n",
             BKMOTION_RPC_ENDPOINT, CONFIG_BK7258_MOTION_DEVPATH);
    }
  else
    {
      if (callback_registered)
        {
          rpmsg_unregister_callback(server, NULL, bkmotion_device_destroy,
                                    bkmotion_ns_match, bkmotion_ns_bind);
        }

      if (nxmutex_lock(&server->endpoint_lock) >= 0)
        {
          __atomic_store_n(&server->endpoint_created, false,
                           __ATOMIC_RELEASE);
          if (server->endpoint.rdev != NULL)
            {
              rpmsg_destroy_ept(&server->endpoint);
            }

          memset(&server->endpoint, 0, sizeof(server->endpoint));
          nxmutex_unlock(&server->endpoint_lock);
        }

      if (sem_ready)
        {
          (void)nxsem_destroy(&server->request_sem);
        }

      server->active = false;
      server->pending = false;
      server->replay_valid = false;
    }

  nxmutex_unlock(&server->init_lock);
  return ret;
}

#endif /* CONFIG_BK7258_MOTION_SERVICE */
