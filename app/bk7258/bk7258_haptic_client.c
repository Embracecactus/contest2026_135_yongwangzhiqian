/****************************************************************************
 * app/bk7258/bk7258_haptic_client.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-side client for the application-owned BKHAPTIC RPMsg protocol.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_HAPTIC

#include "bk7258_haptic_protocol.h"

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

struct bkhaptic_client_s
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
  uint32_t epoch;
  int reply_error;
  uint16_t waiting_command;
  uint32_t waiting_duration;
  uint32_t session;
  uint32_t sequence;
  uint32_t waiting_session;
  uint32_t waiting_sequence;
  struct bkhaptic_rpc_response_s reply;
};

static struct bkhaptic_client_s g_bkhaptic_client =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = NXMUTEX_INITIALIZER,
  .reply_lock = SP_UNLOCKED,
};

static void bkhaptic_flush_sem(sem_t *sem)
{
  while (nxsem_trywait(sem) == 0)
    {
    }
}

static bool bkhaptic_endpoint_ready(struct bkhaptic_client_s *client)
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

static int bkhaptic_wait_endpoint(struct bkhaptic_client_s *client)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKHAPTIC_RPC_ENDPOINT_WAIT_MS);
  int ret;

  do
    {
      if (bkhaptic_endpoint_ready(client))
        {
          return 0;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  ret = __atomic_load_n(&client->connection_error, __ATOMIC_ACQUIRE);
  return ret < 0 ? ret : -ETIMEDOUT;
}

static int bkhaptic_send_bounded(
  struct bkhaptic_client_s *client,
  const struct bkhaptic_rpc_request_s *request, uint32_t epoch)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKHAPTIC_RPC_SEND_WAIT_MS);
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
        }

      nxmutex_unlock(&client->endpoint_lock);
      if (ret >= 0)
        {
          if (__atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE) != epoch)
            {
              return -ENOTCONN;
            }

          /* A new request may use an endpoint rebound after namespace
           * unbind; an old exchange is rejected by its epoch above.
           */

          __atomic_store_n(&client->connection_error, 0, __ATOMIC_RELEASE);
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

static int bkhaptic_wait_reply(
  struct bkhaptic_client_s *client,
  const struct bkhaptic_rpc_request_s *request,
  struct bkhaptic_rpc_response_s *response,
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
      ret = client->reply_error;
      valid = client->reply_valid &&
              client->reply.session == request->session &&
              client->reply.sequence == request->sequence;
      if (valid)
        {
          memcpy(response, &client->reply, sizeof(*response));
          client->reply_valid = false;
        }

      spin_unlock_irqrestore(&client->reply_lock, flags);
      if (ret < 0)
        {
          return ret;
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
          return ret;
        }
    }
}

static int bkhaptic_client_cb(struct rpmsg_endpoint *endpoint, void *data,
                              size_t len, uint32_t src, void *priv)
{
  struct bkhaptic_client_s *client = priv;
  struct bkhaptic_rpc_response_s response;
  irqstate_t flags;
  bool waiting;

  (void)endpoint;
  (void)src;
  if (data == NULL || len != sizeof(response))
    {
      return -ENOMSG;
    }

  memcpy(&response, data, sizeof(response));
  flags = spin_lock_irqsave(&client->reply_lock);
  waiting = client->waiting_session != 0;
  if (waiting)
    {
      if (!bkhaptic_rpc_response_valid(&response) ||
          response.session != client->waiting_session ||
          response.sequence != client->waiting_sequence ||
          response.command != (client->waiting_command |
                               BKHAPTIC_RPC_RESPONSE) ||
          (response.status == 0 &&
           response.accepted_ms != client->waiting_duration))
        {
          client->reply_error = -EPROTO;
        }
      else
        {
          client->reply = response;
          client->reply_valid = true;
        }
    }

  spin_unlock_irqrestore(&client->reply_lock, flags);
  return waiting ? nxsem_post(&client->reply_sem) : -ENOMSG;
}

static void bkhaptic_client_unbind(struct rpmsg_endpoint *endpoint)
{
  struct bkhaptic_client_s *client = endpoint->priv;

  __atomic_add_fetch(&client->epoch, 1, __ATOMIC_ACQ_REL);
  __atomic_store_n(&client->connection_error, -ENOTCONN, __ATOMIC_RELEASE);
  (void)nxsem_post(&client->reply_sem);
}

static void bkhaptic_device_created(struct rpmsg_device *rdev, void *priv)
{
  struct bkhaptic_client_s *client = priv;
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
                             BKHAPTIC_RPC_ENDPOINT,
                             RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                             bkhaptic_client_cb, bkhaptic_client_unbind);
      __atomic_store_n(&client->connection_error, ret, __ATOMIC_RELEASE);
      if (ret >= 0)
        {
          __atomic_store_n(&client->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&client->endpoint_lock);
}

static void bkhaptic_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  struct bkhaptic_client_s *client = priv;
  const char *cpuname = rpmsg_get_cpuname(rdev);
  irqstate_t flags;

  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }

  __atomic_add_fetch(&client->epoch, 1, __ATOMIC_ACQ_REL);
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

int bkhaptic_rpc_client_initialize(void)
{
  struct bkhaptic_client_s *client = &g_bkhaptic_client;
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
                                    bkhaptic_device_created,
                                    bkhaptic_device_destroy,
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

int bkhaptic_rpc_exchange(struct bkhaptic_rpc_request_s *request,
                          struct bkhaptic_rpc_response_s *response,
                          unsigned int timeout_ms)
{
  struct bkhaptic_client_s *client = &g_bkhaptic_client;
  irqstate_t flags;
  unsigned int attempt;
  uint32_t epoch;
  int ret;

  if (request == NULL || response == NULL || timeout_ms == 0)
    {
      return -EINVAL;
    }

  ret = bkhaptic_rpc_client_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&client->request_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkhaptic_wait_endpoint(client);
  if (ret < 0)
    {
      goto out;
    }

  epoch = __atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE);
  if (client->sequence == UINT32_MAX)
    {
      ret = -EOVERFLOW;
      goto out;
    }

  client->sequence++;

  request->magic = BKHAPTIC_RPC_MAGIC;
  request->version = BKHAPTIC_RPC_VERSION;
  request->session = client->session;
  request->sequence = client->sequence;
  request->reserved[0] = 0;
  request->reserved[1] = 0;

  bkhaptic_flush_sem(&client->reply_sem);
  flags = spin_lock_irqsave(&client->reply_lock);
  client->waiting_session = request->session;
  client->waiting_sequence = request->sequence;
  client->waiting_command = request->command;
  client->waiting_duration = request->duration_ms;
  client->reply_error = 0;
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);

  ret = -ETIMEDOUT;
  for (attempt = 0; attempt < BKHAPTIC_RPC_ATTEMPTS; attempt++)
    {
      ret = bkhaptic_send_bounded(client, request, epoch);
      if (ret < 0)
        {
          break;
        }

      ret = bkhaptic_wait_reply(client, request, response, timeout_ms,
                                epoch);
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

#endif /* CONFIG_BK7258_APP_HAPTIC */
