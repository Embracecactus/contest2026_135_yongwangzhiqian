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

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/sched.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_pm.h>
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_pm_ipc.h"
#include "bk7258_pm_activity.h"
#include "bk7258_dvfs.h"
#include "arm_internal.h"

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
  bool pending_valid;
  bool pending_committed;
  int pending_status;
  struct bk7258_pm_wire_s pending_request;
  struct bk7258_pm_wire_s reply;
  uint16_t clock_refs[BK7258_PM_CLOCK_COUNT];
  bool freq_active[BK7258_PM_FREQ_CLIENT_COUNT];
#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
  struct bk7258_pm_activity_s sdk_activity;
#endif
};

static struct bk7258_pm_client_s g_bk7258_pm_client =
{
  .lock = NXMUTEX_INITIALIZER,
};

static int bk7258_pm_recalc_primary_timebase(void);

static void bk7258_pm_publish_vendor_votes_locked(
  FAR const struct bk7258_pm_client_s *priv)
{
  uintptr_t sleep = BK7258_PWR_MNG_ADDR +
                    BK7258_PWR_AP_SLEEP_VOTE_OFFSET;
  uintptr_t clock = BK7258_PWR_MNG_ADDR +
                    BK7258_PWR_AP_CLOCK_VOTE_OFFSET;
  bool active = false;
  unsigned int i;

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
  active = !bk7258_pm_activity_idle(&priv->sdk_activity);
#endif

  for (i = 0; !active && i < BK7258_PM_CLOCK_COUNT; i++)
    {
      if (priv->clock_refs[i] != 0)
        {
          active = true;
          break;
        }
    }

  if (!active)
    {
      for (i = 0; i < BK7258_PM_FREQ_CLIENT_COUNT; i++)
        {
          if (priv->freq_active[i])
            {
              active = true;
              break;
            }
        }
    }

  /* The SDK consumes both fields only as zero/non-zero gates.  Publishing
   * one aggregate bit avoids unaligned 64-bit read/modify/write races at the
   * fixed +60/+68 ABI offsets while retaining the official meaning.
   */

  putreg32(active ? 1u : 0u, sleep);
  putreg32(0, sleep + 4u);
  putreg32(active ? 1u : 0u, clock);
  putreg32(0, clock + 4u);
  __asm volatile ("dmb sy" ::: "memory");
}

static void bk7258_pm_publish_vendor_votes(
  FAR const struct bk7258_pm_client_s *priv)
{
  irqstate_t flags = enter_critical_section();

  bk7258_pm_publish_vendor_votes_locked(priv);
  leave_critical_section(flags);
}

/* v3.1.1.9 routes AP peripheral clocks through bk_pm_clock_ctrl().  The
 * vendor API is a set-state interface rather than a reference-counted one,
 * so retain one compatibility state per raw SDK clock ID and translate only
 * the first edge into the board-owned CP service.  Board drivers that need
 * real shared ownership use bk7258_pm_clock_get()/put() directly.
 */

#define BK7258_SDK_CLOCK_COUNT 38

/* v3.1.1.9 BK7258 sys_types.h encodes VIDP submodules as
 * POWER_MODULE_NAME_VIDP * PM_MODULE_SUB_POWER_DOMAIN_MAX + index.
 * JPEG encode/decode, DMA2D, YUV buffer, rotator, Scale0, Scale1 and H264 are
 * VIDP submodules 0, 1, 2, 4, 5, 6, 7 and 8: 140 through 148 as listed below.
 * Their power-state enum uses ON=0 and OFF=1, unlike bk_pm_clock_ctrl().
 */

#define BK7258_SDK_POWER_VIDP_JPEG_ENCODER 140u
#define BK7258_SDK_POWER_VIDP_JPEG_DECODER 141u
#define BK7258_SDK_POWER_VIDP_DMA2D        142u
#define BK7258_SDK_POWER_VIDP_YUV_BUFFER   144u
#define BK7258_SDK_POWER_VIDP_ROTATOR      145u
#define BK7258_SDK_POWER_VIDP_SCALE0       146u
#define BK7258_SDK_POWER_VIDP_SCALE1       147u
#define BK7258_SDK_POWER_VIDP_H264         148u
#define BK7258_SDK_POWER_AUDP_AUDIO         122u
#define BK7258_SDK_POWER_STATE_ON   0
#define BK7258_SDK_POWER_STATE_OFF  1
#define BK7258_SDK_PM_DEV_PWM2      12u
#define BK7258_SDK_PM_DEV_USB       17u
#define BK7258_SDK_PM_DEV_JPEG      28u
#define BK7258_SDK_PM_DEV_DISPLAY   29u
#define BK7258_SDK_PM_DEV_AUDIO     30u
#define BK7258_SDK_PM_DEV_DECODER   33u
#define BK7258_SDK_PM_DEV_SECURE    36u
#define BK7258_SDK_PM_DEV_DEFAULT   40u
#define BK7258_SDK_PM_CPU_FREQ_MAX  7
#define BK7258_SDK_PM_SLEEP_LOG     22u

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
static bool g_bk7258_pm_sdk_jpeg_encoder_enabled;
static bool g_bk7258_pm_sdk_jpeg_decoder_enabled;
static bool g_bk7258_pm_sdk_dma2d_enabled;
static bool g_bk7258_pm_sdk_yuv_buffer_enabled;
static bool g_bk7258_pm_sdk_rotator_enabled;
static bool g_bk7258_pm_sdk_scale0_enabled;
static bool g_bk7258_pm_sdk_scale1_enabled;
static bool g_bk7258_pm_sdk_h264_enabled;
static bool g_bk7258_pm_sdk_audio_power_enabled;
static uint8_t g_bk7258_pm_sdk_freq[BK7258_PM_FREQ_CLIENT_COUNT];
static uint32_t g_bk7258_pm_sdk_generation;

extern int __real_bk_pm_module_vote_power_ctrl(unsigned int module,
                                                int power_state);
static void bk7258_pm_sdk_reset_generation(uint32_t generation)
{
  unsigned int i;

  memset(g_bk7258_pm_sdk_enabled, 0, sizeof(g_bk7258_pm_sdk_enabled));
  g_bk7258_pm_sdk_jpeg_encoder_enabled = false;
  g_bk7258_pm_sdk_jpeg_decoder_enabled = false;
  g_bk7258_pm_sdk_dma2d_enabled = false;
  g_bk7258_pm_sdk_yuv_buffer_enabled = false;
  g_bk7258_pm_sdk_rotator_enabled = false;
  g_bk7258_pm_sdk_scale0_enabled = false;
  g_bk7258_pm_sdk_scale1_enabled = false;
  g_bk7258_pm_sdk_h264_enabled = false;
  g_bk7258_pm_sdk_audio_power_enabled = false;
  for (i = 0; i < BK7258_PM_FREQ_CLIENT_COUNT; i++)
    {
      g_bk7258_pm_sdk_freq[i] = BK7258_PM_CPU_FREQ_DEFAULT;
    }

  __atomic_store_n(&g_bk7258_pm_sdk_generation, generation,
                   __ATOMIC_RELEASE);
}

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

  memcpy(&priv->reply, reply, sizeof(priv->reply));
  __asm volatile ("dmb sy" ::: "memory");
  __atomic_store_n(&priv->reply_valid, true, __ATOMIC_RELEASE);
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
  __atomic_store_n(&priv->reply_valid, false, __ATOMIC_RELEASE);
  priv->pending_valid = false;
  priv->pending_committed = false;
  memset(priv->clock_refs, 0, sizeof(priv->clock_refs));
  memset(priv->freq_active, 0, sizeof(priv->freq_active));
  bk7258_pm_publish_vendor_votes(priv);
  (void)nxsem_post(&priv->reply_sem);
  if (priv->ept.rdev != NULL)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

static bool bk7258_pm_same_operation(
  FAR const struct bk7258_pm_wire_s *request, uint32_t resource,
  enum bk7258_pm_command_e command, uint32_t value)
{
  return request->command == command && request->clock == resource &&
         request->reserved == value;
}

static int bk7258_pm_exchange(FAR struct bk7258_pm_client_s *priv,
                              FAR const struct bk7258_pm_wire_s *request,
                              bool new_transaction,
                              FAR bool *sent)
{
  unsigned int attempt;
  int ret = -ETIMEDOUT;

  *sent = false;
  priv->waiting_generation = request->generation;
  priv->waiting_sequence = request->sequence;
  if (new_transaction)
    {
      /* Clear state once for the whole transaction.  A reply may arrive just
       * after one attempt times out; retrying the same tuple must not erase
       * that valid late reply before the next wait observes its token.
       */

      __atomic_store_n(&priv->reply_valid, false, __ATOMIC_RELEASE);
      bk7258_pm_flush_sem(&priv->reply_sem);
    }

  for (attempt = 0; attempt < BK7258_PM_REQUEST_ATTEMPTS; attempt++)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (__atomic_load_n(&priv->reply_valid, __ATOMIC_ACQUIRE))
        {
          return OK;
        }

      ret = bk7258_pm_send_bounded(priv, request);
      if (ret < 0)
        {
          break;
        }

      *sent = true;
      ret = nxsem_tickwait_uninterruptible(
              &priv->reply_sem, MSEC2TICK(BK7258_PM_REPLY_TIMEOUT_MS));
      __asm volatile ("dmb sy" ::: "memory");
      if (__atomic_load_n(&priv->reply_valid, __ATOMIC_ACQUIRE))
        {
          return OK;
        }

      if (priv->connection_error < 0)
        {
          return priv->connection_error;
        }

      /* The CP operation may already be committed even though its response
       * was lost.  Retry the exact same sequence; protocol v4 servers replay
       * the cached response without changing references or votes twice.
       */
    }

  return ret;
}

static int bk7258_pm_commit_reply(
  FAR struct bk7258_pm_client_s *priv,
  FAR const struct bk7258_pm_wire_s *request,
  FAR struct bk7258_pm_frequency_status_s *snapshot)
{
  uint32_t resource = request->clock;
  int post_ret = OK;
  int ret;

  __asm volatile ("dmb sy" ::: "memory");
  if (!__atomic_load_n(&priv->reply_valid, __ATOMIC_ACQUIRE))
    {
      return priv->connection_error < 0 ? priv->connection_error : -ESTALE;
    }

  ret = priv->reply.status;
  if (ret >= 0 && request->command == BK7258_PM_COMMAND_CLOCK_GET)
    {
      if (priv->clock_refs[resource] != UINT16_MAX)
        {
          priv->clock_refs[resource]++;
        }
    }
  else if (ret >= 0 && request->command == BK7258_PM_COMMAND_CLOCK_PUT)
    {
      if (priv->clock_refs[resource] != 0)
        {
          priv->clock_refs[resource]--;
        }
    }
  else if (ret >= 0 &&
           request->command == BK7258_PM_COMMAND_CPU_FREQ_VOTE)
    {
      priv->freq_active[resource] =
        request->reserved != BK7258_PM_CPU_FREQ_DEFAULT;
      post_ret = bk7258_pm_recalc_primary_timebase();
    }

  /* A successful CP reply means the resource transition is committed even
   * if AP's local timebase repair subsequently reports an error.  Publish
   * the committed local accounting first so a retry cannot apply the same
   * CP vote twice or leave the vendor activity view stale.
   */

  if (ret >= 0 && request->command != BK7258_PM_COMMAND_CPU_FREQ_QUERY)
    {
      bk7258_pm_publish_vendor_votes(priv);
    }

  if (ret >= 0 && snapshot != NULL)
    {
      snapshot->transitions = priv->reply.clock;
      snapshot->current = priv->reply.refcount;
      snapshot->peak = priv->reply.reserved;
    }

  return post_ret < 0 ? post_ret : ret;
}

static int bk7258_pm_request(uint32_t resource,
                             enum bk7258_pm_command_e command,
                             uint32_t value,
                             struct bk7258_pm_frequency_status_s *snapshot)
{
  struct bk7258_pm_client_s *priv = &g_bk7258_pm_client;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct bk7258_pm_wire_s request;
  bool same_pending;
  bool sent;
  int ret;

  if ((command == BK7258_PM_COMMAND_CLOCK_GET ||
       command == BK7258_PM_COMMAND_CLOCK_PUT) &&
      resource >= BK7258_PM_CLOCK_COUNT)
    {
      return -EINVAL;
    }

  if (command == BK7258_PM_COMMAND_CPU_FREQ_VOTE &&
      (resource >= BK7258_PM_FREQ_CLIENT_COUNT ||
       value > BK7258_PM_CPU_FREQ_DEFAULT))
    {
      return -EINVAL;
    }

  if (command == BK7258_PM_COMMAND_CPU_FREQ_QUERY && snapshot == NULL)
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

  if (priv->pending_valid &&
      priv->pending_request.generation != control->generation)
    {
      /* CP releases all references when the RPTUN generation changes.  The
       * old cached response is no longer reachable, so converge AP to that
       * same empty generation before accepting a new transaction.
       */

      priv->pending_valid = false;
      priv->pending_committed = false;
      __atomic_store_n(&priv->reply_valid, false, __ATOMIC_RELEASE);
      memset(priv->clock_refs, 0, sizeof(priv->clock_refs));
      memset(priv->freq_active, 0, sizeof(priv->freq_active));
      bk7258_pm_publish_vendor_votes(priv);
    }

  /* Never advance past an operation whose response was not observed.  CP
   * may already have committed it.  Recover the cached response with the
   * original generation/sequence first, then update AP's local accounting
   * exactly once.  A caller repeating that operation receives its recovered
   * result instead of creating a second transaction.
   */

  if (priv->pending_valid)
    {
      same_pending = bk7258_pm_same_operation(&priv->pending_request,
                                              resource, command, value);
      if (!priv->pending_committed)
        {
          ret = bk7258_pm_exchange(priv, &priv->pending_request, false,
                                   &sent);
          if (ret < 0)
            {
              goto out;
            }

          priv->pending_status =
            bk7258_pm_commit_reply(priv, &priv->pending_request,
                                   same_pending ? snapshot : NULL);
          priv->pending_committed = true;
        }

      if (!same_pending)
        {
          /* The original caller already observed a timeout.  Once its exact
           * tuple has been replayed and committed to the AP bookkeeping, the
           * local and CP resource views agree again.  Retire that transaction
           * and let the current, different operation proceed; this is needed
           * for the common error-cleanup sequence GET(timeout) -> PUT.
           */

          priv->pending_valid = false;
          priv->pending_committed = false;
        }
      else
        {
          if (snapshot != NULL && priv->pending_status >= 0)
            {
              snapshot->transitions = priv->reply.clock;
              snapshot->current = priv->reply.refcount;
              snapshot->peak = priv->reply.reserved;
            }

          ret = priv->pending_status;
          priv->pending_valid = false;
          priv->pending_committed = false;
          goto out;
        }
    }

  if (++priv->sequence == 0)
    {
      priv->sequence++;
    }

  priv->waiting_generation = control->generation;
  priv->waiting_sequence = priv->sequence;
  memset(&request, 0, sizeof(request));
  request.magic = BK7258_PM_MAGIC;
  request.version = BK7258_PM_VERSION;
  request.command = command;
  request.generation = control->generation;
  request.sequence = priv->sequence;
  request.clock = resource;
  request.reserved = value;

  ret = bk7258_pm_exchange(priv, &request, true, &sent);
  if (ret < 0)
    {
      if (sent && priv->connection_error >= 0 &&
          control->generation == request.generation)
        {
          memcpy(&priv->pending_request, &request,
                 sizeof(priv->pending_request));
          priv->pending_valid = true;
          priv->pending_committed = false;
        }

      goto out;
    }

  ret = bk7258_pm_commit_reply(priv, &request, snapshot);

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
  else
    {
      memset(priv->clock_refs, 0, sizeof(priv->clock_refs));
      memset(priv->freq_active, 0, sizeof(priv->freq_active));
      bk7258_pm_publish_vendor_votes(priv);
    }

  return ret;
}

int bk7258_pm_clock_get(enum bk7258_pm_clock_e clock)
{
  return bk7258_pm_request(clock, BK7258_PM_COMMAND_CLOCK_GET, 0, NULL);
}

int bk7258_pm_clock_put(enum bk7258_pm_clock_e clock)
{
  return bk7258_pm_request(clock, BK7258_PM_COMMAND_CLOCK_PUT, 0, NULL);
}

static int bk7258_pm_recalc_primary(FAR void *arg)
{
  (void)arg;
  bk7258_systick_recalc();
  return OK;
}

static int bk7258_pm_recalc_primary_timebase(void)
{
#ifdef CONFIG_SMP
  if (this_cpu() != 0)
    {
      return nxsched_smp_call_single(0, bk7258_pm_recalc_primary, NULL);
    }
#endif

  return bk7258_pm_recalc_primary(NULL);
}

int bk7258_pm_frequency_vote(enum bk7258_pm_freq_client_e client,
                             enum bk7258_pm_cpu_freq_e frequency)
{
  /* CP changes the shared mux and refreshes its DWT conversion before
   * replying.  bk7258_pm_commit_reply() refreshes AP logical CPU0 after
   * every confirmed vote, including a cached reply recovered from an
   * earlier timeout.  Both scheduler clocks remain on fixed 32 kHz.
   */

  return bk7258_pm_request(client, BK7258_PM_COMMAND_CPU_FREQ_VOTE,
                           frequency, NULL);
}

int bk7258_pm_frequency_get_status(
  struct bk7258_pm_frequency_status_s *status)
{
  return bk7258_pm_request(0, BK7258_PM_COMMAND_CPU_FREQ_QUERY, 0, status);
}

static int bk7258_pm_sdk_freq_client(unsigned int module)
{
  switch (module)
    {
      case BK7258_SDK_PM_DEV_JPEG:
        return BK7258_PM_FREQ_CLIENT_VIDEO_ENCODER;
      case BK7258_SDK_PM_DEV_DECODER:
        return BK7258_PM_FREQ_CLIENT_VIDEO_DECODER;
      case BK7258_SDK_PM_DEV_DISPLAY:
        return BK7258_PM_FREQ_CLIENT_DISPLAY;
      case BK7258_SDK_PM_DEV_AUDIO:
        return BK7258_PM_FREQ_CLIENT_AUDIO;
      case BK7258_SDK_PM_DEV_USB:
        return BK7258_PM_FREQ_CLIENT_USB;
      case BK7258_SDK_PM_DEV_PWM2:
        return BK7258_PM_FREQ_CLIENT_PWM;
      case BK7258_SDK_PM_DEV_SECURE:
        return BK7258_PM_FREQ_CLIENT_SECURE;
      case BK7258_SDK_PM_DEV_DEFAULT:
        return BK7258_PM_FREQ_CLIENT_DEFAULT;
      default:
        return -ENOTSUP;
    }
}

/* v3.1.1.9 bk_yuv_buf_init() votes PM_DEV_ID_JPEG to 480 MHz before
 * starting either JPEG or H264 capture.  In the SDK this AP request is sent
 * to CPU0 by the vendor mailbox PM service.  NuttX owns that mailbox, so
 * preserve the SDK contract over the board-owned RPMsg PM channel instead.
 */

int __wrap_bk_pm_module_vote_cpu_freq(unsigned int module, int cpu_freq)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t generation;
  int client;
  int ret;

  client = bk7258_pm_sdk_freq_client(module);
  if (client < 0)
    {
      return client;
    }

  if (cpu_freq < 0 || cpu_freq > BK7258_SDK_PM_CPU_FREQ_MAX)
    {
      return -EINVAL;
    }

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
      bk7258_pm_sdk_reset_generation(generation);
    }

  if (g_bk7258_pm_sdk_freq[client] == cpu_freq)
    {
      ret = OK;
      goto out;
    }

  ret = bk7258_pm_frequency_vote(client, cpu_freq);
  if (ret >= 0)
    {
      g_bk7258_pm_sdk_freq[client] = cpu_freq;
    }

out:
  nxmutex_unlock(&g_bk7258_pm_sdk_lock);
  return ret;
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
      bk7258_pm_sdk_reset_generation(generation);
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

/* Route the verified BK7258 v3.1.1.9 audio and VIDP submodules through the
 * CP-owned PM service.  The immutable audio driver issues a domain vote
 * followed by PM_CLK_ID_AUDIO; both acquire the composite AUDIO resource,
 * so the first edge powers the domain/clock and the last edge reverses it.
 * Other vendor modules retain their SDK behavior until their ownership is
 * reviewed.
 */

int __wrap_bk_pm_module_vote_power_ctrl(unsigned int module,
                                         int power_state)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t generation;
  FAR bool *enabled;
  enum bk7258_pm_clock_e clock;
  bool enable;
  int ret;

  switch (module)
    {
      case BK7258_SDK_POWER_AUDP_AUDIO:
        clock = BK7258_PM_CLOCK_AUDIO;
        enabled = &g_bk7258_pm_sdk_audio_power_enabled;
        break;

      case BK7258_SDK_POWER_VIDP_JPEG_ENCODER:
        clock = BK7258_PM_CLOCK_JPEG;
        enabled = &g_bk7258_pm_sdk_jpeg_encoder_enabled;
        break;

      case BK7258_SDK_POWER_VIDP_JPEG_DECODER:
        clock = BK7258_PM_CLOCK_JPEG_DECODER;
        enabled = &g_bk7258_pm_sdk_jpeg_decoder_enabled;
        break;

      case BK7258_SDK_POWER_VIDP_DMA2D:
        clock = BK7258_PM_CLOCK_DMA2D;
        enabled = &g_bk7258_pm_sdk_dma2d_enabled;
        break;

      case BK7258_SDK_POWER_VIDP_YUV_BUFFER:
        clock = BK7258_PM_CLOCK_YUV;
        enabled = &g_bk7258_pm_sdk_yuv_buffer_enabled;
        break;

      case BK7258_SDK_POWER_VIDP_ROTATOR:
        clock = BK7258_PM_CLOCK_ROTATOR;
        enabled = &g_bk7258_pm_sdk_rotator_enabled;
        break;

      case BK7258_SDK_POWER_VIDP_SCALE0:
        clock = BK7258_PM_CLOCK_SCALE0;
        enabled = &g_bk7258_pm_sdk_scale0_enabled;
        break;

      case BK7258_SDK_POWER_VIDP_SCALE1:
        clock = BK7258_PM_CLOCK_SCALE1;
        enabled = &g_bk7258_pm_sdk_scale1_enabled;
        break;

      case BK7258_SDK_POWER_VIDP_H264:
        clock = BK7258_PM_CLOCK_H264;
        enabled = &g_bk7258_pm_sdk_h264_enabled;
        break;

      default:
        return __real_bk_pm_module_vote_power_ctrl(module, power_state);
    }

  if (power_state != BK7258_SDK_POWER_STATE_ON &&
      power_state != BK7258_SDK_POWER_STATE_OFF)
    {
      return -EINVAL;
    }

  enable = power_state == BK7258_SDK_POWER_STATE_ON;
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
      bk7258_pm_sdk_reset_generation(generation);
    }

  if (*enabled == enable)
    {
      ret = OK;
      goto out;
    }

  if (enable)
    {
      ret = bk7258_pm_clock_get(clock);
    }
  else
    {
      ret = bk7258_pm_clock_put(clock);
    }

  if (ret >= 0)
    {
      *enabled = enable;
    }

out:
  nxmutex_unlock(&g_bk7258_pm_sdk_lock);
  return ret;
}

#ifdef CONFIG_BK7258_PM_COORDINATED_STANDBY
int __wrap_bk_pm_module_vote_sleep_ctrl(unsigned int module,
                                         uint32_t sleep_state,
                                         uint32_t sleep_time)
{
  struct bk7258_pm_client_s *priv = &g_bk7258_pm_client;
  irqstate_t flags;
  int ret;

  (void)sleep_time;

  /* The official AP implementation publishes an in-flight fixed-address
   * vote, sends PM_SLEEP_CTRL_CMD through the vendor PM mailbox, waits, and
   * then clears that temporary vote.  NuttX owns that mailbox and PM policy;
   * preserve the API's set-state meaning locally and expose one aggregate
   * zero/non-zero gate to CP instead of entering the unused FreeRTOS path.
   * The SDK explicitly treats its AP LOG vote as a no-op.
   */

  if (module == BK7258_SDK_PM_SLEEP_LOG)
    {
      return OK;
    }

  flags = enter_critical_section();
  ret = bk7258_pm_activity_vote(&priv->sdk_activity, module, sleep_state);
  if (ret >= 0)
    {
      bk7258_pm_publish_vendor_votes_locked(priv);
    }

  leave_critical_section(flags);
  return ret;
}
#endif

#endif /* CONFIG_BK7258_PM_CLOCK */
