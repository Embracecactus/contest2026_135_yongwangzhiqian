/****************************************************************************
 * board/bk7258/chip/cp/bk7258_temperature_server.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP owner for the BK7258 on-die temperature sensor.  The immutable SDK
 * one-shot path can wait up to three seconds while retrying SARADC reads, so
 * sampling runs on LPWORK rather than the RPTUN receive worker.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_TEMPERATURE

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <arch/chip/bk7258_rptun.h>
#include "bk7258_sdk_abi.h"
#include <arch/chip/bk7258_temperature.h>

#include <common/bk_err.h>

#include "bk7258_temperature_ipc.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_temperature_server_s
{
  struct rpmsg_endpoint ept;
  struct work_s sample_work;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  spinlock_t lock;
  volatile bool initialized;
  volatile bool endpoint_created;
  bool active;
  bool replay_valid;
  struct bk7258_temperature_wire_s active_request;
  struct bk7258_temperature_wire_s last_request;
  struct bk7258_temperature_wire_s last_reply;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_temperature_server_s g_bk7258_temperature_server =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .lock = SP_UNLOCKED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_temperature_same_request(
  const struct bk7258_temperature_wire_s *left,
  const struct bk7258_temperature_wire_s *right)
{
  return left->magic == right->magic && left->version == right->version &&
         left->command == right->command &&
         left->generation == right->generation &&
         left->sequence == right->sequence;
}

static int bk7258_temperature_map_error(int error)
{
  if (error == BK_OK)
    {
      return OK;
    }

  if (error == BK_ERR_NO_MEM)
    {
      return -ENOMEM;
    }

  if (error == BK_ERR_TRY_AGAIN)
    {
      return -EAGAIN;
    }

  return -EIO;
}

static void bk7258_temperature_make_reply(
  struct bk7258_temperature_wire_s *reply,
  const struct bk7258_temperature_wire_s *request, int status,
  uint32_t raw_code)
{
  memcpy(reply, request, sizeof(*reply));
  reply->command = BK7258_TEMPERATURE_COMMAND_RESPONSE;
  reply->status = status;
  reply->raw_code = raw_code;
  reply->flags = status >= 0 ? BK7258_TEMPERATURE_FLAG_RAW_VALID : 0;
  reply->reserved = 0;
}

static int bk7258_temperature_send(
  struct bk7258_temperature_server_s *priv,
  const struct bk7258_temperature_wire_s *message)
{
  int ret;

  ret = nxmutex_lock(&priv->endpoint_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!__atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) ||
      !is_rpmsg_ept_ready(&priv->ept))
    {
      ret = -ENOTCONN;
    }
  else
    {
      ret = rpmsg_trysend(&priv->ept, message, sizeof(*message));
    }

  nxmutex_unlock(&priv->endpoint_lock);
  return ret;
}

static int bk7258_temperature_send_status(
  struct bk7258_temperature_server_s *priv,
  const struct bk7258_temperature_wire_s *request, int status)
{
  struct bk7258_temperature_wire_s reply;

  bk7258_temperature_make_reply(&reply, request, status, 0);
  return bk7258_temperature_send(priv, &reply);
}

static void bk7258_temperature_sample_worker(void *arg)
{
  struct bk7258_temperature_server_s *priv = arg;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct bk7258_temperature_wire_s request;
  struct bk7258_temperature_wire_s reply;
  irqstate_t irqstate;
  uint32_t raw_code = 0;
  bool send_reply;
  int status;

  irqstate = spin_lock_irqsave(&priv->lock);
  memcpy(&request, &priv->active_request, sizeof(request));
  spin_unlock_irqrestore(&priv->lock, irqstate);

  status = bk7258_temperature_map_error(
             temp_detect_get_temperature(&raw_code));
  if (status >= 0 &&
      (raw_code < BK7258_TEMPERATURE_RAW_MIN ||
       raw_code > BK7258_TEMPERATURE_RAW_MAX))
    {
      status = -ERANGE;
    }

  bk7258_temperature_make_reply(&reply, &request, status, raw_code);

  irqstate = spin_lock_irqsave(&priv->lock);
  send_reply = __atomic_load_n(&priv->endpoint_created,
                               __ATOMIC_ACQUIRE) &&
               control->generation == request.generation;
  if (send_reply)
    {
      memcpy(&priv->last_request, &request, sizeof(request));
      memcpy(&priv->last_reply, &reply, sizeof(reply));
      priv->replay_valid = true;
    }

  priv->active = false;
  spin_unlock_irqrestore(&priv->lock, irqstate);

  /* Never wait for a TX buffer here.  A lost reply is recovered when AP
   * retries the same generation/sequence tuple and receives last_reply.
   */

  if (send_reply)
    {
      (void)bk7258_temperature_send(priv, &reply);
    }
}

static int bk7258_temperature_server_cb(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv_)
{
  struct bk7258_temperature_server_s *priv = priv_;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  const struct bk7258_temperature_wire_s *request = data;
  struct bk7258_temperature_wire_s replay;
  irqstate_t irqstate;
  bool send_replay = false;
  int ret;

  (void)ept;
  (void)src;

  if (request == NULL || len != sizeof(*request) ||
      request->magic != BK7258_TEMPERATURE_MAGIC ||
      request->version != BK7258_TEMPERATURE_VERSION ||
      request->command != BK7258_TEMPERATURE_COMMAND_READ ||
      request->generation == 0 || request->sequence == 0 ||
      request->generation != control->generation)
    {
      return -EINVAL;
    }

  irqstate = spin_lock_irqsave(&priv->lock);
  if (priv->replay_valid &&
      priv->last_request.generation != request->generation)
    {
      priv->replay_valid = false;
    }

  if (priv->replay_valid &&
      request->sequence == priv->last_request.sequence)
    {
      if (!bk7258_temperature_same_request(request, &priv->last_request))
        {
          spin_unlock_irqrestore(&priv->lock, irqstate);
          return bk7258_temperature_send_status(priv, request, -EPROTO);
        }

      memcpy(&replay, &priv->last_reply, sizeof(replay));
      send_replay = true;
    }
  else if (priv->active)
    {
      bool duplicate =
        bk7258_temperature_same_request(request, &priv->active_request);

      spin_unlock_irqrestore(&priv->lock, irqstate);
      return duplicate ? OK :
             bk7258_temperature_send_status(priv, request, -EBUSY);
    }
  else
    {
      memcpy(&priv->active_request, request,
             sizeof(priv->active_request));
      priv->active = true;
    }

  spin_unlock_irqrestore(&priv->lock, irqstate);

  if (send_replay)
    {
      return bk7258_temperature_send(priv, &replay);
    }

  ret = work_queue(LPWORK, &priv->sample_work,
                   bk7258_temperature_sample_worker, priv, 0);
  if (ret < 0)
    {
      irqstate = spin_lock_irqsave(&priv->lock);
      priv->active = false;
      spin_unlock_irqrestore(&priv->lock, irqstate);
      return bk7258_temperature_send_status(priv, request, ret);
    }

  return OK;
}

static void bk7258_temperature_device_created(FAR struct rpmsg_device *rdev,
                                              FAR void *priv_)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv_;
  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }
}

static bool bk7258_temperature_ns_match(FAR struct rpmsg_device *rdev,
                                        FAR void *priv_,
                                        FAR const char *name,
                                        uint32_t dest)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv_;
  (void)dest;
  return cpuname != NULL && strcmp(cpuname, "ap") == 0 &&
         strcmp(name, BK7258_TEMPERATURE_EPT_NAME) == 0;
}

static void bk7258_temperature_ns_bind(FAR struct rpmsg_device *rdev,
                                       FAR void *priv_,
                                       FAR const char *name,
                                       uint32_t dest)
{
  struct bk7258_temperature_server_s *priv = priv_;
  int ret;

  ret = nxmutex_lock(&priv->endpoint_lock);
  if (ret < 0)
    {
      return;
    }

  if (__atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&priv->endpoint_lock);
      return;
    }

  priv->ept.priv = priv;
  ret = rpmsg_create_ept(&priv->ept, rdev, name, RPMSG_ADDR_ANY, dest,
                         bk7258_temperature_server_cb, NULL);
  if (ret >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }

  nxmutex_unlock(&priv->endpoint_lock);
}

static void bk7258_temperature_device_destroy(FAR struct rpmsg_device *rdev,
                                              FAR void *priv_)
{
  struct bk7258_temperature_server_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);
  irqstate_t irqstate;
  int ret;

  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  ret = nxmutex_lock(&priv->endpoint_lock);
  if (ret >= 0)
    {
      if (priv->ept.rdev != NULL)
        {
          rpmsg_destroy_ept(&priv->ept);
        }

      nxmutex_unlock(&priv->endpoint_lock);
    }

  irqstate = spin_lock_irqsave(&priv->lock);
  priv->replay_valid = false;
  spin_unlock_irqrestore(&priv->lock, irqstate);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool bk7258_temperature_server_idle(void)
{
  struct bk7258_temperature_server_s *priv =
    &g_bk7258_temperature_server;
  irqstate_t irqstate;
  bool idle;

  irqstate = spin_lock_irqsave(&priv->lock);
  idle = !priv->active;
  spin_unlock_irqrestore(&priv->lock, irqstate);
  return idle;
}

int bk7258_temperature_initialize(void)
{
  struct bk7258_temperature_server_s *priv =
    &g_bk7258_temperature_server;
  int lockret;
  int ret;

  lockret = nxmutex_lock(&priv->init_lock);
  if (lockret < 0)
    {
      return lockret;
    }

  if (__atomic_load_n(&priv->initialized, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&priv->init_lock);
      return OK;
    }

  ret = rpmsg_register_callback(priv, bk7258_temperature_device_created,
                                bk7258_temperature_device_destroy,
                                bk7258_temperature_ns_match,
                                bk7258_temperature_ns_bind);
  if (ret < 0)
    {
      __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
    }
  else
    {
      __atomic_store_n(&priv->initialized, true, __ATOMIC_RELEASE);
    }

  nxmutex_unlock(&priv->init_lock);
  return ret;
}

#endif /* CONFIG_BK7258_TEMPERATURE */
