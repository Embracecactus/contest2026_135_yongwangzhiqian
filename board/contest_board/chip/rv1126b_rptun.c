/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/rv1126b_rptun.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * RV1126B RPTun glue for AMP (HPMCU NuttX <-> Linux A-core)
 *
 * Architecture:
 *   HPMCU is the remote/device (is_master=false).  Linux is the host.
 *   A private static resource table lives in HPMCU DRAM, initialized
 *   at boot -- NOT read from shared memory.  The table describes fixed
 *   vring addresses that match the Linux-side amp.dtsi.
 *
 * Shared memory layout:
 *   Base:   CONFIG_RV1126B_RPTUN_SHM_ADDR (0x48c3c000)
 *   Size:   CONFIG_RV1126B_RPTUN_SHM_SIZE (128KB / 0x20000)
 *
 * Vrings (addresses from Linux amp.dtsi contract):
 *   vring0: DA 0x48c3c000, num 64, align 0x1000, notifyid 0
 *   vring1: DA 0x48c44000, num 64, align 0x1000, notifyid 1
 *
 * Linux pool (0x48c4c000 / 0x10000) is NOT described in the resource
 * table; descriptor.addr is accessed through default metal IO identity.
 *
 * Lifecycle:
 *   1. rv1126b_rptun_init() -- prepare static table + ops, init mailbox
 *      (IRQ disabled), register with rptun framework.
 *   2. Linux writes first handshake via MBOX7 A2B -> ISR sets DRIVER_OK
 *      on the vdev status byte (consumed internally, not forwarded).
 *   3. rp_set_callback(cb != NULL) -- store arg, then cb, then
 *      enable_and_drain() mailbox.  First pending handshake becomes
 *      linux_ready; later events -> generic callback.
 *   4. rp_notify(vqid) -> mailbox notify(vqid).
 *   5. rp_set_callback(NULL) -- disable mailbox, clear cb/arg.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/nuttx.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/rptun/rptun.h>
#include <nuttx/spinlock.h>

#include <arch/barriers.h>

#include <openamp/remoteproc.h>
#include <openamp/rpmsg_virtio.h>
#include <openamp/virtio.h>

#include "rv1126b_mailbox.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define rpinfo rpmsginfo
#define rpwarn rpmsgwarn
#define rperr  rpmsgerr

/* SHM base / size from Kconfig */

#define SHM_ADDR   CONFIG_RV1126B_RPTUN_SHM_ADDR
#define SHM_SIZE   CONFIG_RV1126B_RPTUN_SHM_SIZE

/* Derived vring addresses (fixed contract with Linux amp.dtsi) */

#define VRING0_DA  0x48c3c000u
#define VRING1_DA  0x48c44000u
#define POOL_DA    0x48c4c000u
#define POOL_SIZE  0x10000u

/* Vring parameters */

#define VRING_NUM   64
#define VRING_ALIGN 0x1000

/* Local CPU name for config space (host_cpuname) */

#define LOCAL_CPUNAME  "hpmcu"

/* Compile-time partition boundary checks */

#define SHM_END  (SHM_ADDR + SHM_SIZE)

#if (SHM_ADDR & 0xFFF) != 0
#  error "CONFIG_RV1126B_RPTUN_SHM_ADDR must be 4 KiB aligned"
#endif

#if SHM_SIZE != 0x20000
#  error "CONFIG_RV1126B_RPTUN_SHM_SIZE must be 131072 (128 KiB)"
#endif

#if VRING0_DA < SHM_ADDR || VRING0_DA >= SHM_END
#  error "vring0 DA 0x48c3c000 out of SHM bounds"
#endif

#if VRING1_DA < SHM_ADDR || VRING1_DA >= SHM_END
#  error "vring1 DA 0x48c44000 out of SHM bounds"
#endif

#if POOL_DA < SHM_ADDR || (POOL_DA + POOL_SIZE) > SHM_END
#  error "Linux pool 0x48c4c000/0x10000 out of SHM bounds"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum rv1126b_rptun_state_e
{
  RV1126B_RPTUN_STATE_UNINIT      = 0,
  RV1126B_RPTUN_STATE_INITIALIZED = 1,
  RV1126B_RPTUN_STATE_LINUX_READY = 2,
};

struct rv1126b_rptun_dev_s
{
  struct rptun_dev_s              rptun;
  rptun_callback_t                callback;
  void                           *arg;
  unsigned int                    state;
  bool                            pending_notify;
  char                            peername[RPMSG_NAME_SIZE + 1];
};

#define to_rv1126b_rptun(d) \
  container_of(d, struct rv1126b_rptun_dev_s, rptun)

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static const char      *rp_get_cpuname(struct rptun_dev_s *dev);
static const char      *rp_get_firmware(struct rptun_dev_s *dev);
static struct resource_table *rp_get_resource(struct rptun_dev_s *dev);
static bool             rp_is_autostart(struct rptun_dev_s *dev);
static bool             rp_is_master(struct rptun_dev_s *dev);
static int              rp_start(struct rptun_dev_s *dev);
static int              rp_stop(struct rptun_dev_s *dev);
static int              rp_notify(struct rptun_dev_s *dev, uint32_t vqid);
static int              rp_set_callback(struct rptun_dev_s *dev,
                                        rptun_callback_t cb, void *arg);
static void             rv1126b_rptun_mbox_cb(void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Static resource table in HPMCU DRAM -- initialized once at boot,
 * long-lived and writable (status byte is updated at runtime).
 */

static struct rptun_rsc_s g_rptun_rsc;

/* Ops table -- installed in rptun_dev_s.ops */

static const struct rptun_ops_s g_rv1126b_rptun_ops =
{
  .get_cpuname       = rp_get_cpuname,
  .get_firmware      = rp_get_firmware,
  .get_addrenv       = NULL,
  .get_resource      = rp_get_resource,
  .is_autostart      = rp_is_autostart,
  .is_master         = rp_is_master,
  .config            = NULL,
  .start             = rp_start,
  .stop              = rp_stop,
  .notify            = rp_notify,
  .register_callback = rp_set_callback,
  .reset             = NULL,
  .set_phase         = NULL,
  .get_phase         = NULL,
};

static struct rv1126b_rptun_dev_s g_rptun_dev;

static spinlock_t g_rptun_lock = SP_UNLOCKED;

/****************************************************************************
 * Private Functions -- Resource Table
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_rptun_init_resource_table
 *
 * One-time initialization of the private static resource table.
 * Describes a single VDEV with two vrings at fixed Linux-contract
 * addresses.  No carveout; no shared-memory overlay.
 *
 ****************************************************************************/

static void rv1126b_rptun_init_resource_table(struct rptun_rsc_s *rsc,
                                               const char *peername)
{
  size_t len;

  memset(rsc, 0, sizeof(*rsc));

  /* Header: version 1, one entry */

  rsc->rsc_tbl_hdr.ver         = 1;
  rsc->rsc_tbl_hdr.num         = 1;
  rsc->rsc_tbl_hdr.reserved[0] = 0;
  rsc->rsc_tbl_hdr.reserved[1] = 0;

  /* offset[0] -> rpmsg_vdev */

  rsc->offset[0] = offsetof(struct rptun_rsc_s, rpmsg_vdev);

  /* VDEV */

  rsc->rpmsg_vdev.type          = RSC_VDEV;
  rsc->rpmsg_vdev.id            = VIRTIO_ID_RPMSG;
  rsc->rpmsg_vdev.notifyid      = 2;

  /* Features: NS (bit0) + CPUNAME (bit3) => 0x9.
   * Explicitly exclude ACK, BUFSZ, BUFADDR, PRIORITY.
   */

  rsc->rpmsg_vdev.dfeatures     = (1u << VIRTIO_RPMSG_F_NS)
                                | (1u << VIRTIO_RPMSG_F_CPUNAME);
  rsc->rpmsg_vdev.gfeatures     = (1u << VIRTIO_RPMSG_F_NS)
                                | (1u << VIRTIO_RPMSG_F_CPUNAME);

  rsc->rpmsg_vdev.config_len    = sizeof(struct fw_rsc_config);
  rsc->rpmsg_vdev.num_of_vrings = 2;
  rsc->rpmsg_vdev.status        = 0;  /* DRIVER_OK set on first handshake */

  /* Vring 0 */

  rsc->rpmsg_vring0.da          = VRING0_DA;
  rsc->rpmsg_vring0.align       = VRING_ALIGN;
  rsc->rpmsg_vring0.num         = VRING_NUM;
  rsc->rpmsg_vring0.notifyid    = 0;
  rsc->rpmsg_vring0.reserved    = 0;

  /* Vring 1 */

  rsc->rpmsg_vring1.da          = VRING1_DA;
  rsc->rpmsg_vring1.align       = VRING_ALIGN;
  rsc->rpmsg_vring1.num         = VRING_NUM;
  rsc->rpmsg_vring1.notifyid    = 1;
  rsc->rpmsg_vring1.reserved    = 0;

  /* Config: buffer sizes = 512 each direction */

  rsc->config.h2r_buf_size      = 512;
  rsc->config.r2h_buf_size      = 512;

  /* Host CPU name: Linux peer owns the VIRTIO_DEV_DRIVER role */

  len = strlen(peername);
  if (len >= VIRTIO_RPMSG_CPUNAME_SIZE)
    {
      len = VIRTIO_RPMSG_CPUNAME_SIZE - 1;
    }

  memcpy(rsc->config.host_cpuname, peername, len);
  rsc->config.host_cpuname[len] = '\0';

  /* Remote CPU name: local HPMCU owns the VIRTIO_DEV_DEVICE role */

  len = strlen(LOCAL_CPUNAME);
  if (len >= VIRTIO_RPMSG_CPUNAME_SIZE)
    {
      len = VIRTIO_RPMSG_CPUNAME_SIZE - 1;
    }

  memcpy(rsc->config.remote_cpuname, LOCAL_CPUNAME, len);
  rsc->config.remote_cpuname[len] = '\0';
}

/****************************************************************************
 * Private Functions -- rptun_ops_s callbacks
 ****************************************************************************/

static const char *rp_get_cpuname(struct rptun_dev_s *dev)
{
  struct rv1126b_rptun_dev_s *priv = to_rv1126b_rptun(dev);
  return priv->peername;
}

static const char *rp_get_firmware(struct rptun_dev_s *dev)
{
  UNUSED(dev);
  return NULL;
}

static struct resource_table *rp_get_resource(struct rptun_dev_s *dev)
{
  UNUSED(dev);

  /* Return the private static table -- always ready, no shmem probing */

  return &g_rptun_rsc.rsc_tbl_hdr;
}

static bool rp_is_autostart(struct rptun_dev_s *dev)
{
  UNUSED(dev);
  return true;
}

static bool rp_is_master(struct rptun_dev_s *dev)
{
  UNUSED(dev);
  return false;  /* HPMCU is always remote/device */
}

static int rp_start(struct rptun_dev_s *dev)
{
  rpinfo("start\n");
  UNUSED(dev);
  return 0;
}

static int rp_stop(struct rptun_dev_s *dev)
{
  rpinfo("stop\n");
  UNUSED(dev);
  return 0;
}

static int rp_notify(struct rptun_dev_s *dev, uint32_t vqid)
{
  UNUSED(dev);
  return rv1126b_mailbox_notify(vqid);
}

static int rp_set_callback(struct rptun_dev_s *dev, rptun_callback_t cb,
                           void *arg)
{
  struct rv1126b_rptun_dev_s *priv = to_rv1126b_rptun(dev);
  irqstate_t flags;

  if (cb != NULL)
    {
      /* Register: store arg first, then callback, under irqsave */

      flags = spin_lock_irqsave(&g_rptun_lock);

      priv->arg             = arg;
      priv->callback        = cb;
      priv->pending_notify  = false;

      spin_unlock_irqrestore(&g_rptun_lock, flags);

      /* Enable RX and drain any already-latched pending handshake.
       * enable_and_drain() does its own locking.  The first valid
       * handshake sets linux_ready / DRIVER_OK and is swallowed;
       * additional pending events trigger the generic callback.
       */

      rv1126b_mailbox_enable_and_drain();
    }
  else
    {
      /* Unregister: first disable mailbox RX (stop new interrupts),
       * then clear callback and arg under irqsave.
       */

      rv1126b_mailbox_disable();

      flags = spin_lock_irqsave(&g_rptun_lock);

      priv->callback        = NULL;
      priv->arg             = NULL;
      priv->pending_notify  = false;

      spin_unlock_irqrestore(&g_rptun_lock, flags);
    }

  return 0;
}

/****************************************************************************
 * Name: rv1126b_rptun_mbox_cb
 *
 * Mailbox RX callback from ISR context.
 *
 * First valid event:
 *   Sets vdev status to DRIVER_OK, marks linux_ready, SWALLOWED.
 *
 * Subsequent events:
 *   If callback registered: snapshot, invoke outside lock with
 *   RPTUN_NOTIFY_ALL.  Otherwise defer via pending_notify flag.
 *
 ****************************************************************************/

static void rv1126b_rptun_mbox_cb(void *arg)
{
  struct rv1126b_rptun_dev_s *dev = arg;
  rptun_callback_t cb;
  void             *cb_arg;
  irqstate_t        flags;

  flags = spin_lock_irqsave(&g_rptun_lock);

  if (dev->state < RV1126B_RPTUN_STATE_INITIALIZED)
    {
      spin_unlock_irqrestore(&g_rptun_lock, flags);
      return;
    }

  if (dev->state < RV1126B_RPTUN_STATE_LINUX_READY)
    {
      /* First valid handshake: set DRIVER_OK, mark ready, swallow */

      g_rptun_rsc.rpmsg_vdev.status = VIRTIO_CONFIG_STATUS_DRIVER_OK;

      UP_DSB();

      dev->state = RV1126B_RPTUN_STATE_LINUX_READY;

      rpinfo("linux ready: DRIVER_OK set\n");

      spin_unlock_irqrestore(&g_rptun_lock, flags);
      return;
    }

  /* Subsequent event -- forward to generic callback if registered */

  if (dev->callback != NULL)
    {
      cb    = dev->callback;
      cb_arg = dev->arg;
      dev->pending_notify = false;

      spin_unlock_irqrestore(&g_rptun_lock, flags);

      cb(cb_arg, RPTUN_NOTIFY_ALL);
    }
  else
    {
      dev->pending_notify = true;

      spin_unlock_irqrestore(&g_rptun_lock, flags);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int rv1126b_rptun_init(const char *peername)
{
  struct rv1126b_rptun_dev_s *dev = &g_rptun_dev;
  irqstate_t flags;
  int ret;

  if (peername == NULL || peername[0] == '\0')
    {
      rperr("invalid peername\n");
      return -EINVAL;
    }

  if (strlen(peername) > RPMSG_NAME_SIZE)
    {
      rperr("peername too long (max %u)\n", RPMSG_NAME_SIZE);
      return -EINVAL;
    }

  /* Reject duplicate initialization */

  flags = spin_lock_irqsave(&g_rptun_lock);

  if (dev->state >= RV1126B_RPTUN_STATE_INITIALIZED)
    {
      spin_unlock_irqrestore(&g_rptun_lock, flags);
      rperr("already initialized\n");
      return -EBUSY;
    }

  spin_unlock_irqrestore(&g_rptun_lock, flags);

  /* Prepare static resource table (one-time init with peername) */

  rv1126b_rptun_init_resource_table(&g_rptun_rsc, peername);

  /* Initialize device private data */

  memset(dev, 0, sizeof(*dev));

  strncpy(dev->peername, peername, RPMSG_NAME_SIZE);
  dev->peername[RPMSG_NAME_SIZE] = '\0';

  dev->rptun.ops = &g_rv1126b_rptun_ops;

  /* Mark initialized before mailbox init so callback can run safely */

  flags = spin_lock_irqsave(&g_rptun_lock);
  dev->state = RV1126B_RPTUN_STATE_INITIALIZED;
  spin_unlock_irqrestore(&g_rptun_lock, flags);

  /* Initialize mailbox (attaches ISR, keeps IRQ disabled) */

  ret = rv1126b_mailbox_init(rv1126b_rptun_mbox_cb, dev);
  if (ret < 0)
    {
      rperr("mailbox_init failed: %d\n", ret);
      goto err_state;
    }

  /* Register with RPTun framework */

  ret = rptun_initialize(&dev->rptun);
  if (ret < 0)
    {
      rperr("rptun_initialize failed: %d\n", ret);
      goto err_mbox;
    }

  rpinfo("rptun init: peer=%s shmem=0x%08lx vring0=0x%08lx vring1=0x%08lx\n",
         peername, (unsigned long)SHM_ADDR,
         (unsigned long)VRING0_DA, (unsigned long)VRING1_DA);

  return OK;

err_mbox:
  rv1126b_mailbox_deinit();

err_state:
  flags = spin_lock_irqsave(&g_rptun_lock);
  dev->state = RV1126B_RPTUN_STATE_UNINIT;
  spin_unlock_irqrestore(&g_rptun_lock, flags);

  return ret;
}
