/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/ap/
 * bk7258_pm_client.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP wrapper for the CP-owned BK7258 peripheral clock service.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_PM_CLOCK

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_pm.h>
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_pm_ipc.h"

struct bk7258_pm_client_s
{
  struct rpmsg_endpoint ept;
  mutex_t lock;
  sem_t reply_sem;
  volatile bool initialized;
  volatile bool endpoint_created;
  volatile bool reply_valid;
  volatile int connection_error;
  uint32_t sequence;
  uint32_t waiting_generation;
  uint32_t waiting_sequence;
  int reply_status;
};

static struct bk7258_pm_client_s g_bk7258_pm_client =
{
  .lock = NXMUTEX_INITIALIZER,
};

/* v3.1.1.9 routes AP peripheral clocks through bk_pm_clock_ctrl().  The
 * vendor API is a set-state interface rather than a reference-counted one,
 * so retain one compatibility state per raw SDK clock ID and translate only
 * the first edge into the board-owned CP service.  Board drivers that need
 * real shared ownership use bk7258_pm_clock_get()/put() directly.
 */

#define BK7258_SDK_CLOCK_COUNT 38

static const enum bk7258_pm_clock_e
g_bk7258_pm_sdk_clock_map[BK7258_SDK_CLOCK_COUNT] =
{
  BK7258_PM_CLOCK_I2C1,
  BK7258_PM_CLOCK_SPI1,
  BK7258_PM_CLOCK_UART0,
  BK7258_PM_CLOCK_PWM1,
  BK7258_PM_CLOCK_TIMER1,
  BK7258_PM_CLOCK_SARADC,
  BK7258_PM_CLOCK_IRDA,
  BK7258_PM_CLOCK_EFUSE,
  BK7258_PM_CLOCK_I2C2,
  BK7258_PM_CLOCK_SPI2,
  BK7258_PM_CLOCK_UART1,
  BK7258_PM_CLOCK_UART2,
  BK7258_PM_CLOCK_PWM2,
  BK7258_PM_CLOCK_TIMER2,
  BK7258_PM_CLOCK_TIMER3,
  BK7258_PM_CLOCK_OTP,
  BK7258_PM_CLOCK_I2S1,
  BK7258_PM_CLOCK_USB,
  BK7258_PM_CLOCK_CAN,
  BK7258_PM_CLOCK_PSRAM,
  BK7258_PM_CLOCK_QSPI0,
  BK7258_PM_CLOCK_QSPI1,
  BK7258_PM_CLOCK_SDIO,
  BK7258_PM_CLOCK_AUXS,
  BK7258_PM_CLOCK_BTDM,
  BK7258_PM_CLOCK_XVR,
  BK7258_PM_CLOCK_MAC,
  BK7258_PM_CLOCK_PHY,
  BK7258_PM_CLOCK_JPEG,
  BK7258_PM_CLOCK_DISPLAY,
  BK7258_PM_CLOCK_AUDIO,
  BK7258_PM_CLOCK_WATCHDOG,
  BK7258_PM_CLOCK_H264,
  BK7258_PM_CLOCK_I2S2,
  BK7258_PM_CLOCK_I2S3,
  BK7258_PM_CLOCK_YUV,
  BK7258_PM_CLOCK_SEGMENT_LCD,
  BK7258_PM_CLOCK_LIN,
};

static mutex_t g_bk7258_pm_sdk_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_pm_sdk_enabled[BK7258_SDK_CLOCK_COUNT];
static uint32_t g_bk7258_pm_sdk_generation;

static void bk7258_pm_flush_sem(FAR sem_t *sem)
{
  while (nxsem_trywait(sem) == OK)
    {
    }
}

static bool bk7258_pm_endpoint_ready(struct bk7258_pm_client_s *priv)
{
  return __atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) &&
         is_rpmsg_ept_ready(&priv->ept);
}

static int bk7258_pm_wait_endpoint(struct bk7258_pm_client_s *priv)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BK7258_PM_ENDPOINT_WAIT_MS);

  do
    {
      if (bk7258_pm_endpoint_ready(priv))
        {
          return OK;
        }

      if (priv->connection_error < 0)
        {
          return priv->connection_error;
        }

      nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  return -ETIMEDOUT;
}

static int bk7258_pm_send_bounded(struct bk7258_pm_client_s *priv,
                                  FAR const struct bk7258_pm_wire_s *msg)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BK7258_PM_SEND_TIMEOUT_MS);
  int ret;

  do
    {
      if (!bk7258_pm_endpoint_ready(priv))
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

static int bk7258_pm_client_cb(FAR struct rpmsg_endpoint *ept,
                               FAR void *data, size_t len,
                               uint32_t src, FAR void *priv_)
{
  struct bk7258_pm_client_s *priv = priv_;
  FAR const struct bk7258_pm_wire_s *reply = data;

  (void)ept;
  (void)src;
  if (len != sizeof(*reply) || reply->magic != BK7258_PM_MAGIC ||
      reply->version != BK7258_PM_VERSION ||
      reply->command != BK7258_PM_COMMAND_RESPONSE ||
      reply->generation != priv->waiting_generation ||
      reply->sequence != priv->waiting_sequence)
    {
      return -ENOMSG;
    }

  priv->reply_status = reply->status;
  __asm volatile ("dmb sy" ::: "memory");
  priv->reply_valid = true;
  return nxsem_post(&priv->reply_sem);
}

static void bk7258_pm_device_created(FAR struct rpmsg_device *rdev,
                                     FAR void *priv_)
{
  struct bk7258_pm_client_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);
  int ret;

  if (cpuname == NULL || strcmp(cpuname, "cp") != 0)
    {
      return;
    }

  priv->ept.priv = priv;
  ret = rpmsg_create_ept(&priv->ept, rdev, BK7258_PM_EPT_NAME,
                         RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                         bk7258_pm_client_cb, NULL);
  priv->connection_error = ret;
  if (ret >= 0)
    {
      __atomic_store_n(&priv->endpoint_created, true, __ATOMIC_RELEASE);
    }
}

static void bk7258_pm_device_destroy(FAR struct rpmsg_device *rdev,
                                     FAR void *priv_)
{
  struct bk7258_pm_client_s *priv = priv_;
  FAR const char *cpuname = rpmsg_get_cpuname(rdev);

  if (cpuname == NULL || strcmp(cpuname, "cp") != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  __atomic_store_n(&g_bk7258_pm_sdk_generation, 0, __ATOMIC_RELEASE);
  priv->connection_error = -ENOTCONN;
  priv->reply_valid = false;
  (void)nxsem_post(&priv->reply_sem);
  if (priv->ept.rdev != NULL)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

static int bk7258_pm_request(enum bk7258_pm_clock_e clock,
                             enum bk7258_pm_command_e command)
{
  struct bk7258_pm_client_s *priv = &g_bk7258_pm_client;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct bk7258_pm_wire_s request;
  int ret;

  if (clock < 0 || clock >= BK7258_PM_CLOCK_COUNT)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&priv->initialized, __ATOMIC_ACQUIRE))
    {
      return -EAGAIN;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_pm_wait_endpoint(priv);
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

  bk7258_pm_flush_sem(&priv->reply_sem);
  priv->waiting_generation = control->generation;
  priv->waiting_sequence = priv->sequence;
  priv->reply_valid = false;
  memset(&request, 0, sizeof(request));
  request.magic = BK7258_PM_MAGIC;
  request.version = BK7258_PM_VERSION;
  request.command = command;
  request.generation = control->generation;
  request.sequence = priv->sequence;
  request.clock = clock;

  ret = bk7258_pm_send_bounded(priv, &request);
  if (ret >= 0)
    {
      ret = nxsem_tickwait_uninterruptible(
              &priv->reply_sem, MSEC2TICK(BK7258_PM_REPLY_TIMEOUT_MS));
    }

  __asm volatile ("dmb sy" ::: "memory");
  if (ret >= 0)
    {
      ret = priv->reply_valid ? priv->reply_status :
            (priv->connection_error < 0 ? priv->connection_error : -ESTALE);
    }

out:
  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_pm_initialize(void)
{
  struct bk7258_pm_client_s *priv = &g_bk7258_pm_client;
  bool expected = false;
  int ret;

  if (!__atomic_compare_exchange_n(&priv->initialized, &expected, true,
                                   false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return OK;
    }

  ret = nxsem_init(&priv->reply_sem, 0, 0);
#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&priv->reply_sem, SEM_PRIO_NONE);
    }
#endif
  if (ret >= 0)
    {
      ret = rpmsg_register_callback(priv, bk7258_pm_device_created,
                                    bk7258_pm_device_destroy, NULL, NULL);
    }

  if (ret < 0)
    {
      (void)nxsem_destroy(&priv->reply_sem);
      __atomic_store_n(&priv->initialized, false, __ATOMIC_RELEASE);
    }

  return ret;
}

int bk7258_pm_clock_get(enum bk7258_pm_clock_e clock)
{
  return bk7258_pm_request(clock, BK7258_PM_COMMAND_CLOCK_GET);
}

int bk7258_pm_clock_put(enum bk7258_pm_clock_e clock)
{
  return bk7258_pm_request(clock, BK7258_PM_COMMAND_CLOCK_PUT);
}

/* The immutable AP SDK declares bk_pm_clock_ctrl() with enum arguments and a
 * bk_err_t return value; all three use the ordinary C int ABI.  Keep SDK
 * headers out of this board service so raw vendor IDs cannot escape beyond
 * this compatibility boundary.
 *
 * SDK v3.1.1.9 has two names for raw ID 32: its current BK7258 sys_ctrl path
 * uses it as H264, while the older public PM enum calls it ENET.  No BK7258
 * v3.1.1.9 driver calls the latter; Ethernet uses the separate MAC and PHY
 * IDs.  The project-owned composite ETHERNET vote remains available through
 * bk7258_pm_clock_get() without making the ambiguous raw ABI part of RPMsg.
 */

int __wrap_bk_pm_clock_ctrl(int module, int clock_state)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t generation;
  bool enable;
  int ret;

  if (module < 0 || module >= BK7258_SDK_CLOCK_COUNT ||
      (clock_state != 0 && clock_state != 1))
    {
      return -EINVAL;
    }

  enable = clock_state != 0;
  ret = nxmutex_lock(&g_bk7258_pm_sdk_lock);
  if (ret < 0)
    {
      return ret;
    }

  generation = control->generation;
  if (generation == 0)
    {
      ret = -ENOTCONN;
      goto out;
    }

  if (__atomic_load_n(&g_bk7258_pm_sdk_generation, __ATOMIC_ACQUIRE) !=
      generation)
    {
      memset(g_bk7258_pm_sdk_enabled, 0,
             sizeof(g_bk7258_pm_sdk_enabled));
      __atomic_store_n(&g_bk7258_pm_sdk_generation, generation,
                       __ATOMIC_RELEASE);
    }

  if (g_bk7258_pm_sdk_enabled[module] == enable)
    {
      ret = OK;
      goto out;
    }

  if (enable)
    {
      ret = bk7258_pm_clock_get(g_bk7258_pm_sdk_clock_map[module]);
    }
  else
    {
      ret = bk7258_pm_clock_put(g_bk7258_pm_sdk_clock_map[module]);
    }

  if (ret >= 0)
    {
      g_bk7258_pm_sdk_enabled[module] = enable;
    }

out:
  nxmutex_unlock(&g_bk7258_pm_sdk_lock);
  return ret;
}

#endif /* CONFIG_BK7258_PM_CLOCK */
