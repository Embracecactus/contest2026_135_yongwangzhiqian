/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/
 * bk7258_rpmsg_health.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * N10 generation-safe RPMsg health endpoint.  CP sends a bounded probe and AP
 * returns both scheduler progress counters.  The callback never waits for a
 * TX buffer: AP uses rpmsg_trysend(), while CP performs bounded retry and wait
 * from its supervisor thread.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_SUPERVISOR

#include <errno.h>
#ifdef CONFIG_BK7258_AP_CORE
#  include <pthread.h>
#  include <sched.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_rpmsg_health.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_RPMSG_HEALTH_EPT_NAME      "bk7258-health"
#define BK7258_RPMSG_HEALTH_MAGIC         0x544c4842u /* "BHLT" */
#define BK7258_RPMSG_HEALTH_VERSION       1u
#define BK7258_RPMSG_HEALTH_SEND_MS       100u

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_RPMSG_HEALTH_TX_CPU      0u
#  define BK7258_RPMSG_HEALTH_TX_PRIO     \
     (CONFIG_BK7258_RPTUN_RX_PRIORITY - 2)
#endif

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_RPMSG_HEALTH_REMOTE_NAME "cp"
#else
#  define BK7258_RPMSG_HEALTH_REMOTE_NAME "ap"
#endif

enum bk7258_rpmsg_health_command_e
{
  BK7258_RPMSG_HEALTH_PING = 1,
  BK7258_RPMSG_HEALTH_PONG
};

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_rpmsg_health_wire_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t generation;
  uint32_t sequence;
  uint32_t primary_heartbeat;
  uint32_t secondary_heartbeat;
  int32_t status;
  uint32_t reserved;
};

struct bk7258_rpmsg_health_dev_s
{
  struct rpmsg_endpoint ept;
  volatile bool initialized;
  volatile bool endpoint_created;
  volatile int connection_error;
#ifdef CONFIG_BK7258_AP_CORE
  sem_t tx_sem;
  volatile bool tx_pending;
  struct bk7258_rpmsg_health_wire_s tx_reply;
#else
  sem_t reply_sem;
  mutex_t lock;
  volatile uint32_t waiting_generation;
  volatile uint32_t waiting_sequence;
  volatile bool reply_valid;
  uint32_t next_sequence;
  struct bk7258_rpmsg_health_result_s reply;
#endif
};

static_assert(sizeof(struct bk7258_rpmsg_health_wire_s) == 32u,
              "BK7258 RPMsg health wire ABI must remain 32 bytes");

#ifdef CONFIG_BK7258_AP_CORE
static_assert(CONFIG_BK7258_RPTUN_RX_PRIORITY >= 3,
              "RPTUN RX priority must leave room for health TX");
static_assert(BK7258_RPMSG_HEALTH_TX_PRIO >=
              CONFIG_BK7258_AP_SUPERVISOR_PRIORITY,
              "RPMsg health TX must not run below the AP heartbeat");
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_rpmsg_health_dev_s g_bk7258_rpmsg_health =
{
#ifndef CONFIG_BK7258_AP_CORE
  .lock = NXMUTEX_INITIALIZER,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bk7258_rpmsg_health_endpoint_ready(void)
{
  struct bk7258_rpmsg_health_dev_s *priv = &g_bk7258_rpmsg_health;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  return __atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) &&
         control->state == BK7258_RPTUN_STATE_CONNECTED &&
         is_rpmsg_ept_ready(&priv->ept);
}

#ifdef CONFIG_BK7258_AP_CORE
static int bk7258_rpmsg_health_send_bounded(
  const struct bk7258_rpmsg_health_wire_s *msg)
{
  struct bk7258_rpmsg_health_dev_s *priv = &g_bk7258_rpmsg_health;
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BK7258_RPMSG_HEALTH_SEND_MS);
  int ret = -ENOTCONN;

  do
    {
      if (!bk7258_rpmsg_health_endpoint_ready())
        {
          return -ENOTCONN;
        }

      ret = rpmsg_trysend(&priv->ept, msg, sizeof(*msg));
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

static FAR void *bk7258_rpmsg_health_tx_worker(FAR void *arg)
{
  struct bk7258_rpmsg_health_dev_s *priv = &g_bk7258_rpmsg_health;
  struct bk7258_rpmsg_health_wire_s reply;

  (void)arg;
  for (; ; )
    {
      if (nxsem_wait_uninterruptible(&priv->tx_sem) < 0)
        {
          continue;
        }

      while (__atomic_exchange_n(&priv->tx_pending, false,
                                  __ATOMIC_ACQ_REL))
        {
          __asm volatile ("dmb sy" ::: "memory");
          memcpy(&reply, &priv->tx_reply, sizeof(reply));
          (void)bk7258_rpmsg_health_send_bounded(&reply);
        }
    }

  return NULL;
}

static int bk7258_rpmsg_health_start_tx_worker(void)
{
  struct sched_param param;
  cpu_set_t cpuset =
    (cpu_set_t)(1u << BK7258_RPMSG_HEALTH_TX_CPU);
  pthread_attr_t attr;
  pthread_t thread;
  bool initialized = false;
  int ret;

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      initialized = true;
      ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(
              &attr, CONFIG_BK7258_AP_SUPERVISOR_STACKSIZE);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

  if (ret == 0)
    {
      ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    }

  if (ret == 0)
    {
      memset(&param, 0, sizeof(param));
      param.sched_priority = BK7258_RPMSG_HEALTH_TX_PRIO;
      ret = pthread_attr_setschedparam(&attr, &param);
    }

  if (ret == 0)
    {
      ret = pthread_create(&thread, &attr,
                           bk7258_rpmsg_health_tx_worker, NULL);
    }

  if (initialized)
    {
      (void)pthread_attr_destroy(&attr);
    }

  return ret == 0 ? OK : -ret;
}
#endif

#ifndef CONFIG_BK7258_AP_CORE
static void bk7258_rpmsg_health_flush_sem(sem_t *sem)
{
  while (nxsem_trywait(sem) == OK)
    {
    }
}

static int bk7258_rpmsg_health_send_bounded(
  const struct bk7258_rpmsg_health_wire_s *msg, uint32_t timeout_ms)
{
  struct bk7258_rpmsg_health_dev_s *priv = &g_bk7258_rpmsg_health;
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(timeout_ms);
  int ret = -ENOTCONN;

  do
    {
      if (!bk7258_rpmsg_health_endpoint_ready())
        {
          return -ENOTCONN;
        }

      ret = rpmsg_trysend(&priv->ept, msg, sizeof(*msg));
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
#endif

static bool bk7258_rpmsg_health_wire_valid(
  const struct bk7258_rpmsg_health_wire_s *msg, size_t len)
{
  return msg != NULL && len == sizeof(*msg) &&
         msg->magic == BK7258_RPMSG_HEALTH_MAGIC &&
         msg->version == BK7258_RPMSG_HEALTH_VERSION &&
         (msg->command == BK7258_RPMSG_HEALTH_PING ||
          msg->command == BK7258_RPMSG_HEALTH_PONG);
}

static int bk7258_rpmsg_health_ept_cb(FAR struct rpmsg_endpoint *ept,
                                      FAR void *data, size_t len,
                                      uint32_t src, FAR void *priv_)
{
  struct bk7258_rpmsg_health_dev_s *priv = priv_;
  struct bk7258_rpmsg_health_wire_s *msg = data;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  (void)ept;
  (void)src;
  if (!bk7258_rpmsg_health_wire_valid(msg, len) ||
      msg->generation == 0 || msg->generation != control->generation)
    {
      return -ESTALE;
    }

#ifdef CONFIG_BK7258_AP_CORE
  if (msg->command == BK7258_RPMSG_HEALTH_PING)
    {
      volatile struct bk7258_ap_boot_state_s *boot =
        bk7258_ap_boot_state();
      volatile struct bk7258_cpu2_probe_state_s *cpu2 =
        bk7258_cpu2_probe_state();
      struct bk7258_rpmsg_health_wire_s reply;
      int ret;

      memset(&reply, 0, sizeof(reply));
      reply.magic = BK7258_RPMSG_HEALTH_MAGIC;
      reply.version = BK7258_RPMSG_HEALTH_VERSION;
      reply.command = BK7258_RPMSG_HEALTH_PONG;
      reply.generation = msg->generation;
      reply.sequence = msg->sequence;
      reply.primary_heartbeat = boot->heartbeat;
      reply.secondary_heartbeat = cpu2->heartbeat;
      reply.status = OK;
      ret = rpmsg_trysend(&priv->ept, &reply, sizeof(reply));
      if (ret >= 0 || (ret != -ENOMEM && ret != -EAGAIN))
        {
          return ret;
        }

      /* Never wait for a TX buffer in the RPTUN RX callback.  Preserve the
       * latest probe and let the CPU0 health worker retry after the callback
       * releases the receive path.  The CP serializes probes, so one pending
       * reply is sufficient and coalescing cannot hide an in-flight request.
       */

      memcpy(&priv->tx_reply, &reply, sizeof(reply));
      __asm volatile ("dmb sy" ::: "memory");
      __atomic_store_n(&priv->tx_pending, true, __ATOMIC_RELEASE);
      return nxsem_post(&priv->tx_sem);
    }
#else
  if (msg->command == BK7258_RPMSG_HEALTH_PONG &&
      msg->generation == priv->waiting_generation &&
      msg->sequence == priv->waiting_sequence)
    {
      priv->reply.generation = msg->generation;
      priv->reply.sequence = msg->sequence;
      priv->reply.primary_heartbeat = msg->primary_heartbeat;
      priv->reply.secondary_heartbeat = msg->secondary_heartbeat;
      priv->connection_error = msg->status;
      __asm volatile ("dmb sy" ::: "memory");
      priv->reply_valid = msg->status >= 0;
      (void)nxsem_post(&priv->reply_sem);
      return OK;
    }
#endif

  return -ENOMSG;
}

static void bk7258_rpmsg_health_device_created(FAR struct rpmsg_device *rdev,
                                               FAR void *priv_)
{
  struct bk7258_rpmsg_health_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL ||
      strcmp(cpuname, BK7258_RPMSG_HEALTH_REMOTE_NAME) != 0)
    {
      return;
    }

#ifdef CONFIG_BK7258_AP_CORE
  priv->ept.priv = priv;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, BK7258_RPMSG_HEALTH_EPT_NAME,
    RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, bk7258_rpmsg_health_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
#else
  priv->connection_error = OK;
#endif
}

#ifndef CONFIG_BK7258_AP_CORE
static bool bk7258_rpmsg_health_ns_match(FAR struct rpmsg_device *rdev,
                                        FAR void *priv_,
                                        FAR const char *name,
                                        uint32_t dest)
{
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)priv_;
  (void)dest;
  return cpuname != NULL &&
         strcmp(cpuname, BK7258_RPMSG_HEALTH_REMOTE_NAME) == 0 &&
         strcmp(name, BK7258_RPMSG_HEALTH_EPT_NAME) == 0;
}

static void bk7258_rpmsg_health_ns_bind(FAR struct rpmsg_device *rdev,
                                       FAR void *priv_,
                                       FAR const char *name,
                                       uint32_t dest)
{
  struct bk7258_rpmsg_health_dev_s *priv = priv_;

  priv->ept.priv = priv;
  priv->connection_error = rpmsg_create_ept(
    &priv->ept, rdev, name, RPMSG_ADDR_ANY, dest,
    bk7258_rpmsg_health_ept_cb, NULL);
  if (priv->connection_error >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
      bk7258_rptun_mark_connected();
    }
}
#endif

static void bk7258_rpmsg_health_device_destroy(FAR struct rpmsg_device *rdev,
                                               FAR void *priv_)
{
  struct bk7258_rpmsg_health_dev_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL ||
      strcmp(cpuname, BK7258_RPMSG_HEALTH_REMOTE_NAME) != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  priv->connection_error = -ENOTCONN;
#ifndef CONFIG_BK7258_AP_CORE
  priv->reply_valid = false;
  (void)nxsem_post(&priv->reply_sem);
#endif
  if (priv->ept.rdev != NULL)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_rpmsg_health_initialize(void)
{
  struct bk7258_rpmsg_health_dev_s *priv = &g_bk7258_rpmsg_health;
  bool expected = false;
  bool semaphore_initialized = false;
  int ret = OK;

  if (!__atomic_compare_exchange_n(&priv->initialized, &expected, true,
                                   false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return OK;
    }

#ifdef CONFIG_BK7258_AP_CORE
  ret = nxsem_init(&priv->tx_sem, 0, 0);
#else
  ret = nxsem_init(&priv->reply_sem, 0, 0);
#endif

  if (ret >= 0)
    {
      semaphore_initialized = true;
    }

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
#ifdef CONFIG_BK7258_AP_CORE
      ret = nxsem_set_protocol(&priv->tx_sem, SEM_PRIO_NONE);
#else
      ret = nxsem_set_protocol(&priv->reply_sem, SEM_PRIO_NONE);
#endif
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(priv,
                                    bk7258_rpmsg_health_device_created,
                                    bk7258_rpmsg_health_device_destroy,
#ifdef CONFIG_BK7258_AP_CORE
                                    NULL, NULL);
#else
                                    bk7258_rpmsg_health_ns_match,
                                    bk7258_rpmsg_health_ns_bind);
#endif
    }

#ifdef CONFIG_BK7258_AP_CORE
  if (ret >= 0)
    {
      ret = bk7258_rpmsg_health_start_tx_worker();
    }
#endif

  if (ret < 0)
    {
      if (semaphore_initialized)
        {
#ifdef CONFIG_BK7258_AP_CORE
          (void)nxsem_destroy(&priv->tx_sem);
#else
          (void)nxsem_destroy(&priv->reply_sem);
#endif
        }
      __atomic_store_n(&priv->initialized, false, __ATOMIC_RELEASE);
    }

  return ret;
}

#ifndef CONFIG_BK7258_AP_CORE
bool bk7258_rpmsg_health_ready(void)
{
  return bk7258_rpmsg_health_endpoint_ready();
}

int bk7258_rpmsg_health_probe(
  uint32_t generation, uint32_t timeout_ms,
  struct bk7258_rpmsg_health_result_s *result)
{
  struct bk7258_rpmsg_health_dev_s *priv = &g_bk7258_rpmsg_health;
  struct bk7258_rpmsg_health_wire_s msg;
  clock_t start;
  clock_t elapsed;
  clock_t limit;
  int ret;

  if (generation == 0 || timeout_ms == 0 || result == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!bk7258_rpmsg_health_endpoint_ready())
    {
      ret = -ENOTCONN;
      goto out;
    }

  if (++priv->next_sequence == 0)
    {
      priv->next_sequence++;
    }

  bk7258_rpmsg_health_flush_sem(&priv->reply_sem);
  priv->waiting_generation = generation;
  priv->waiting_sequence = priv->next_sequence;
  priv->reply_valid = false;
  memset(&msg, 0, sizeof(msg));
  msg.magic = BK7258_RPMSG_HEALTH_MAGIC;
  msg.version = BK7258_RPMSG_HEALTH_VERSION;
  msg.command = BK7258_RPMSG_HEALTH_PING;
  msg.generation = generation;
  msg.sequence = priv->next_sequence;

  start = clock_systime_ticks();
  ret = bk7258_rpmsg_health_send_bounded(
          &msg, timeout_ms < BK7258_RPMSG_HEALTH_SEND_MS ?
          timeout_ms : BK7258_RPMSG_HEALTH_SEND_MS);
  if (ret < 0)
    {
      goto out;
    }

  elapsed = clock_systime_ticks() - start;
  limit = MSEC2TICK(timeout_ms);
  if (elapsed >= limit)
    {
      ret = -ETIMEDOUT;
      goto out;
    }

  ret = nxsem_tickwait_uninterruptible(&priv->reply_sem, limit - elapsed);
  if (ret < 0)
    {
      goto out;
    }

  __asm volatile ("dmb sy" ::: "memory");
  if (!priv->reply_valid ||
      priv->reply.generation != generation ||
      priv->reply.sequence != priv->waiting_sequence)
    {
      ret = priv->connection_error < 0 ?
            priv->connection_error : -ESTALE;
      goto out;
    }

  memcpy(result, &priv->reply, sizeof(*result));
  ret = OK;

out:
  nxmutex_unlock(&priv->lock);
  return ret;
}
#endif

#endif /* CONFIG_BK7258_AP_SUPERVISOR */
