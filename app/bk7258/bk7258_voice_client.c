/****************************************************************************
 * app/bk7258/bk7258_voice_client.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-side client for the application-owned BKVoice RPMsg protocol.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_VOICE

#include "bk7258_voice_protocol.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

struct bkvoice_client_s
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
  uint32_t session;
  uint32_t sequence;
  uint32_t waiting_session;
  uint32_t waiting_sequence;
  struct bkvoice_rpc_response_s reply;
};

static struct bkvoice_client_s g_bkvoice_client =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = NXMUTEX_INITIALIZER,
  .reply_lock = SP_UNLOCKED,
};

static void bkvoice_flush_sem(sem_t *sem)
{
  while (nxsem_trywait(sem) == OK)
    {
    }
}

static bool bkvoice_endpoint_ready(struct bkvoice_client_s *client)
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

static int bkvoice_wait_endpoint(struct bkvoice_client_s *client)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKVOICE_RPC_ENDPOINT_WAIT_MS);
  int ret;

  do
    {
      if (bkvoice_endpoint_ready(client))
        {
          return OK;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  ret = __atomic_load_n(&client->connection_error, __ATOMIC_ACQUIRE);
  return ret < 0 ? ret : -ETIMEDOUT;
}

static int bkvoice_send_bounded(
  struct bkvoice_client_s *client,
  const struct bkvoice_rpc_request_s *request)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKVOICE_RPC_SEND_WAIT_MS);
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
          ret = rpmsg_trysend(&client->endpoint, request,
                              sizeof(*request));
        }

      nxmutex_unlock(&client->endpoint_lock);
      if (ret >= 0)
        {
          return OK;
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

static int bkvoice_wait_reply(
  struct bkvoice_client_s *client,
  const struct bkvoice_rpc_request_s *request,
  struct bkvoice_rpc_response_s *response,
  unsigned int timeout_ms)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(timeout_ms);

  for (; ; )
    {
      irqstate_t flags;
      clock_t elapsed;
      bool valid;
      int ret;

      flags = spin_lock_irqsave(&client->reply_lock);
      valid = client->reply_valid &&
              client->reply.session == request->session &&
              client->reply.sequence == request->sequence;
      if (valid)
        {
          memcpy(response, &client->reply, sizeof(*response));
          client->reply_valid = false;
        }

      spin_unlock_irqrestore(&client->reply_lock, flags);
      if (valid)
        {
          return OK;
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

static int bkvoice_client_cb(struct rpmsg_endpoint *endpoint, void *data,
                             size_t len, uint32_t src, void *priv)
{
  struct bkvoice_client_s *client = priv;
  const struct bkvoice_rpc_response_s *response = data;
  irqstate_t flags;
  bool matched;

  (void)endpoint;
  (void)src;

  if (response == NULL || len != sizeof(*response) ||
      response->magic != BKVOICE_RPC_MAGIC ||
      response->version != BKVOICE_RPC_VERSION ||
      response->command != BKVOICE_RPC_RESPONSE)
    {
      return -ENOMSG;
    }

  flags = spin_lock_irqsave(&client->reply_lock);
  matched = client->waiting_session != 0 &&
            response->session == client->waiting_session &&
            response->sequence == client->waiting_sequence;
  if (matched)
    {
      memcpy(&client->reply, response, sizeof(client->reply));
      client->reply_valid = true;
    }

  spin_unlock_irqrestore(&client->reply_lock, flags);
  return matched ? nxsem_post(&client->reply_sem) : -ENOMSG;
}

static void bkvoice_device_created(struct rpmsg_device *rdev, void *priv)
{
  struct bkvoice_client_s *client = priv;
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
      ret = rpmsg_create_ept(&client->endpoint, rdev,
                             BKVOICE_RPC_ENDPOINT,
                             RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                             bkvoice_client_cb, NULL);
      __atomic_store_n(&client->connection_error, ret, __ATOMIC_RELEASE);
      if (ret >= 0)
        {
          __atomic_store_n(&client->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&client->endpoint_lock);
}

static void bkvoice_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  struct bkvoice_client_s *client = priv;
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

      nxmutex_unlock(&client->endpoint_lock);
    }

  flags = spin_lock_irqsave(&client->reply_lock);
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);
  (void)nxsem_post(&client->reply_sem);
}

int bkvoice_rpc_client_initialize(void)
{
  struct bkvoice_client_s *client = &g_bkvoice_client;
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
      return OK;
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
      client->session = (uint32_t)clock_systime_ticks() ^
                        (uint32_t)(uintptr_t)client;
      if (client->session == 0)
        {
          client->session = 1;
        }

      ret = rpmsg_register_callback(client, bkvoice_device_created,
                                    bkvoice_device_destroy, NULL, NULL);
    }

  if (ret >= 0)
    {
      __atomic_store_n(&client->initialized, true, __ATOMIC_RELEASE);
    }
  else
    {
      if (semaphore_initialized)
        {
          (void)nxsem_destroy(&client->reply_sem);
        }
    }

  nxmutex_unlock(&client->init_lock);
  return ret;
}

int bkvoice_rpc_exchange(struct bkvoice_rpc_request_s *request,
                         struct bkvoice_rpc_response_s *response,
                         unsigned int timeout_ms)
{
  struct bkvoice_client_s *client = &g_bkvoice_client;
  irqstate_t flags;
  unsigned int attempt;
  int ret;

  if (request == NULL || response == NULL || timeout_ms == 0)
    {
      return -EINVAL;
    }

  ret = bkvoice_rpc_client_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&client->request_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_wait_endpoint(client);
  if (ret < 0)
    {
      goto out;
    }

  if (++client->sequence == 0)
    {
      client->sequence++;
    }

  request->magic = BKVOICE_RPC_MAGIC;
  request->version = BKVOICE_RPC_VERSION;
  request->session = client->session;
  request->sequence = client->sequence;

  bkvoice_flush_sem(&client->reply_sem);
  flags = spin_lock_irqsave(&client->reply_lock);
  client->waiting_session = request->session;
  client->waiting_sequence = request->sequence;
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);

  ret = -ETIMEDOUT;
  for (attempt = 0; attempt < BKVOICE_RPC_ATTEMPTS; attempt++)
    {
      ret = bkvoice_send_bounded(client, request);
      if (ret < 0)
        {
          break;
        }

      ret = bkvoice_wait_reply(client, request, response, timeout_ms);
      if (ret != -ETIMEDOUT)
        {
          break;
        }
    }

  flags = spin_lock_irqsave(&client->reply_lock);
  client->waiting_session = 0;
  client->waiting_sequence = 0;
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);

out:
  nxmutex_unlock(&client->request_lock);
  return ret;
}

#endif /* CONFIG_BK7258_APP_VOICE */
