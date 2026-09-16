/****************************************************************************
 * app/bk7258/bk7258_vision_client.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_VISION

#include "bk7258_vision_core.h"
#include "bk7258_vision_protocol.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

struct bkvision_client_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  mutex_t request_lock;
  spinlock_t reply_lock;
  sem_t reply_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  volatile bool reply_valid;
  volatile int connection_error;
  uint32_t session_id;
  uint32_t sequence;
  uint32_t waiting_session_id;
  uint32_t waiting_sequence;
  struct bkvision_rpc_response_s reply;
};

static struct bkvision_client_s g_bkvision_client =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = NXMUTEX_INITIALIZER,
  .reply_lock = SP_UNLOCKED,
};

static void bkvision_flush_sem(sem_t *sem)
{
  while (nxsem_trywait(sem) == 0)
    {
    }
}

static bool bkvision_endpoint_ready(struct bkvision_client_s *client)
{
  bool ready = false;

  if (nxmutex_lock(&client->endpoint_lock) >= 0)
    {
      ready = __atomic_load_n(&client->endpoint_created,
                              __ATOMIC_ACQUIRE) &&
              is_rpmsg_ept_ready(&client->endpoint);
      nxmutex_unlock(&client->endpoint_lock);
    }

  return ready;
}

static int bkvision_wait_endpoint(struct bkvision_client_s *client)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKVISION_RPC_ENDPOINT_WAIT_MS);
  int ret;

  do
    {
      if (bkvision_endpoint_ready(client))
        {
          return 0;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  ret = __atomic_load_n(&client->connection_error, __ATOMIC_ACQUIRE);
  return ret < 0 ? ret : -ETIMEDOUT;
}

static int bkvision_send_bounded(struct bkvision_client_s *client,
                                 const struct bkvision_rpc_request_s *request)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKVISION_RPC_SEND_WAIT_MS);
  int ret = -ENOTCONN;

  do
    {
      ret = nxmutex_lock(&client->endpoint_lock);
      if (ret < 0)
        {
          return ret;
        }

      if (!__atomic_load_n(&client->endpoint_created, __ATOMIC_ACQUIRE) ||
          !is_rpmsg_ept_ready(&client->endpoint))
        {
          ret = -ENOTCONN;
        }
      else
        {
          ret = rpmsg_trysend(&client->endpoint, request, sizeof(*request));
        }

      nxmutex_unlock(&client->endpoint_lock);
      if (ret >= 0)
        {
          return 0;
        }

      if (ret != -ENOMEM && ret != -EAGAIN)
        {
          return ret;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  return -ETIMEDOUT;
}

static int bkvision_wait_reply(struct bkvision_client_s *client,
                               const struct bkvision_rpc_request_s *request,
                               struct bkvision_rpc_response_s *response,
                               unsigned int timeout_ms)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(timeout_ms);

  for (;;)
    {
      irqstate_t flags;
      clock_t elapsed;
      bool valid;
      int ret;

      flags = spin_lock_irqsave(&client->reply_lock);
      valid = client->reply_valid &&
              client->reply.session_id == request->session_id &&
              client->reply.sequence == request->sequence;
      if (valid)
        {
          memcpy(response, &client->reply, sizeof(*response));
          client->reply_valid = false;
        }

      spin_unlock_irqrestore(&client->reply_lock, flags);
      if (valid)
        {
          return 0;
        }

      ret = __atomic_load_n(&client->connection_error, __ATOMIC_ACQUIRE);
      if (ret < 0)
        {
          return ret;
        }

      elapsed = clock_systime_ticks() - start;
      if (elapsed >= limit)
        {
          return -ETIMEDOUT;
        }

      ret = nxsem_tickwait_uninterruptible(&client->reply_sem,
                                           limit - elapsed);
      if (ret < 0)
        {
          return ret;
        }
    }
}

static int bkvision_client_cb(struct rpmsg_endpoint *endpoint, void *data,
                              size_t len, uint32_t src, void *priv)
{
  struct bkvision_client_s *client = priv;
  const struct bkvision_rpc_response_s *response = data;
  irqstate_t flags;
  bool matched;

  (void)endpoint;
  (void)src;
  if (response == NULL || len != sizeof(*response) ||
      response->magic != BKVISION_RPC_MAGIC ||
      response->version != BKVISION_RPC_VERSION ||
      !bkvision_rpc_response_valid(response))
    {
      return -ENOMSG;
    }

  flags = spin_lock_irqsave(&client->reply_lock);
  matched = client->waiting_session_id != 0 &&
            response->session_id == client->waiting_session_id &&
            response->sequence == client->waiting_sequence;
  if (matched)
    {
      memcpy(&client->reply, response, sizeof(client->reply));
      client->reply_valid = true;
    }

  spin_unlock_irqrestore(&client->reply_lock, flags);
  return matched ? nxsem_post(&client->reply_sem) : -ENOMSG;
}

static void bkvision_device_created(struct rpmsg_device *rdev, void *priv)
{
  struct bkvision_client_s *client = priv;
  const char *cpuname = rpmsg_get_cpuname(rdev);
  int ret;

  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }

  ret = nxmutex_lock(&client->endpoint_lock);
  if (ret < 0)
    {
      __atomic_store_n(&client->connection_error, ret, __ATOMIC_RELEASE);
      return;
    }

  if (!__atomic_load_n(&client->endpoint_created, __ATOMIC_ACQUIRE))
    {
      client->endpoint.priv = client;
      ret = rpmsg_create_ept(&client->endpoint, rdev, BKVISION_RPC_ENDPOINT,
                             RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                             bkvision_client_cb, NULL);
      __atomic_store_n(&client->connection_error, ret, __ATOMIC_RELEASE);
      if (ret >= 0)
        {
          __atomic_store_n(&client->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&client->endpoint_lock);
}

static void bkvision_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  struct bkvision_client_s *client = priv;
  const char *cpuname = rpmsg_get_cpuname(rdev);
  irqstate_t flags;

  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }

  __atomic_store_n(&client->endpoint_created, false, __ATOMIC_RELEASE);
  __atomic_store_n(&client->connection_error, -ENOTCONN,
                   __ATOMIC_RELEASE);
  if (nxmutex_lock(&client->endpoint_lock) >= 0)
    {
      if (client->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&client->endpoint);
        }

      memset(&client->endpoint, 0, sizeof(client->endpoint));
      nxmutex_unlock(&client->endpoint_lock);
    }

  flags = spin_lock_irqsave(&client->reply_lock);
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);
  (void)nxsem_post(&client->reply_sem);
}

int bkvision_rpc_client_initialize(void)
{
  struct bkvision_client_s *client = &g_bkvision_client;
  bool semaphore_initialized = false;
  int ret;

  ret = nxmutex_lock(&client->init_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (__atomic_load_n(&client->initialized, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&client->init_lock);
      return 0;
    }

  __atomic_store_n(&client->connection_error, -ENOTCONN,
                   __ATOMIC_RELEASE);
  ret = nxsem_init(&client->reply_sem, 0, 0);
  if (ret >= 0)
    {
      semaphore_initialized = true;
    }

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&client->reply_sem, SEM_PRIO_NONE);
    }
#endif

  if (ret >= 0)
    {
      client->session_id = (uint32_t)clock_systime_ticks() ^
                           (uint32_t)(uintptr_t)client;
      if (client->session_id == 0)
        {
          client->session_id = 1;
        }

      ret = rpmsg_register_callback(client, bkvision_device_created,
                                    bkvision_device_destroy, NULL, NULL);
    }

  if (ret >= 0)
    {
      __atomic_store_n(&client->initialized, true, __ATOMIC_RELEASE);
    }
  else if (semaphore_initialized)
    {
      (void)nxsem_destroy(&client->reply_sem);
    }

  nxmutex_unlock(&client->init_lock);
  return ret;
}

int bkvision_rpc_exchange(struct bkvision_rpc_request_s *request,
                          struct bkvision_rpc_response_s *response,
                          unsigned int timeout_ms)
{
  struct bkvision_client_s *client = &g_bkvision_client;
  irqstate_t flags;
  unsigned int attempt;
  int ret;

  if (request == NULL || response == NULL || timeout_ms == 0)
    {
      return -EINVAL;
    }

  ret = bkvision_rpc_client_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&client->request_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvision_wait_endpoint(client);
  if (ret < 0)
    {
      goto out;
    }

  if (++client->sequence == 0)
    {
      client->sequence++;
    }

  request->magic = BKVISION_RPC_MAGIC;
  request->version = BKVISION_RPC_VERSION;
  request->session_id = client->session_id;
  request->sequence = client->sequence;
  if (request->command != BKVISION_RPC_CHECK_RECORD)
    {
      request->reserved = 0;
    }
  if (!bkvision_rpc_request_valid(request))
    {
      ret = -EINVAL;
      goto out;
    }

  bkvision_flush_sem(&client->reply_sem);
  flags = spin_lock_irqsave(&client->reply_lock);
  client->waiting_session_id = request->session_id;
  client->waiting_sequence = request->sequence;
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);

  ret = -ETIMEDOUT;
  for (attempt = 0; attempt < BKVISION_RPC_ATTEMPTS; attempt++)
    {
      ret = bkvision_send_bounded(client, request);
      if (ret < 0)
        {
          break;
        }

      ret = bkvision_wait_reply(client, request, response, timeout_ms);
      if (ret == 0 && response->rpc_status == 0 &&
          response->operation_status == 0 &&
          (request->command == BKVISION_RPC_RECORD) !=
          ((response->flags & BKVISION_FLAG_RECORDED) != 0))
        {
          ret = -EPROTO;
        }
      if (ret != -ETIMEDOUT)
        {
          break;
        }
    }

  flags = spin_lock_irqsave(&client->reply_lock);
  client->waiting_session_id = 0;
  client->waiting_sequence = 0;
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);

out:
  nxmutex_unlock(&client->request_lock);
  return ret;
}

#endif /* CONFIG_BK7258_APP_VISION */
