/****************************************************************************
 * app/bk7258/bk7258_prov_service.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-side service for the BKPROV identity-supply protocol.  The AP product
 * owns the on-chip filesystem worker, so the CP console command forwards the
 * bounded BPI1 record here instead of writing the store from the other core.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_PROVISION_GATT

#include "bk7258_prov_rpc.h"
#include "bk7258_provision_identity.h"
#include "bk7258_provision_storage.h"

#include <errno.h>
#include <mbedtls/platform_util.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

struct bkprov_server_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t endpoint_lock;
  spinlock_t lock;
  sem_t sem;
  volatile bool endpoint_created;
  volatile bool connected;
  bool pending;
  uint32_t epoch;
  uint32_t session;
  uint32_t total;
  size_t received;
  struct bkprov_rpc_frame_s request;
  uint8_t record[BKPROV_RPC_RECORD_MAX];
};

static struct bkprov_server_s g_bkprov =
{
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .lock = SP_UNLOCKED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bkprov_send_answer(struct bkprov_server_s *s,
                               const struct bkprov_rpc_frame_s *frame,
                               int status, uint32_t accepted)
{
  struct bkprov_rpc_answer_s answer;

  bkprov_rpc_make_answer(&answer, frame, status);
  answer.accepted = accepted;
  if (nxmutex_lock(&s->endpoint_lock) >= 0)
    {
      if (s->endpoint_created && s->connected &&
          is_rpmsg_ept_ready(&s->endpoint))
        {
          (void)rpmsg_trysend(&s->endpoint, &answer, sizeof(answer));
        }

      nxmutex_unlock(&s->endpoint_lock);
    }
}

static int bkprov_handle_frame(struct bkprov_server_s *s,
                               const struct bkprov_rpc_frame_s *frame,
                               uint32_t *accepted)
{
  struct bkprov_identity_s probe;
  int ret;

  *accepted = (uint32_t)s->received;
  switch (frame->command)
    {
      case BKPROV_RPC_BEGIN:
        mbedtls_platform_zeroize(s->record, sizeof(s->record));
        s->session = frame->session;
        s->total = frame->total;
        s->received = 0;
        *accepted = 0;
        return 0;

      case BKPROV_RPC_DATA:
        if (s->session != frame->session || s->total != frame->total ||
            s->received != frame->offset ||
            frame->offset + frame->length > s->total)
          {
            return -EPROTO;
          }

        memcpy(s->record + frame->offset, frame->data, frame->length);
        s->received += frame->length;
        *accepted = (uint32_t)s->received;
        return 0;

      case BKPROV_RPC_COMMIT:
        if (s->session != frame->session || s->total != frame->total ||
            s->received != s->total || s->total == 0)
          {
            return -EPROTO;
          }

        /* Only a parsed BPI1 record may reach the store; the probe identity
         * is discarded immediately and the record bytes stay in RAM.
         */

        memset(&probe, 0, sizeof(probe));
        ret = bkprov_identity_load(&probe, s->record, s->total);
        bkprov_identity_clear(&probe);
        if (ret < 0)
          {
            return ret;
          }

        ret = bkprov_storage_identity_install(s->record, s->total);
        if (ret == 0)
          {
            mbedtls_platform_zeroize(s->record, sizeof(s->record));
            s->session = 0;
            s->total = 0;
            s->received = 0;
            *accepted = 0;
          }

        return ret;

      case BKPROV_RPC_STATUS:
        if (s->total != 0)
          {
            return -EBUSY;
          }

        ret = bkprov_storage_identity(s->record, sizeof(s->record),
                                      &s->received);
        *accepted = ret < 0 ? 0 : (uint32_t)s->received;
        mbedtls_platform_zeroize(s->record, sizeof(s->record));
        s->received = 0;
        return ret;

      default:
        return -EINVAL;
    }
}

static int bkprov_worker(int argc, char *argv[])
{
  struct bkprov_server_s *s = &g_bkprov;

  (void)argc;
  (void)argv;
  for (; ; )
    {
      struct bkprov_rpc_frame_s frame;
      irqstate_t flags;
      uint32_t accepted = 0;
      int ret;

      while (nxsem_wait_uninterruptible(&s->sem) < 0)
        {
        }

      flags = spin_lock_irqsave(&s->lock);
      if (!s->pending)
        {
          spin_unlock_irqrestore(&s->lock, flags);
          continue;
        }

      frame = s->request;
      s->pending = false;
      spin_unlock_irqrestore(&s->lock, flags);

      ret = bkprov_rpc_frame_valid(&frame) ?
            bkprov_handle_frame(s, &frame, &accepted) : -EINVAL;
      bkprov_send_answer(s, &frame, ret, accepted);
      memset(&frame, 0, sizeof(frame));
    }

  return 0;
}

static int bkprov_server_cb(struct rpmsg_endpoint *endpoint, void *data,
                            size_t len, uint32_t src, void *priv)
{
  struct bkprov_server_s *s = priv;
  irqstate_t flags;

  (void)endpoint;
  (void)src;
  if (data == NULL || len != sizeof(struct bkprov_rpc_frame_s))
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&s->lock);
  if (!s->connected || !s->endpoint_created || s->pending)
    {
      spin_unlock_irqrestore(&s->lock, flags);
      return -EBUSY;
    }

  memcpy(&s->request, data, sizeof(s->request));
  s->pending = true;
  spin_unlock_irqrestore(&s->lock, flags);
  return nxsem_post(&s->sem);
}

static void bkprov_disconnect(struct bkprov_server_s *s)
{
  irqstate_t flags = spin_lock_irqsave(&s->lock);

  s->connected = false;
  s->epoch++;
  s->pending = false;
  s->session = 0;
  s->total = 0;
  s->received = 0;
  mbedtls_platform_zeroize(s->record, sizeof(s->record));
  spin_unlock_irqrestore(&s->lock, flags);

  if (nxmutex_lock(&s->endpoint_lock) >= 0)
    {
      if (s->endpoint_created)
        {
          s->endpoint_created = false;
          rpmsg_destroy_ept(&s->endpoint);
        }

      nxmutex_unlock(&s->endpoint_lock);
    }
}

static void bkprov_unbind(struct rpmsg_endpoint *endpoint)
{
  bkprov_disconnect(endpoint->priv);
}

static bool bkprov_ns_match(struct rpmsg_device *rdev, void *priv,
                            const char *name, uint32_t dest)
{
  const char *cpu = rpmsg_get_cpuname(rdev);

  (void)priv;
  (void)dest;
  return cpu && strcmp(cpu, "cp") == 0 &&
         strcmp(name, BKPROV_RPC_ENDPOINT) == 0;
}

static void bkprov_ns_bind(struct rpmsg_device *rdev, void *priv,
                           const char *name, uint32_t dest)
{
  struct bkprov_server_s *s = priv;
  irqstate_t flags;

  if (nxmutex_lock(&s->endpoint_lock) < 0)
    {
      return;
    }

  if (!s->endpoint_created)
    {
      s->endpoint.priv = s;
      if (rpmsg_create_ept(&s->endpoint, rdev, name, RPMSG_ADDR_ANY, dest,
                           bkprov_server_cb, bkprov_unbind) >= 0)
        {
          s->endpoint_created = true;
          flags = spin_lock_irqsave(&s->lock);
          s->connected = true;
          spin_unlock_irqrestore(&s->lock, flags);
        }
    }

  nxmutex_unlock(&s->endpoint_lock);
}

static void bkprov_device_destroy(struct rpmsg_device *rdev, void *priv)
{
  const char *cpu = rpmsg_get_cpuname(rdev);

  if (cpu && strcmp(cpu, "cp") == 0)
    {
      bkprov_disconnect(priv);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkprov_service_initialize(void)
{
  struct bkprov_server_s *s = &g_bkprov;
  static bool initialized;
  bool sem_ready = false;
  bool registered = false;
  pid_t pid;
  int ret;

  if (initialized)
    {
      return 0;
    }

  ret = nxsem_init(&s->sem, 0, 0);
  sem_ready = ret >= 0;
#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&s->sem, SEM_PRIO_NONE);
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(s, NULL, bkprov_device_destroy,
                                    bkprov_ns_match, bkprov_ns_bind);
      registered = ret >= 0;
    }

  if (ret >= 0)
    {
      pid = task_create("bkprov-rpc",
                        CONFIG_BK7258_PROVISION_SUPPLY_PRIORITY,
                        CONFIG_BK7258_PROVISION_SUPPLY_STACKSIZE,
                        bkprov_worker, NULL);
      if (pid < 0)
        {
          ret = -errno;
        }
    }

  if (ret >= 0)
    {
      initialized = true;
      syslog(LOG_INFO, "BKPROV SERVICE READY endpoint=%s\n",
             BKPROV_RPC_ENDPOINT);
    }
  else
    {
      if (registered)
        {
          rpmsg_unregister_callback(s, NULL, bkprov_device_destroy,
                                    bkprov_ns_match, bkprov_ns_bind);
        }

      if (sem_ready)
        {
          nxsem_destroy(&s->sem);
        }
    }

  return ret;
}

#endif /* CONFIG_BK7258_PROVISION_GATT */
