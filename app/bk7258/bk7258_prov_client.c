/****************************************************************************
 * app/bk7258/bk7258_prov_client.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-side client for the application-owned BKPROV identity-supply protocol.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_APP_PROV

#include "bk7258_prov_rpc.h"

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

struct bkprov_client_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  mutex_t request_lock;
  spinlock_t reply_lock;
  sem_t reply_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  volatile int connection_error;
  uint32_t epoch;
  volatile bool reply_valid;
  uint16_t waiting_command;
  uint32_t waiting_session;
  uint32_t waiting_sequence;
  uint32_t session;
  uint32_t sequence;
  struct bkprov_rpc_answer_s reply;
};

static struct bkprov_client_s g_bkprov_client =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .request_lock = NXMUTEX_INITIALIZER,
  .reply_lock = SP_UNLOCKED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bkprov_endpoint_ready(struct bkprov_client_s *client)
{
  bool ready = false;

  if (nxmutex_lock(&client->endpoint_lock) >= 0)
    {
      ready = __atomic_load_n(&client->endpoint_created, __ATOMIC_ACQUIRE) &&
              is_rpmsg_ept_ready(&client->endpoint);
      nxmutex_unlock(&client->endpoint_lock);
    }

  return ready;
}

static int bkprov_wait_endpoint(struct bkprov_client_s *client)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKPROV_RPC_ENDPOINT_WAIT_MS);
  int ret;

  do
    {
      if (bkprov_endpoint_ready(client))
        {
          return 0;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  ret = __atomic_load_n(&client->connection_error, __ATOMIC_ACQUIRE);
  return ret < 0 ? ret : -ETIMEDOUT;
}

static int bkprov_send_bounded(struct bkprov_client_s *client,
                               const struct bkprov_rpc_frame_s *frame,
                               uint32_t epoch)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BKPROV_RPC_SEND_WAIT_MS);
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
          ret = rpmsg_trysend(&client->endpoint, frame, sizeof(*frame));
        }

      nxmutex_unlock(&client->endpoint_lock);
      if (ret >= 0)
        {
          if (__atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE) != epoch)
            {
              return -ENOTCONN;
            }

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

static int bkprov_wait_reply(struct bkprov_client_s *client,
                             const struct bkprov_rpc_frame_s *frame,
                             struct bkprov_rpc_answer_s *answer,
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
              client->reply.session == frame->session &&
              client->reply.sequence == frame->sequence;
      if (valid)
        {
          memcpy(answer, &client->reply, sizeof(*answer));
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

static int bkprov_client_cb(struct rpmsg_endpoint *endpoint, void *data,
                            size_t len, uint32_t src, void *priv)
{
  struct bkprov_client_s *client = priv;
  struct bkprov_rpc_answer_s answer;
  irqstate_t flags;
  bool waiting;

  (void)endpoint;
  (void)src;
  if (data == NULL || len != sizeof(answer))
    {
      return -ENOMSG;
    }

  memcpy(&answer, data, sizeof(answer));
  flags = spin_lock_irqsave(&client->reply_lock);
  waiting = client->waiting_session != 0;
  if (waiting)
    {
      if (!bkprov_rpc_answer_valid(&answer) ||
          answer.session != client->waiting_session ||
          answer.sequence != client->waiting_sequence ||
          answer.command != (client->waiting_command | BKPROV_RPC_RESPONSE))
        {
          client->reply_valid = false;
        }
      else
        {
          client->reply = answer;
          client->reply_valid = true;
        }
    }

  spin_unlock_irqrestore(&client->reply_lock, flags);
  return waiting ? nxsem_post(&client->reply_sem) : -ENOMSG;
}

static void bkprov_client_unbind(struct rpmsg_endpoint *endpoint)
{
  struct bkprov_client_s *client = endpoint->priv;

  __atomic_add_fetch(&client->epoch, 1, __ATOMIC_ACQ_REL);
  __atomic_store_n(&client->connection_error, -ENOTCONN, __ATOMIC_RELEASE);
  (void)nxsem_post(&client->reply_sem);
}

static void bkprov_device_created(struct rpmsg_device *rdev, void *priv)
{
  struct bkprov_client_s *client = priv;
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
      ret = rpmsg_create_ept(&client->endpoint, rdev, BKPROV_RPC_ENDPOINT,
                             RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                             bkprov_client_cb, bkprov_client_unbind);
      __atomic_store_n(&client->connection_error, ret, __ATOMIC_RELEASE);
      if (ret >= 0)
        {
          __atomic_store_n(&client->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&client->endpoint_lock);
}

static void bkprov_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  struct bkprov_client_s *client = priv;
  const char *cpuname = rpmsg_get_cpuname(rdev);
  irqstate_t flags;

  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }

  __atomic_add_fetch(&client->epoch, 1, __ATOMIC_ACQ_REL);
  __atomic_store_n(&client->endpoint_created, false, __ATOMIC_RELEASE);
  __atomic_store_n(&client->connection_error, -ENOTCONN, __ATOMIC_RELEASE);
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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkprov_rpc_client_initialize(void)
{
  struct bkprov_client_s *client = &g_bkprov_client;
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

  __atomic_store_n(&client->connection_error, -ENOTCONN, __ATOMIC_RELEASE);
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

      ret = rpmsg_register_callback(client, bkprov_device_created,
                                    bkprov_device_destroy, NULL, NULL);
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

int bkprov_rpc_exchange(struct bkprov_rpc_frame_s *frame,
                        struct bkprov_rpc_answer_s *answer,
                        unsigned int timeout_ms)
{
  struct bkprov_client_s *client = &g_bkprov_client;
  irqstate_t flags;
  uint32_t epoch;
  int ret;

  if (frame == NULL || answer == NULL || timeout_ms == 0)
    {
      return -EINVAL;
    }

  ret = bkprov_rpc_client_initialize();
  if (ret < 0)
    {
      return ret;
    }

  ret = nxmutex_lock(&client->request_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkprov_wait_endpoint(client);
  if (ret < 0)
    {
      goto out;
    }

  epoch = __atomic_load_n(&client->epoch, __ATOMIC_ACQUIRE);
  if (client->sequence == UINT32_MAX)
    {
      client->sequence = 0;
    }

  frame->magic = BKPROV_RPC_MAGIC;
  frame->version = BKPROV_RPC_VERSION;
  frame->reserved = 0;
  frame->session = client->session;
  frame->sequence = ++client->sequence;
  if (frame->session == 0 || frame->sequence == 0)
    {
      ret = -EIO;
      goto out;
    }

  flags = spin_lock_irqsave(&client->reply_lock);
  client->waiting_command = frame->command;
  client->waiting_session = frame->session;
  client->waiting_sequence = frame->sequence;
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);

  ret = bkprov_send_bounded(client, frame, epoch);
  if (ret >= 0)
    {
      ret = bkprov_wait_reply(client, frame, answer, timeout_ms, epoch);
    }

  flags = spin_lock_irqsave(&client->reply_lock);
  client->waiting_command = 0;
  client->waiting_session = 0;
  client->waiting_sequence = 0;
  client->reply_valid = false;
  spin_unlock_irqrestore(&client->reply_lock, flags);

out:
  (void)nxmutex_unlock(&client->request_lock);
  return ret;
}

int bkprov_rpc_supply(const uint8_t *record, size_t size)
{
  struct bkprov_rpc_frame_s frame;
  struct bkprov_rpc_answer_s answer;
  clock_t start;
  clock_t limit;
  size_t offset = 0;
  int ret;

  if (record == NULL || size < BKPROV_RPC_RECORD_MIN ||
      size > BKPROV_RPC_RECORD_MAX)
    {
      return -EINVAL;
    }

  memset(&frame, 0, sizeof(frame));
  frame.command = BKPROV_RPC_BEGIN;
  frame.total = (uint32_t)size;
  ret = bkprov_rpc_exchange(&frame, &answer, BKPROV_RPC_REPLY_WAIT_MS);
  if (ret < 0)
    {
      return ret;
    }

  if (answer.status < 0)
    {
      return answer.status;
    }

  while (offset < size)
    {
      size_t chunk = size - offset;

      if (chunk > BKPROV_RPC_CHUNK_BYTES)
        {
          chunk = BKPROV_RPC_CHUNK_BYTES;
        }

      memset(&frame, 0, sizeof(frame));
      frame.command = BKPROV_RPC_DATA;
      frame.offset = (uint32_t)offset;
      frame.total = (uint32_t)size;
      frame.length = (uint16_t)chunk;
      memcpy(frame.data, record + offset, chunk);
      ret = bkprov_rpc_exchange(&frame, &answer, BKPROV_RPC_REPLY_WAIT_MS);
      memset(frame.data, 0, sizeof(frame.data));
      if (ret < 0)
        {
          return ret;
        }

      if (answer.status < 0)
        {
          return answer.status;
        }

      if (answer.accepted != offset + chunk)
        {
          return -EPROTO;
        }

      offset += chunk;
    }

  /* Each retry is a fresh sequence polling the same copied record; the AP
   * worker owns durable publication and never rewrites a committed record.
   */

  start = clock_systime_ticks();
  limit = MSEC2TICK(30000);
  for (; ; )
    {
      memset(&frame, 0, sizeof(frame));
      frame.command = BKPROV_RPC_COMMIT;
      frame.offset = (uint32_t)size;
      frame.total = (uint32_t)size;
      ret = bkprov_rpc_exchange(&frame, &answer, BKPROV_RPC_REPLY_WAIT_MS);
      if (ret < 0)
        {
          return ret;
        }

      if (answer.status != -EAGAIN)
        {
          return answer.status;
        }

      if ((clock_t)(clock_systime_ticks() - start) >= limit)
        {
          return -ETIMEDOUT;
        }

      nxsig_usleep(100000);
    }
}

int bkprov_rpc_status(size_t *size)
{
  struct bkprov_rpc_frame_s frame;
  struct bkprov_rpc_answer_s answer;
  int ret;

  if (size == NULL)
    {
      return -EINVAL;
    }

  memset(&frame, 0, sizeof(frame));
  frame.command = BKPROV_RPC_STATUS;
  ret = bkprov_rpc_exchange(&frame, &answer, BKPROV_RPC_REPLY_WAIT_MS);
  if (ret < 0)
    {
      return ret;
    }

  if (answer.status == 0)
    {
      *size = answer.accepted;
    }

  return answer.status;
}

#endif /* CONFIG_BK7258_APP_PROV */
