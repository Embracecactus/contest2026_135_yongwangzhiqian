/****************************************************************************
 * app/bk7258/bk7258_display_rpc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-side transport for the application-owned BKDisplay protocol.  The
 * receive callback only validates and queues requests; SD NAND and LCD work
 * runs in the dedicated worker.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_DISPLAY_SERVICE

#include "bk7258_display_protocol.h"
#include "bk7258_display_rpc.h"
#include "bk7258_display_rpc_core.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

struct bkdisplay_rpc_server_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  spinlock_t request_lock;
  sem_t request_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  bool active;
  bool replay_valid;
  struct bkdisplay_rpc_request_s active_request;
  struct bkdisplay_rpc_request_s last_request;
  struct bkdisplay_rpc_response_s last_response;
};

static struct bkdisplay_rpc_server_s g_bkdisplay_rpc_server =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = SP_UNLOCKED,
};

static int bkdisplay_rpc_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static int bkdisplay_rpc_send(
  struct bkdisplay_rpc_server_s *server,
  const struct bkdisplay_rpc_response_s *response)
{
  int ret = nxmutex_lock(&server->endpoint_lock);

  if (ret < 0)
    {
      return ret;
    }

  if (!__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE) ||
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

static int bkdisplay_rpc_worker(int argc, char **argv)
{
  struct bkdisplay_rpc_server_s *server = &g_bkdisplay_rpc_server;

  (void)argc;
  (void)argv;

  for (; ; )
    {
      struct bkdisplay_rpc_request_s request;
      struct bkdisplay_rpc_response_s response;
      irqstate_t flags;

      if (nxsem_wait_uninterruptible(&server->request_sem) < 0)
        {
          continue;
        }

      flags = spin_lock_irqsave(&server->request_lock);
      memcpy(&request, &server->active_request, sizeof(request));
      spin_unlock_irqrestore(&server->request_lock, flags);

      (void)bkdisplay_rpc_handle_request(&request, &response);

      flags = spin_lock_irqsave(&server->request_lock);
      memcpy(&server->last_request, &request, sizeof(request));
      memcpy(&server->last_response, &response, sizeof(response));
      server->replay_valid = true;
      server->active = false;
      spin_unlock_irqrestore(&server->request_lock, flags);

      /* A client retry replays the completed response if no TX buffer was
       * available here.  Never block the display worker on RPMsg buffers.
       */

      (void)bkdisplay_rpc_send(server, &response);
    }

  return 0;
}

static int bkdisplay_rpc_server_cb(struct rpmsg_endpoint *endpoint,
                                   void *data, size_t len, uint32_t src,
                                   void *priv)
{
  struct bkdisplay_rpc_server_s *server = priv;
  const struct bkdisplay_rpc_request_s *request = data;
  struct bkdisplay_rpc_response_s response;
  irqstate_t flags;
  bool replay = false;
  bool duplicate = false;
  int ret;

  (void)endpoint;
  (void)src;

  if (request == NULL || len != sizeof(*request) ||
      request->magic != BKDISPLAY_RPC_MAGIC ||
      request->version != BKDISPLAY_RPC_VERSION)
    {
      return -EINVAL;
    }

  if (!bkdisplay_rpc_request_valid(request))
    {
      bkdisplay_rpc_make_response(&response, request, -EINVAL);
      return bkdisplay_rpc_send(server, &response);
    }

  flags = spin_lock_irqsave(&server->request_lock);
  if (server->replay_valid &&
      request->session == server->last_request.session &&
      request->sequence == server->last_request.sequence)
    {
      if (memcmp(request, &server->last_request, sizeof(*request)) != 0)
        {
          spin_unlock_irqrestore(&server->request_lock, flags);
          bkdisplay_rpc_make_response(&response, request, -EPROTO);
          return bkdisplay_rpc_send(server, &response);
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

      bkdisplay_rpc_make_response(&response, request, -EBUSY);
      return bkdisplay_rpc_send(server, &response);
    }
  else
    {
      memcpy(&server->active_request, request, sizeof(*request));
      server->active = true;
    }

  spin_unlock_irqrestore(&server->request_lock, flags);
  if (replay)
    {
      return bkdisplay_rpc_send(server, &response);
    }

  ret = nxsem_post(&server->request_sem);
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&server->request_lock);
      server->active = false;
      spin_unlock_irqrestore(&server->request_lock, flags);
      bkdisplay_rpc_make_response(&response, request, ret);
      return bkdisplay_rpc_send(server, &response);
    }

  return 0;
}

static bool bkdisplay_rpc_ns_match(struct rpmsg_device *rdev, void *priv,
                                   const char *name, uint32_t dest)
{
  const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv;
  (void)dest;
  return cpuname != NULL && strcmp(cpuname, "cp") == 0 &&
         strcmp(name, BKDISPLAY_RPC_ENDPOINT) == 0;
}

static void bkdisplay_rpc_ns_bind(struct rpmsg_device *rdev, void *priv,
                                  const char *name, uint32_t dest)
{
  struct bkdisplay_rpc_server_s *server = priv;
  int ret;

  ret = nxmutex_lock(&server->endpoint_lock);
  if (ret < 0)
    {
      return;
    }

  if (!__atomic_load_n(&server->endpoint_created, __ATOMIC_ACQUIRE))
    {
      server->endpoint.priv = server;
      ret = rpmsg_create_ept(&server->endpoint, rdev, name,
                             RPMSG_ADDR_ANY, dest,
                             bkdisplay_rpc_server_cb, NULL);
      if (ret >= 0)
        {
          __atomic_store_n(&server->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&server->endpoint_lock);
}

static void bkdisplay_rpc_device_destroy(struct rpmsg_device *rdev,
                                         void *priv)
{
  struct bkdisplay_rpc_server_s *server = priv;
  const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL || strcmp(cpuname, "cp") != 0)
    {
      return;
    }

  __atomic_store_n(&server->endpoint_created, false, __ATOMIC_RELEASE);
  if (nxmutex_lock(&server->endpoint_lock) >= 0)
    {
      if (server->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&server->endpoint);
        }

      memset(&server->endpoint, 0, sizeof(server->endpoint));
      nxmutex_unlock(&server->endpoint_lock);
    }
}

int bk7258_display_rpc_server_initialize(void)
{
  struct bkdisplay_rpc_server_s *server = &g_bkdisplay_rpc_server;
  bool callback_registered = false;
  bool semaphore_initialized = false;
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
      semaphore_initialized = true;
    }

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&server->request_sem, SEM_PRIO_NONE);
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(server, NULL,
                                    bkdisplay_rpc_device_destroy,
                                    bkdisplay_rpc_ns_match,
                                    bkdisplay_rpc_ns_bind);
      callback_registered = ret >= 0;
    }

  if (ret >= 0)
    {
      pid = task_create("bkdisplay-rpc", CONFIG_BK7258_DISPLAY_RPC_PRIORITY,
                        CONFIG_BK7258_DISPLAY_RPC_STACKSIZE,
                        bkdisplay_rpc_worker, NULL);
      if (pid < 0)
        {
          ret = bkdisplay_rpc_errno();
        }
    }

  if (ret >= 0)
    {
      __atomic_store_n(&server->initialized, true, __ATOMIC_RELEASE);
      syslog(LOG_INFO, "BKDISPLAY RPC READY endpoint=%s\n",
             BKDISPLAY_RPC_ENDPOINT);
    }
  else
    {
      if (callback_registered)
        {
          rpmsg_unregister_callback(server, NULL,
                                    bkdisplay_rpc_device_destroy,
                                    bkdisplay_rpc_ns_match,
                                    bkdisplay_rpc_ns_bind);
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

      if (semaphore_initialized)
        {
          (void)nxsem_destroy(&server->request_sem);
        }

      server->active = false;
      server->replay_valid = false;
    }

  nxmutex_unlock(&server->init_lock);
  return ret;
}

#endif /* CONFIG_BK7258_DISPLAY_SERVICE */
