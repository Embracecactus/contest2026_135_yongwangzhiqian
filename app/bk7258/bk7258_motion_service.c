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
};

struct bkmotion_server_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  spinlock_t request_lock;
  sem_t request_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  bool active;
  bool pending;
  uint32_t epoch;
  uint32_t request_epoch;
  bool replay_valid;
  struct bkmotion_rpc_request_s active_request;
  struct bkmotion_rpc_request_s last_request;
  struct bkmotion_rpc_response_s last_response;
  struct bkmotion_source_s source;
};

static struct bkmotion_server_s g_bkmotion_server =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
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
      (void)close(source->fd);
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

  return close(fd) < 0 ? bkmotion_errno() : 0;
}

static const struct bkmotion_source_ops_s g_bkmotion_ops =
{
  .open = bkmotion_open,
  .read = bkmotion_read,
  .close = bkmotion_close,
};

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

      if (nxsem_wait_uninterruptible(&server->request_sem) < 0)
        {
          continue;
        }

      flags = spin_lock_irqsave(&server->request_lock);
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
      memcpy(&request, &server->active_request, sizeof(request));
      spin_unlock_irqrestore(&server->request_lock, flags);

      (void)bkmotion_rpc_handle_request(&request, &response, &g_bkmotion_ops,
                                        &server->source);

      flags = spin_lock_irqsave(&server->request_lock);
      /* Old I/O still closes its own fd, but must not publish a result or
       * clear a request accepted by a reconnected peer.
       */

      if (server->epoch == epoch && server->active &&
          server->request_epoch == epoch)
        {
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

  flags = spin_lock_irqsave(&server->request_lock);
  if (server->epoch != epoch ||
      !__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
    {
      spin_unlock_irqrestore(&server->request_lock, flags);
      return -ENOTCONN;
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
