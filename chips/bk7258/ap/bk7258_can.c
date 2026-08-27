/****************************************************************************
 * board/bk7258/chip/ap/bk7258_can.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP classic-CAN lower-half.  NuttX owns the CAN upper-half, FIFO,
 * ioctl ABI, and device registration.  The immutable v3.1.1.9 AP bundle
 * owns the controller and its interrupt handler; the SDK callbacks are ISR
 * context and therefore only post a semaphore.  A private ordinary NuttX
 * kernel thread drains the SDK's serialized FIFO before calling
 * can_receive().
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sched.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/can/can.h>

#include <common/bk_err.h>
#include <driver/hal/hal_can_types.h>

#include <arch/chip/bk7258_can.h>
#include <arch/chip/bk7258_sdk_abi.h>

#if defined(CONFIG_CAN_EXTID) || defined(CONFIG_CAN_FD)
#  error "BK7258 CAN lower-half requires classic 11-bit CAN configuration"
#endif

/*
 * The v3.1.1.9 public driver/can.h includes SDK-private gpio_driver.h,
 * spinlock.h, and bk_fifo.h, which are not part of the immutable AP bundle.
 * Keep the missing ABI declarations in the board-owned
 * bk7258_sdk_abi.h boundary; all CAN frame and error types below still come
 * from the exported hal_can_types.h and bk_err.h.
 */

/* The public can_types.h cannot be included in the bundle-only build because
 * of its private header dependencies.  Keep its CAN-specific error values
 * expressed from the exported base, without defining SDK names globally. */

#define BK7258_CAN_ERR_NOT_INIT       (BK_ERR_CAN_BASE - 1)
#define BK7258_CAN_ERR_TIMEOUT        (BK_ERR_CAN_BASE - 5)
#define BK7258_CAN_ERR_INVALID_ARG    (BK_ERR_CAN_BASE - 8)

#define BK7258_CAN_FIFO_HEADER        5
#define BK7258_CAN_PAYLOAD_MAX        CAN_20_MAX_PAYLOAD
#define BK7258_CAN_RX_BUDGET          8
struct bk7258_can_s
{
  struct can_dev_s dev;
  mutex_t lifecycle_lock;
  spinlock_t state_lock;
  sem_t rx_sem;
  pid_t rx_thread;

  can_callback_des_t rx_callback;
  can_callback_des_t tx_callback;
  can_callback_des_t err_callback;

  uint8_t rx_header[BK7258_CAN_FIFO_HEADER];
  uint8_t rx_data[BK7258_CAN_PAYLOAD_MAX];
  uint8_t rx_header_len;
  uint8_t rx_data_len;
  uint8_t rx_dlc;
  uint8_t rx_discard_len;
  uint32_t baud;
  int last_error;

  bool sdk_started;
  bool configured;
  bool rx_enabled;
  bool tx_enabled;
  bool rx_thread_stop;
  bool error_pending;
  bool tx_busy;
  bool tx_submitting;
  bool loopback;
};

static int bk7258_can_rx_thread(int argc, FAR char *argv[]);
static void bk7258_can_rx_isr(FAR void *arg);
static void bk7258_can_tx_isr(FAR void *arg);
static void bk7258_can_err_isr(FAR void *arg);

static struct bk7258_can_s g_bk7258_can =
{
  .dev =
    {
      .cd_ops  = NULL,
      .cd_priv = &g_bk7258_can,
    },
  .lifecycle_lock = NXMUTEX_INITIALIZER,
  .state_lock = SP_UNLOCKED,
  .rx_sem = SEM_INITIALIZER(0),
  .rx_thread = -1,
  .baud = 1000000,
};

static int bk7258_can_error(bk_err_t error)
{
  switch (error)
    {
      case BK_OK:
        return 0;

      case BK_ERR_PARAM:
      case BK_ERR_NULL_PARAM:
      case BK7258_CAN_ERR_INVALID_ARG:
        return -EINVAL;

      case BK_ERR_NO_MEM:
        return -ENOMEM;

      case BK_ERR_TIMEOUT:
      case BK7258_CAN_ERR_TIMEOUT:
        return -ETIMEDOUT;

      case BK_ERR_BUSY:
      case BK_ERR_IN_PROGRESS:
        return -EBUSY;

      case BK_ERR_NOT_SUPPORT:
        return -ENOTSUP;

      case BK_ERR_TRY_AGAIN:
        return -EAGAIN;

      case BK_ERR_SHUT_DOWN:
        return -ESHUTDOWN;

      case BK_ERR_NOT_INIT:
      case BK_ERR_NO_DEV:
      case BK7258_CAN_ERR_NOT_INIT:
        return -ENODEV;

      default:
        return -EIO;
    }
}

static bool bk7258_can_active(FAR struct bk7258_can_s *priv)
{
  return priv->sdk_started && priv->configured;
}

/* Return zero for one complete frame, -EAGAIN for a temporary short read. */

static int bk7258_can_discard_payload(FAR struct bk7258_can_s *priv)
{
  uint8_t scratch[BK7258_CAN_PAYLOAD_MAX];
  uint32_t received;
  uint32_t request;
  bk_err_t sdkret;

  while (priv->rx_discard_len > 0u)
    {
      request = priv->rx_discard_len;
      if (request > sizeof(scratch))
        {
          request = sizeof(scratch);
        }

      received = 0u;
      sdkret = bk_can_receive(scratch, request, &received, 0);
      if (received > request)
        {
          priv->rx_discard_len = 0u;
          priv->rx_header_len = 0u;
          priv->rx_data_len = 0u;
          return -EPROTO;
        }

      priv->rx_discard_len -= (uint8_t)received;
      if (priv->rx_discard_len == 0u)
        {
          priv->rx_header_len = 0u;
          priv->rx_data_len = 0u;
          return -EPROTO;
        }

      if (sdkret != BK_OK)
        {
          return bk7258_can_error(sdkret);
        }

      if (received < request)
        {
          return -EAGAIN;
        }
    }

  return -EPROTO;
}

static int bk7258_can_drain_one(FAR struct bk7258_can_s *priv)
{
  uint32_t received;
  bk_err_t sdkret;
  uint32_t id;
  struct can_hdr_s hdr;
  int ret;

  if (priv->rx_discard_len > 0u)
    {
      return bk7258_can_discard_payload(priv);
    }

  if (priv->rx_header_len < BK7258_CAN_FIFO_HEADER)
    {
      sdkret = bk_can_receive(priv->rx_header + priv->rx_header_len,
                              BK7258_CAN_FIFO_HEADER - priv->rx_header_len,
                              &received, 0);
      priv->rx_header_len += received;
      if (priv->rx_header_len < BK7258_CAN_FIFO_HEADER)
        {
          return sdkret == BK_OK ? -EAGAIN : bk7258_can_error(sdkret);
        }
    }

  priv->rx_dlc = priv->rx_header[0];
  if (priv->rx_dlc > BK7258_CAN_PAYLOAD_MAX)
    {
      /* The SDK exposes one byte-stream record as DLC + ID + payload.
       * Discard an invalid record's declared payload before accepting the
       * next header; otherwise payload bytes would permanently desynchronize
       * the stream after a corrupt DLC. */
      priv->rx_discard_len = priv->rx_dlc;
      return bk7258_can_discard_payload(priv);
    }

  if (priv->rx_data_len < priv->rx_dlc)
    {
      sdkret = bk_can_receive(priv->rx_data + priv->rx_data_len,
                              priv->rx_dlc - priv->rx_data_len, &received,
                              0);
      priv->rx_data_len += received;
      if (priv->rx_data_len < priv->rx_dlc)
        {
          return sdkret == BK_OK ? -EAGAIN : bk7258_can_error(sdkret);
        }
    }

  id = (uint32_t)priv->rx_header[1] |
       ((uint32_t)priv->rx_header[2] << 8) |
       ((uint32_t)priv->rx_header[3] << 16) |
       ((uint32_t)priv->rx_header[4] << 24);
  if (id > CAN_SFF_MASK)
    {
      ret = -EPROTO;
    }
  else
    {
      memset(&hdr, 0, sizeof(hdr));
      hdr.ch_id = id;
      hdr.ch_dlc = priv->rx_dlc;
      /* The SDK FIFO format is DLC + ID + payload and has no per-record
       * IDE/RTR/FDF bits.  This lower-half therefore delivers data-only
       * standard frames; remote/FD metadata is not invented. */
      hdr.ch_rtr = 0;
#ifdef CONFIG_CAN_ERRORS
      hdr.ch_error = 0;
#endif
#ifdef CONFIG_CAN_EXTID
      hdr.ch_extid = 0;
#endif
#ifdef CONFIG_CAN_TIMESTAMP
      {
        struct timespec ts;

        if (clock_systime_timespec(&ts) == 0)
          {
            hdr.ch_ts.tv_sec = ts.tv_sec;
            hdr.ch_ts.tv_usec = ts.tv_nsec / 1000;
          }
      }
#endif
      ret = can_receive(&priv->dev, &hdr, priv->rx_data);
    }

  priv->rx_header_len = 0;
  priv->rx_data_len = 0;
  priv->rx_discard_len = 0;
  return ret < 0 ? ret : 0;
}

static void bk7258_can_report_error(FAR struct bk7258_can_s *priv)
{
#ifdef CONFIG_CAN_ERRORS
  struct can_hdr_s hdr;
  uint8_t data[CAN_ERR_DLC];

  memset(&hdr, 0, sizeof(hdr));
  memset(data, 0, sizeof(data));
  hdr.ch_id = CAN_ERROR_CONTROLLER;
  hdr.ch_dlc = CAN_ERROR_DLC;
  hdr.ch_error = 1;
  data[1] = CAN_ERROR1_UNSPEC;
  (void)can_receive(&priv->dev, &hdr, data);
#else
  (void)priv;
#endif
}

static int bk7258_can_rx_thread(int argc, FAR char *argv[])
{
  FAR struct bk7258_can_s *priv = &g_bk7258_can;
  irqstate_t flags;
  unsigned int count;
  int ret;

  (void)argc;
  (void)argv;

  for (;;)
    {
      ret = nxsem_wait(&priv->rx_sem);
      if (ret < 0 && ret != -EINTR)
        {
          continue;
        }

      flags = spin_lock_irqsave(&priv->state_lock);
      if (priv->rx_thread_stop)
        {
          spin_unlock_irqrestore(&priv->state_lock, flags);
          break;
        }
      if (!bk7258_can_active(priv) || !priv->rx_enabled)
        {
          spin_unlock_irqrestore(&priv->state_lock, flags);
          continue;
        }
      if (priv->error_pending)
        {
          priv->error_pending = false;
          spin_unlock_irqrestore(&priv->state_lock, flags);
          bk7258_can_report_error(priv);
        }
      else
        {
          spin_unlock_irqrestore(&priv->state_lock, flags);
        }

      do
        {
          for (count = 0; count < BK7258_CAN_RX_BUDGET; count++)
            {
              ret = bk7258_can_drain_one(priv);
              if (ret < 0)
                {
                  if (ret != -EAGAIN && ret != -ETIMEDOUT)
                    {
                      flags = spin_lock_irqsave(&priv->state_lock);
                      priv->last_error = ret;
                      spin_unlock_irqrestore(&priv->state_lock, flags);
                    }
                  break;
                }
            }
        }
      while (count == BK7258_CAN_RX_BUDGET);
    }

  return 0;
}

static void bk7258_can_stop_thread(FAR struct bk7258_can_s *priv)
{
  irqstate_t flags;
  pid_t thread;

  flags = spin_lock_irqsave(&priv->state_lock);
  thread = priv->rx_thread;
  priv->rx_thread = -1;
  priv->rx_thread_stop = true;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  if (thread > 0)
    {
      (void)nxsem_post(&priv->rx_sem);
      (void)kthread_delete(thread);
    }
}

static void bk7258_can_rx_isr(FAR void *arg)
{
  FAR struct bk7258_can_s *priv = arg;

  /* The SDK invokes this callback from can_isr().  Do not inspect or drain
   * the SDK FIFO here: bk_can_receive() may wait on an RTOS semaphore. */
  (void)nxsem_post(&priv->rx_sem);
}

static void bk7258_can_tx_isr(FAR void *arg)
{
  FAR struct bk7258_can_s *priv = arg;
  irqstate_t flags;
  bool done = false;

  flags = spin_lock_irqsave(&priv->state_lock);
  if (priv->tx_busy)
    {
      priv->tx_busy = false;
      done = priv->tx_enabled && bk7258_can_active(priv);
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);

  if (done)
    {
      (void)can_txdone(&priv->dev);
    }
}

static void bk7258_can_err_isr(FAR void *arg)
{
  FAR struct bk7258_can_s *priv = arg;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->state_lock);
  if (bk7258_can_active(priv) && priv->rx_enabled)
    {
      priv->error_pending = true;
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);

  (void)nxsem_post(&priv->rx_sem);
}

static void bk7258_can_quiet_isr(FAR void *arg)
{
  (void)arg;
}

static void bk7258_can_reset(FAR struct can_dev_s *dev)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->state_lock);
  priv->configured = false;
  priv->rx_enabled = false;
  priv->tx_enabled = false;
  priv->tx_busy = false;
  priv->tx_submitting = false;
  priv->error_pending = false;
  priv->rx_header_len = 0;
  priv->rx_data_len = 0;
  priv->rx_discard_len = 0;
  spin_unlock_irqrestore(&priv->state_lock, flags);
  bk7258_can_stop_thread(priv);
}

static int bk7258_can_setup(FAR struct can_dev_s *dev)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;
  irqstate_t flags;
  char *argv[1] = { NULL };
  bool configured;
  int ret;

  flags = spin_lock_irqsave(&priv->state_lock);
  if (!priv->sdk_started || priv->configured)
    {
      configured = priv->configured;
      spin_unlock_irqrestore(&priv->state_lock, flags);
      return configured ? -EBUSY : -ENODEV;
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);

#ifdef CONFIG_CAN_LOOPBACK
  can_hal_set_lbmi(1);
  priv->loopback = true;
#else
  can_hal_set_lbmi(0);
  priv->loopback = false;
#endif
  /* bk_can_driver_init() leaves SDK filter 0 in its documented accept-all
   * state.  Do not rewrite it with an unproven mask encoding here. */

  (void)nxsem_reset(&priv->rx_sem, 0);
  flags = spin_lock_irqsave(&priv->state_lock);
  priv->rx_thread_stop = false;
  spin_unlock_irqrestore(&priv->state_lock, flags);
  ret = kthread_create("bk7258-can-rx", SCHED_PRIORITY_DEFAULT, 2048,
                       bk7258_can_rx_thread, argv);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&priv->state_lock);
  priv->rx_thread = (pid_t)ret;
  priv->configured = true;
  priv->rx_enabled = false;
  priv->tx_enabled = false;
  priv->tx_busy = false;
  priv->tx_submitting = false;
  priv->rx_header_len = 0;
  priv->rx_data_len = 0;
  priv->rx_discard_len = 0;
  spin_unlock_irqrestore(&priv->state_lock, flags);
  return 0;
}

static void bk7258_can_shutdown(FAR struct can_dev_s *dev)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->state_lock);
  priv->configured = false;
  priv->rx_enabled = false;
  priv->tx_enabled = false;
  priv->error_pending = false;
  priv->tx_busy = false;
  priv->tx_submitting = false;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  bk7258_can_stop_thread(priv);
  (void)bk_can_abort_all();
}

static void bk7258_can_rxint(FAR struct can_dev_s *dev, bool enable)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->state_lock);
  priv->rx_enabled = enable && priv->configured;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  if (enable)
    {
      (void)nxsem_post(&priv->rx_sem);
    }
}

static void bk7258_can_txint(FAR struct can_dev_s *dev, bool enable)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->state_lock);
  priv->tx_enabled = enable && priv->configured;
  spin_unlock_irqrestore(&priv->state_lock, flags);
}

static int bk7258_can_ioctl(FAR struct can_dev_s *dev, int cmd,
                            unsigned long arg)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;
  FAR struct canioc_connmodes_s *modes;
  FAR struct canioc_bittiming_s *timing;
  can_bit_rate_e speed;
  bk_err_t sdkret;
  irqstate_t flags;

  switch (cmd)
    {
      case CANIOC_GET_CONNMODES:
        modes = (FAR struct canioc_connmodes_s *)(uintptr_t)arg;
        if (modes == NULL)
          {
            return -EINVAL;
          }
        modes->bm_loopback = can_hal_get_lbmi() != 0;
        modes->bm_silent = 0;
        return 0;

      case CANIOC_SET_CONNMODES:
        modes = (FAR struct canioc_connmodes_s *)(uintptr_t)arg;
        if (modes == NULL)
          {
            return -EINVAL;
          }
        if (modes->bm_silent)
          {
            return -ENOTSUP;
          }
        flags = spin_lock_irqsave(&priv->state_lock);
        if (priv->tx_busy || !priv->configured)
          {
            spin_unlock_irqrestore(&priv->state_lock, flags);
            return priv->configured ? -EBUSY : -ENODEV;
          }
        spin_unlock_irqrestore(&priv->state_lock, flags);
        can_hal_set_lbmi(modes->bm_loopback != 0);
        flags = spin_lock_irqsave(&priv->state_lock);
        priv->loopback = modes->bm_loopback != 0;
        spin_unlock_irqrestore(&priv->state_lock, flags);
        return 0;

      case CANIOC_GET_BITTIMING:
        timing = (FAR struct canioc_bittiming_s *)(uintptr_t)arg;
        if (timing == NULL)
          {
            return -EINVAL;
          }
        memset(timing, 0, sizeof(*timing));
        flags = spin_lock_irqsave(&priv->state_lock);
        timing->bt_baud = priv->baud;
        spin_unlock_irqrestore(&priv->state_lock, flags);
        return 0;

      case CANIOC_SET_BITTIMING:
        timing = (FAR struct canioc_bittiming_s *)(uintptr_t)arg;
        if (timing == NULL || timing->bt_tseg1 != 0 ||
            timing->bt_tseg2 != 0 || timing->bt_sjw != 0)
          {
            return -ENOTSUP;
          }
        switch (timing->bt_baud)
          {
            case 125000:  speed = CAN_BR_125K; break;
            case 250000:  speed = CAN_BR_250K; break;
            case 500000:  speed = CAN_BR_500K; break;
            case 800000:  speed = CAN_BR_800K; break;
            case 1000000: speed = CAN_BR_1M; break;
            default:      return -ENOTSUP;
          }
        flags = spin_lock_irqsave(&priv->state_lock);
        if (!priv->configured)
          {
            spin_unlock_irqrestore(&priv->state_lock, flags);
            return -ENODEV;
          }
        if (priv->tx_busy)
          {
            spin_unlock_irqrestore(&priv->state_lock, flags);
            return -EBUSY;
          }
        spin_unlock_irqrestore(&priv->state_lock, flags);
        sdkret = can_driver_bit_rate_config(speed, CAN_BR_4M);
        if (sdkret != BK_OK)
          {
            return bk7258_can_error(sdkret);
          }
        priv->baud = timing->bt_baud;
        return 0;

      case CANIOC_BUSOFF_RECOVERY:
        sdkret = can_hal_ctrl(CMD_CAN_BUSOFF_CLR, NULL);
        return bk7258_can_error(sdkret);

      case CANIOC_ADD_EXTFILTER:
      case CANIOC_DEL_EXTFILTER:
        return -ENOTSUP;

      case CANIOC_ADD_STDFILTER:
      case CANIOC_DEL_STDFILTER:
        return -ENOTSUP;

      default:
        return -ENOTTY;
    }
}

static int bk7258_can_send(FAR struct can_dev_s *dev,
                           FAR struct can_msg_s *msg)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;
  can_frame_s frame;
  bk_err_t sdkret;
  irqstate_t flags;
  int ret;

  if (msg == NULL || msg->cm_hdr.ch_dlc > BK7258_CAN_PAYLOAD_MAX ||
      msg->cm_hdr.ch_rtr)
    {
      return msg != NULL && msg->cm_hdr.ch_rtr ? -ENOTSUP : -EINVAL;
    }
#ifdef CONFIG_CAN_EXTID
  if (msg->cm_hdr.ch_extid)
    {
      return -ENOTSUP;
    }
#endif
#ifdef CONFIG_CAN_FD
  if (msg->cm_hdr.ch_edl || msg->cm_hdr.ch_brs || msg->cm_hdr.ch_esi)
    {
      return -ENOTSUP;
    }
#endif
  if (msg->cm_hdr.ch_id > CAN_SFF_MASK)
    {
      return -EINVAL;
    }

  memset(&frame, 0, sizeof(frame));
  frame.tag.id = msg->cm_hdr.ch_id;
  frame.tag.ide = 0;
  frame.tag.rtr = 0;
  frame.tag.fdf = FDF_CAN_20;
  frame.size = msg->cm_hdr.ch_dlc;
  frame.data = msg->cm_data;

  flags = spin_lock_irqsave(&priv->state_lock);
  if (!bk7258_can_active(priv))
    {
      spin_unlock_irqrestore(&priv->state_lock, flags);
      return -ENODEV;
    }
  if (priv->tx_busy || priv->tx_submitting)
    {
      spin_unlock_irqrestore(&priv->state_lock, flags);
      return -EBUSY;
    }
  priv->tx_busy = true;
  priv->tx_submitting = true;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  /* PTB is the SDK's non-blocking single-frame path. */
  sdkret = bk_can_send_ptb(&frame);
  ret = bk7258_can_error(sdkret);

  flags = spin_lock_irqsave(&priv->state_lock);
  priv->tx_submitting = false;
  if (ret < 0)
    {
      priv->tx_busy = false;
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);
  return ret;
}

static bool bk7258_can_txready(FAR struct can_dev_s *dev)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;
  irqstate_t flags;
  bool ready;

  flags = spin_lock_irqsave(&priv->state_lock);
  ready = bk7258_can_active(priv) && !priv->tx_busy &&
          !priv->tx_submitting;
  spin_unlock_irqrestore(&priv->state_lock, flags);
  return ready;
}

static bool bk7258_can_txempty(FAR struct can_dev_s *dev)
{
  return bk7258_can_txready(dev);
}

static bool bk7258_can_cancel(FAR struct can_dev_s *dev,
                              FAR struct can_msg_s *msg)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;
  irqstate_t flags;
  bk_err_t sdkret;

  (void)msg;
  flags = spin_lock_irqsave(&priv->state_lock);
  if (!priv->tx_busy || priv->tx_submitting)
    {
      spin_unlock_irqrestore(&priv->state_lock, flags);
      return false;
    }
  spin_unlock_irqrestore(&priv->state_lock, flags);

  sdkret = bk_can_abort_ptb();
  if (sdkret == BK_OK)
    {
      flags = spin_lock_irqsave(&priv->state_lock);
      priv->tx_busy = false;
      spin_unlock_irqrestore(&priv->state_lock, flags);
      return true;
    }

  return false;
}

static void bk7258_can_errhandle(FAR struct can_dev_s *dev)
{
  FAR struct bk7258_can_s *priv = dev->cd_priv;

  (void)nxsem_post(&priv->rx_sem);
}

static int bk7258_can_remoterequest(FAR struct can_dev_s *dev,
                                    uint16_t id)
{
  (void)dev;
  (void)id;
  return -ENOTSUP;
}

static const struct can_ops_s g_bk7258_can_ops =
{
  .co_reset          = bk7258_can_reset,
  .co_setup          = bk7258_can_setup,
  .co_shutdown       = bk7258_can_shutdown,
  .co_rxint          = bk7258_can_rxint,
  .co_txint          = bk7258_can_txint,
  .co_ioctl          = bk7258_can_ioctl,
  .co_remoterequest  = bk7258_can_remoterequest,
  .co_send           = bk7258_can_send,
  .co_txready        = bk7258_can_txready,
  .co_txempty        = bk7258_can_txempty,
  .co_cancel         = bk7258_can_cancel,
  .co_errhandle      = bk7258_can_errhandle,
};

int bk7258_can_initialize(FAR struct can_dev_s **dev)
{
  FAR struct bk7258_can_s *priv = &g_bk7258_can;
  int ret;
  bk_err_t sdkret;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lifecycle_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->sdk_started)
    {
      nxmutex_unlock(&priv->lifecycle_lock);
      return -EBUSY;
    }

  sdkret = bk_can_driver_init();
  if (sdkret != BK_OK)
    {
      (void)bk_can_driver_deinit();
      nxmutex_unlock(&priv->lifecycle_lock);
      return bk7258_can_error(sdkret);
    }

  priv->rx_callback.cb = bk7258_can_rx_isr;
  priv->rx_callback.param = priv;
  priv->tx_callback.cb = bk7258_can_tx_isr;
  priv->tx_callback.param = priv;
  priv->err_callback.cb = bk7258_can_err_isr;
  priv->err_callback.param = priv;
  bk_can_register_isr_callback(&priv->rx_callback, &priv->tx_callback);
  bk_can_register_err_callback(&priv->err_callback);

#ifdef CONFIG_CAN_LOOPBACK
  can_hal_set_lbmi(1);
  priv->loopback = true;
#else
  can_hal_set_lbmi(0);
  priv->loopback = false;
#endif
  priv->baud = 1000000;
  priv->sdk_started = true;
  priv->configured = false;
  priv->dev.cd_ops = &g_bk7258_can_ops;
  *dev = &priv->dev;
  nxmutex_unlock(&priv->lifecycle_lock);
  return 0;
}

int bk7258_can_uninitialize(FAR struct can_dev_s *dev)
{
  FAR struct bk7258_can_s *priv = &g_bk7258_can;
  irqstate_t flags;
  int ret;

  if (dev != &priv->dev)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lifecycle_lock);
  if (ret < 0)
    {
      return ret;
    }
  if (priv->dev.cd_crefs != 0)
    {
      nxmutex_unlock(&priv->lifecycle_lock);
      return -EBUSY;
    }
  if (!priv->sdk_started)
    {
      nxmutex_unlock(&priv->lifecycle_lock);
      return -ENODEV;
    }

  flags = spin_lock_irqsave(&priv->state_lock);
  priv->sdk_started = false;
  priv->configured = false;
  priv->rx_enabled = false;
  priv->tx_enabled = false;
  priv->error_pending = false;
  priv->tx_busy = false;
  priv->tx_submitting = false;
  spin_unlock_irqrestore(&priv->state_lock, flags);

  bk7258_can_stop_thread(priv);
  {
    can_callback_des_t quiet;

    quiet.cb = bk7258_can_quiet_isr;
    quiet.param = NULL;
    bk_can_register_isr_callback(&quiet, &quiet);
    bk_can_register_err_callback(&quiet);
  }
  can_hal_set_lbmi(0);
  (void)bk_can_abort_all();
  ret = bk7258_can_error(bk_can_driver_deinit());
  nxmutex_unlock(&priv->lifecycle_lock);
  return ret;
}
