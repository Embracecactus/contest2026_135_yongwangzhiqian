/****************************************************************************
 * chips/bk7258/ap/bk7258_temperature_client.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP client for the CP-owned BK7258 on-die temperature sensor.
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

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>
#include <nuttx/thermal.h>

#include <arch/chip/bk7258_rptun.h>
#include <arch/chip/bk7258_temperature.h>

#include "bk7258_temperature_ipc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_BK7258_TEMPERATURE_FIXED_REFERENCE_RAW
#  define CONFIG_BK7258_TEMPERATURE_FIXED_REFERENCE_RAW 0
#endif

#ifndef CONFIG_BK7258_TEMPERATURE_POLL_INTERVAL_MS
#  define CONFIG_BK7258_TEMPERATURE_POLL_INTERVAL_MS 1000
#endif

#define BK7258_TEMPERATURE_BUSY_RETRY_US 10000u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_temperature_client_s
{
  struct rpmsg_endpoint ept;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  mutex_t lock;
  mutex_t zone_lock;
  spinlock_t reply_lock;
  sem_t reply_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  volatile bool reply_valid;
  volatile int connection_error;
  volatile uint32_t waiting_generation;
  volatile uint32_t waiting_sequence;
  uint32_t sequence;
  uint32_t reference_raw;
  struct bk7258_temperature_wire_s reply;
  FAR struct thermal_zone_device_s *zone;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_temperature_client_s g_bk7258_temperature_client =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
  .lock = NXMUTEX_INITIALIZER,
  .zone_lock = NXMUTEX_INITIALIZER,
  .reply_lock = SP_UNLOCKED,
};

static const struct thermal_zone_params_s g_bk7258_temperature_params =
{
  .gov_name = NULL,
  .passive_delay =
    MSEC2TICK(CONFIG_BK7258_TEMPERATURE_POLL_INTERVAL_MS),
  .polling_delay =
    MSEC2TICK(CONFIG_BK7258_TEMPERATURE_POLL_INTERVAL_MS),
  .trips = NULL,
  .num_trips = 0,
  .maps = NULL,
  .num_maps = 0,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_temperature_endpoint_ready(void)
{
  struct bk7258_temperature_client_s *priv =
    &g_bk7258_temperature_client;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  bool ready = false;
  int ret;

  ret = nxmutex_lock(&priv->endpoint_lock);
  if (ret >= 0)
    {
      __asm volatile ("dmb sy" ::: "memory");
      ready = __atomic_load_n(&priv->endpoint_created,
                              __ATOMIC_ACQUIRE) &&
              control->generation != 0 &&
              control->state == BK7258_RPTUN_STATE_CONNECTED &&
              is_rpmsg_ept_ready(&priv->ept);
      nxmutex_unlock(&priv->endpoint_lock);
    }

  return ready;
}

static void bk7258_temperature_flush_sem(sem_t *sem)
{
  while (nxsem_trywait(sem) == OK)
    {
    }
}

static int bk7258_temperature_wait_endpoint(
  struct bk7258_temperature_client_s *priv)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BK7258_TEMPERATURE_ENDPOINT_WAIT_MS);
  int ret;

  do
    {
      if (bk7258_temperature_endpoint_ready())
        {
          return OK;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  ret = __atomic_load_n(&priv->connection_error, __ATOMIC_ACQUIRE);

  return ret < 0 ? ret : -ETIMEDOUT;
}

static int bk7258_temperature_send_bounded(
  struct bk7258_temperature_client_s *priv,
  const struct bk7258_temperature_wire_s *request)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BK7258_TEMPERATURE_SEND_TIMEOUT_MS);
  int ret = -ENOTCONN;

  do
    {
      ret = nxmutex_lock(&priv->endpoint_lock);
      if (ret < 0)
        {
          return ret;
        }

      __asm volatile ("dmb sy" ::: "memory");
      if (!__atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) ||
          control->generation != request->generation ||
          control->state != BK7258_RPTUN_STATE_CONNECTED ||
          !is_rpmsg_ept_ready(&priv->ept))
        {
          ret = -ENOTCONN;
        }
      else
        {
          ret = rpmsg_trysend(&priv->ept, request, sizeof(*request));
        }

      nxmutex_unlock(&priv->endpoint_lock);
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

static int bk7258_temperature_wait_reply(
  struct bk7258_temperature_client_s *priv,
  const struct bk7258_temperature_wire_s *request,
  struct bk7258_temperature_wire_s *reply)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BK7258_TEMPERATURE_REPLY_TIMEOUT_MS);

  for (; ; )
    {
      irqstate_t irqstate;
      clock_t elapsed;
      bool valid;
      int ret;

      irqstate = spin_lock_irqsave(&priv->reply_lock);
      valid = priv->reply_valid &&
              priv->reply.generation == request->generation &&
              priv->reply.sequence == request->sequence;
      if (valid)
        {
          memcpy(reply, &priv->reply, sizeof(*reply));
          priv->reply_valid = false;
        }

      spin_unlock_irqrestore(&priv->reply_lock, irqstate);
      if (valid)
        {
          return OK;
        }

      ret = __atomic_load_n(&priv->connection_error, __ATOMIC_ACQUIRE);
      if (ret < 0)
        {
          return ret;
        }

      elapsed = clock_systime_ticks() - start;
      if (elapsed >= limit)
        {
          return -ETIMEDOUT;
        }

      ret = nxsem_tickwait_uninterruptible(&priv->reply_sem,
                                           limit - elapsed);
      if (ret < 0)
        {
          return ret;
        }
    }
}

static int bk7258_temperature_exchange(
  struct bk7258_temperature_client_s *priv,
  const struct bk7258_temperature_wire_s *request,
  struct bk7258_temperature_wire_s *reply)
{
  irqstate_t irqstate;
  unsigned int attempt;
  int ret = -ETIMEDOUT;

  bk7258_temperature_flush_sem(&priv->reply_sem);
  irqstate = spin_lock_irqsave(&priv->reply_lock);
  priv->waiting_generation = request->generation;
  priv->waiting_sequence = request->sequence;
  priv->reply_valid = false;
  spin_unlock_irqrestore(&priv->reply_lock, irqstate);

  for (attempt = 0;
       attempt < BK7258_TEMPERATURE_REQUEST_ATTEMPTS;
       attempt++)
    {
      ret = bk7258_temperature_send_bounded(priv, request);
      if (ret < 0)
        {
          break;
        }

      ret = bk7258_temperature_wait_reply(priv, request, reply);
      if (ret >= 0)
        {
          if (reply->status != -EBUSY)
            {
              ret = OK;
              break;
            }

          ret = -EBUSY;
          bk7258_temperature_flush_sem(&priv->reply_sem);
          if (attempt + 1u < BK7258_TEMPERATURE_REQUEST_ATTEMPTS)
            {
              nxsig_usleep(BK7258_TEMPERATURE_BUSY_RETRY_US);
            }
        }
      else if (ret != -ETIMEDOUT)
        {
          break;
        }
    }

  irqstate = spin_lock_irqsave(&priv->reply_lock);
  priv->waiting_generation = 0;
  priv->waiting_sequence = 0;
  priv->reply_valid = false;
  spin_unlock_irqrestore(&priv->reply_lock, irqstate);
  return ret;
}

static int bk7258_temperature_client_cb(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv_)
{
  struct bk7258_temperature_client_s *priv = priv_;
  const struct bk7258_temperature_wire_s *reply = data;
  irqstate_t irqstate;
  bool matched;

  (void)ept;
  (void)src;

  if (reply == NULL || len != sizeof(*reply) ||
      reply->magic != BK7258_TEMPERATURE_MAGIC ||
      reply->version != BK7258_TEMPERATURE_VERSION ||
      reply->command != BK7258_TEMPERATURE_COMMAND_RESPONSE)
    {
      return -ENOMSG;
    }

  irqstate = spin_lock_irqsave(&priv->reply_lock);
  matched = priv->waiting_generation != 0 &&
            reply->generation == priv->waiting_generation &&
            reply->sequence == priv->waiting_sequence;
  if (matched)
    {
      memcpy(&priv->reply, reply, sizeof(priv->reply));
      priv->reply_valid = true;
    }

  spin_unlock_irqrestore(&priv->reply_lock, irqstate);
  if (!matched)
    {
      return -ENOMSG;
    }

  return nxsem_post(&priv->reply_sem);
}

static void bk7258_temperature_device_created(FAR struct rpmsg_device *rdev,
                                              FAR void *priv_)
{
  struct bk7258_temperature_client_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);
  int ret;

  if (cpuname == NULL || strcmp(cpuname, "cp") != 0)
    {
      return;
    }

  ret = nxmutex_lock(&priv->endpoint_lock);
  if (ret < 0)
    {
      __atomic_store_n(&priv->connection_error, ret, __ATOMIC_RELEASE);
      return;
    }

  if (__atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&priv->endpoint_lock);
      return;
    }

  priv->ept.priv = priv;
  ret = rpmsg_create_ept(&priv->ept, rdev,
                         BK7258_TEMPERATURE_EPT_NAME,
                         RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                         bk7258_temperature_client_cb, NULL);
  __atomic_store_n(&priv->connection_error, ret, __ATOMIC_RELEASE);
  if (ret >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }

  nxmutex_unlock(&priv->endpoint_lock);
}

static void bk7258_temperature_device_destroy(FAR struct rpmsg_device *rdev,
                                              FAR void *priv_)
{
  struct bk7258_temperature_client_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);
  irqstate_t irqstate;
  int ret;

  if (cpuname == NULL || strcmp(cpuname, "cp") != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  __atomic_store_n(&priv->connection_error, -ENOTCONN, __ATOMIC_RELEASE);
  ret = nxmutex_lock(&priv->endpoint_lock);
  if (ret >= 0)
    {
      if (priv->ept.rdev != NULL)
        {
          rpmsg_destroy_ept(&priv->ept);
        }

      nxmutex_unlock(&priv->endpoint_lock);
    }

  irqstate = spin_lock_irqsave(&priv->reply_lock);
  priv->reply_valid = false;
  spin_unlock_irqrestore(&priv->reply_lock, irqstate);
  (void)nxsem_post(&priv->reply_sem);
}

static int bk7258_temperature_get_temp(
  FAR struct thermal_zone_device_s *zone, FAR int *temperature)
{
  struct bk7258_temperature_sample_s sample;
  int32_t millicelsius;
  int ret;

  (void)zone;
  if (temperature == NULL)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&g_bk7258_temperature_client.reference_raw,
                      __ATOMIC_ACQUIRE) == 0)
    {
      return -ENODATA;
    }

  /* Registration can precede RPTUN creation.  Do not let the thermal core's
   * initial update stall the AP thread which must create that transport.
   */

  if (!bk7258_temperature_endpoint_ready())
    {
      return -EAGAIN;
    }

  ret = bk7258_temperature_read(&sample);
  if (ret < 0)
    {
      return ret;
    }

  if ((sample.flags & BK7258_TEMPERATURE_FLAG_CALIBRATED) == 0)
    {
      return -ENODATA;
    }

  millicelsius = sample.temperature_millicelsius;
  *temperature = millicelsius >= 0 ?
                 (millicelsius + 500) / 1000 :
                 (millicelsius - 500) / 1000;
  return OK;
}

static const struct thermal_zone_device_ops_s g_bk7258_temperature_ops =
{
  .get_temp = bk7258_temperature_get_temp,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_temperature_read(
  struct bk7258_temperature_sample_s *sample)
{
  struct bk7258_temperature_client_s *priv =
    &g_bk7258_temperature_client;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct bk7258_temperature_wire_s request;
  struct bk7258_temperature_wire_s reply;
  int64_t millicelsius;
  uint32_t reference_raw;
  int ret;

  if (sample == NULL)
    {
      return -EINVAL;
    }

  memset(sample, 0, sizeof(*sample));
  if (!__atomic_load_n(&priv->initialized, __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_temperature_wait_endpoint(priv);
  if (ret < 0)
    {
      goto out;
    }

  if (control->generation == 0)
    {
      ret = -ENOTCONN;
      goto out;
    }

  if (++priv->sequence == 0)
    {
      priv->sequence++;
    }

  memset(&request, 0, sizeof(request));
  request.magic = BK7258_TEMPERATURE_MAGIC;
  request.version = BK7258_TEMPERATURE_VERSION;
  request.command = BK7258_TEMPERATURE_COMMAND_READ;
  request.generation = control->generation;
  request.sequence = priv->sequence;

  ret = bk7258_temperature_exchange(priv, &request, &reply);
  if (ret < 0)
    {
      goto out;
    }

  ret = reply.status;
  if (ret < 0)
    {
      goto out;
    }

  if ((reply.flags & BK7258_TEMPERATURE_FLAG_RAW_VALID) == 0 ||
      reply.raw_code < BK7258_TEMPERATURE_RAW_MIN ||
      reply.raw_code > BK7258_TEMPERATURE_RAW_MAX)
    {
      ret = -EPROTO;
      goto out;
    }

  sample->generation = reply.generation;
  sample->sequence = reply.sequence;
  sample->raw_code = reply.raw_code;
  sample->flags = BK7258_TEMPERATURE_FLAG_RAW_VALID;

  reference_raw = __atomic_load_n(&priv->reference_raw,
                                  __ATOMIC_ACQUIRE);
  if (reference_raw >= BK7258_TEMPERATURE_RAW_MIN &&
      reference_raw <= BK7258_TEMPERATURE_RAW_MAX)
    {
      millicelsius = 25000ll +
        ((int64_t)reference_raw - sample->raw_code) * 10000ll /
        BK7258_TEMPERATURE_LSB_PER_10C;
      sample->reference_raw = reference_raw;
      if (millicelsius >= -40000ll && millicelsius <= 125000ll)
        {
          sample->temperature_millicelsius = (int32_t)millicelsius;
          sample->flags |= BK7258_TEMPERATURE_FLAG_CALIBRATED;
        }
    }

out:
  nxmutex_unlock(&priv->lock);
  return ret;
}

static int bk7258_temperature_apply_reference_raw(
  struct bk7258_temperature_client_s *priv, uint32_t reference_raw)
{
  FAR struct thermal_zone_device_s *zone;
  bool zone_created = false;
  int ret = OK;

  if (reference_raw < BK7258_TEMPERATURE_RAW_MIN ||
      reference_raw > BK7258_TEMPERATURE_RAW_MAX)
    {
      return -ERANGE;
    }

  ret = nxmutex_lock(&priv->zone_lock);
  if (ret < 0)
    {
      return ret;
    }

  zone = priv->zone;
  if (zone == NULL)
    {
      zone = thermal_zone_device_register("bk7258-die", priv,
                                          &g_bk7258_temperature_ops,
                                          &g_bk7258_temperature_params);
      if (zone == NULL)
        {
          ret = -ENODEV;
        }
      else
        {
          priv->zone = zone;
          zone_created = true;
        }
    }

  if (ret >= 0)
    {
      __atomic_store_n(&priv->reference_raw, reference_raw,
                       __ATOMIC_RELEASE);
    }

  nxmutex_unlock(&priv->zone_lock);
  if (ret >= 0 && zone != NULL && !zone_created)
    {
      thermal_zone_device_update(zone);
    }

  return ret;
}

int bk7258_temperature_set_reference_raw(uint32_t reference_raw)
{
  struct bk7258_temperature_client_s *priv =
    &g_bk7258_temperature_client;

  if (!__atomic_load_n(&priv->initialized, __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  return bk7258_temperature_apply_reference_raw(priv, reference_raw);
}

int bk7258_temperature_initialize(void)
{
  struct bk7258_temperature_client_s *priv =
    &g_bk7258_temperature_client;
  bool callback_registered = false;
  bool semaphore_initialized = false;
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

  __atomic_store_n(&priv->connection_error, -ENOTCONN, __ATOMIC_RELEASE);
  ret = nxsem_init(&priv->reply_sem, 0, 0);
  if (ret >= 0)
    {
      semaphore_initialized = true;
    }

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&priv->reply_sem, SEM_PRIO_NONE);
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(priv,
                                    bk7258_temperature_device_created,
                                    bk7258_temperature_device_destroy,
                                    NULL, NULL);
      callback_registered = ret >= 0;
    }

  if (ret >= 0 && CONFIG_BK7258_TEMPERATURE_FIXED_REFERENCE_RAW != 0)
    {
      ret = bk7258_temperature_apply_reference_raw(
              priv, CONFIG_BK7258_TEMPERATURE_FIXED_REFERENCE_RAW);
    }

  if (ret < 0)
    {
      if (callback_registered)
        {
          rpmsg_unregister_callback(priv,
                                    bk7258_temperature_device_created,
                                    bk7258_temperature_device_destroy,
                                    NULL, NULL);
        }

      if (semaphore_initialized)
        {
          (void)nxsem_destroy(&priv->reply_sem);
        }

      memset(&priv->ept, 0, sizeof(priv->ept));
      __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
      __atomic_store_n(&priv->connection_error, ret, __ATOMIC_RELEASE);
    }
  else
    {
      __atomic_store_n(&priv->initialized, true, __ATOMIC_RELEASE);
    }

  nxmutex_unlock(&priv->init_lock);
  return ret;
}

#endif /* CONFIG_BK7258_TEMPERATURE */
