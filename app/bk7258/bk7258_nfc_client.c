/****************************************************************************
 * app/bk7258/bk7258_nfc_client.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-side client for the application-owned BKNFC RPMsg protocol.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_NFC

#include "bk7258_nfc_protocol.h"
#include "bk7258_nfc_core.h"

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

struct bknfc_client_s
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
  uint32_t epoch; /* Changed on disconnect; an exchange never crosses it. */
  uint32_t session;
  uint32_t sequence;
  uint32_t waiting_session;
  uint32_t waiting_sequence;
  struct bknfc_rpc_response_s reply;
};

static struct bknfc_client_s g_bknfc_client =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = NXMUTEX_INITIALIZER,
  .reply_lock = SP_UNLOCKED,
};

static void bknfc_flush_sem(sem_t *sem)
{
  while (nxsem_trywait(sem) == 0)
    {
    }
}

static bool bknfc_endpoint_ready(struct bknfc_client_s *client,
                                    uint32_t *epoch)
{
  bool ready = false;

  if (nxmutex_lock(&client->endpoint_lock) >= 0)
    {
      ready = __atomic_load_n(&client->endpoint_created,
                              __ATOMIC_ACQUIRE) &&
              is_rpmsg_ept_ready(&client->endpoint);
      if (ready)
        {
          *epoch = __atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE);
        }

      nxmutex_unlock(&client->endpoint_lock);
    }

  return ready;
}

static int bknfc_wait_endpoint(struct bknfc_client_s *client,
                                  uint32_t *epoch)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKNFC_RPC_ENDPOINT_WAIT_MS);
  int ret;

  do
    {
      if (bknfc_endpoint_ready(client, epoch))
        {
          return 0;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  ret = __atomic_load_n(&client->connection_error, __ATOMIC_ACQUIRE);
  return ret < 0 ? ret : -ETIMEDOUT;
}

static int bknfc_send_bounded(
  struct bknfc_client_s *client,
  const struct bknfc_rpc_request_s *request, uint32_t epoch)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKNFC_RPC_SEND_WAIT_MS);
  int ret = -ENOTCONN;

  do
    {
      ret = nxmutex_lock(&client->endpoint_lock);
      if (ret < 0)
        {
          return ret;
        }

      if (__atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE) != epoch ||
          !__atomic_load_n(&client->endpoint_created, __ATOMIC_ACQUIRE) ||
          !is_rpmsg_ept_ready(&client->endpoint))
        {
          ret = -ENOTCONN;
        }
      else
        {
          ret = rpmsg_trysend(&client->endpoint, request,
                              sizeof(*request));
          if (ret >= 0)
            {
              __atomic_store_n(&client->connection_error, 0,
                               __ATOMIC_RELEASE);
            }
        }

      nxmutex_unlock(&client->endpoint_lock);
      if (__atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE) != epoch)
        {
          return -ENOTCONN;
        }

      if (ret >= 0)
        {
          return 0;
        }

      if (ret != RPMSG_ERR_NO_BUFF && ret != -ENOMEM && ret != -EAGAIN)
        {
          return ret;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  return -ETIMEDOUT;
}

static int bknfc_wait_reply(
  struct bknfc_client_s *client,
  const struct bknfc_rpc_request_s *request,
  struct bknfc_rpc_response_s *response,
  unsigned int timeout_ms, uint32_t epoch)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(timeout_ms);

  for (; ; )
    {
      irqstate_t flags;
      clock_t elapsed;
      bool valid;
      int ret;

      if (__atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE) != epoch)
        {
          return -ENOTCONN;
        }

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
      if (__atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE) != epoch)
        {
          return -ENOTCONN;
        }

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
          return __atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE) != epoch ?
                 -ENOTCONN : ret;
        }
    }
}

static int bknfc_client_cb(struct rpmsg_endpoint *endpoint, void *data,
                              size_t len, uint32_t src, void *priv)
{
  struct bknfc_client_s *client = priv;
  const struct bknfc_rpc_response_s *response = data;
  irqstate_t flags;
  bool matched;

  (void)endpoint;
  (void)src;

  if (response == NULL || len != sizeof(*response) ||
      response->magic != BKNFC_RPC_MAGIC ||
      response->version != BKNFC_RPC_VERSION ||
      !bknfc_rpc_response_valid(response))
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

/* Called with endpoint_lock held.  Keep the failure attached to the old
 * exchange even if the same endpoint is rebound before its waiter runs.
 */

static void bknfc_connection_lost(struct bknfc_client_s *client)
{
  irqstate_t flags;

  __atomic_add_fetch(&client->epoch, 1u, __ATOMIC_RELEASE);
  __atomic_store_n(&client->connection_error, -ENOTCONN, __ATOMIC_RELEASE);
  flags = spin_lock_irqsave(&client->reply_lock);
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);
  (void)nxsem_post(&client->reply_sem);
}

static void bknfc_client_unbind(struct rpmsg_endpoint *endpoint)
{
  struct bknfc_client_s *client = endpoint->priv;

  if (nxmutex_lock(&client->endpoint_lock) >= 0)
    {
      bknfc_connection_lost(client);
      nxmutex_unlock(&client->endpoint_lock);
    }
}

static void bknfc_device_created(struct rpmsg_device *rdev, void *priv)
{
  struct bknfc_client_s *client = priv;
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
                             BKNFC_RPC_ENDPOINT,
                             RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                             bknfc_client_cb, bknfc_client_unbind);
      __atomic_store_n(&client->connection_error, ret, __ATOMIC_RELEASE);
      if (ret >= 0)
        {
          __atomic_store_n(&client->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&client->endpoint_lock);
}

static void bknfc_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  struct bknfc_client_s *client = priv;
  const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }

  if (nxmutex_lock(&client->endpoint_lock) >= 0)
    {
      bknfc_connection_lost(client);
      __atomic_store_n(&client->endpoint_created, false, __ATOMIC_RELEASE);
      if (client->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&client->endpoint);
        }

      memset(&client->endpoint, 0, sizeof(client->endpoint));
      nxmutex_unlock(&client->endpoint_lock);
    }
}

int bknfc_rpc_client_initialize(void)
{
  struct bknfc_client_s *client = &g_bknfc_client;
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
      client->session = (uint32_t)clock_systime_ticks() ^
                        (uint32_t)(uintptr_t)client;
      if (client->session == 0)
        {
          client->session = 1;
        }

      ret = rpmsg_register_callback(client,
                                    bknfc_device_created,
                                    bknfc_device_destroy,
                                    NULL, NULL);
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

int bknfc_rpc_exchange(struct bknfc_rpc_request_s *request,
                          struct bknfc_rpc_response_s *response,
                          unsigned int timeout_ms)
{
  struct bknfc_client_s *client = &g_bknfc_client;
  irqstate_t flags;
  unsigned int attempt;
  uint32_t epoch;
  int ret;

  if (request == NULL || response == NULL || timeout_ms == 0)
    {
      return -EINVAL;
    }

  ret = bknfc_rpc_client_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&client->request_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bknfc_wait_endpoint(client, &epoch);
  if (ret < 0)
    {
      goto out;
    }

  if (++client->sequence == 0)
    {
      client->sequence++;
    }

  request->magic = BKNFC_RPC_MAGIC;
  request->version = BKNFC_RPC_VERSION;
  request->session = client->session;
  request->sequence = client->sequence;
  request->reserved[0] = 0;
  request->reserved[1] = 0;

  bknfc_flush_sem(&client->reply_sem);
  flags = spin_lock_irqsave(&client->reply_lock);
  client->waiting_session = request->session;
  client->waiting_sequence = request->sequence;
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);

  ret = -ETIMEDOUT;
  for (attempt = 0; attempt < BKNFC_RPC_ATTEMPTS; attempt++)
    {
      ret = bknfc_send_bounded(client, request, epoch);
      if (ret < 0)
        {
          break;
        }

      ret = bknfc_wait_reply(client, request, response, timeout_ms, epoch);
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

#endif /* CONFIG_BK7258_APP_NFC */
