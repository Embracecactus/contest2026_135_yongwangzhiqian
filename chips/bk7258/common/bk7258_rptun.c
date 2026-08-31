/****************************************************************************
 * chips/bk7258/common/
 * bk7258_rptun.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP/AP NuttX RPTUN lower half.  The Beken SDK mailbox-channel
 * wrapper supplies edge notifications; shared SRAM pending words retain the
 * delivery truth if an edge is coalesced or the hardware FIFO is busy.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_RPTUN

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <nuttx/nuttx.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/rptun/rptun.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_amp.h>
#include <arch/chip/bk7258_ota_rpmsg.h>
#include <arch/chip/bk7258_rptun.h>

#include "bk7258_rptun.h"
#ifdef CONFIG_BK7258_AP_SUPERVISOR
#  include "bk7258_rpmsg_health.h"
#endif
#include "bk7258_rptun_mbox.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_BK7258_AP_CORE
#  define BK7258_RPTUN_REMOTE_NAME "cp"
#else
#  define BK7258_RPTUN_REMOTE_NAME "ap"
#endif

#define BK7258_RPTUN_FEATURES \
  ((1u << VIRTIO_RPMSG_F_NS) | (1u << VIRTIO_RPMSG_F_ACK) | \
   (1u << VIRTIO_RPMSG_F_BUFSZ) | (1u << VIRTIO_RPMSG_F_CPUNAME))

#define BK7258_RPTUN_NOTIFY_VALID \
  (BK7258_RPTUN_NOTIFY_VRING0 | BK7258_RPTUN_NOTIFY_VRING1 | \
   BK7258_RPTUN_NOTIFY_ALL)

#define BK7258_RPTUN_PROOF_NAME_PREFIX "bk7258-rptun-"
#define BK7258_RPTUN_PROOF_NAME_SIZE \
  (sizeof(BK7258_RPTUN_PROOF_NAME_PREFIX) + 8u)

static_assert(BK7258_RPTUN_PROOF_NAME_SIZE <= RPMSG_NAME_SIZE,
              "RPTUN proof endpoint name exceeds the RPMsg ABI");

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_rptun_dev_s
{
  struct rptun_dev_s rptun;
  struct rpmsg_endpoint proof_ept;
  rptun_callback_t callback;
  void *callback_arg;
  char proof_name[RPMSG_NAME_SIZE];
  uint32_t generation;
  uint32_t proof_generation;
  bool bootstrap;
  bool proof_registered;
  bool initialized;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static const char *bk7258_rptun_get_cpuname(struct rptun_dev_s *dev);
static struct resource_table *
bk7258_rptun_get_resource(struct rptun_dev_s *dev);
static bool bk7258_rptun_is_autostart(struct rptun_dev_s *dev);
static bool bk7258_rptun_is_master(struct rptun_dev_s *dev);
static int bk7258_rptun_config(struct rptun_dev_s *dev, void *data);
static int bk7258_rptun_start(struct rptun_dev_s *dev);
static int bk7258_rptun_stop(struct rptun_dev_s *dev);
static int bk7258_rptun_notify(struct rptun_dev_s *dev, uint32_t vqid);
static int bk7258_rptun_register_callback(struct rptun_dev_s *dev,
                                          rptun_callback_t callback,
                                          void *arg);
static void bk7258_rptun_proof_device_created(
  struct rpmsg_device *rdev, void *arg);
static void bk7258_rptun_proof_device_destroyed(
  struct rpmsg_device *rdev, void *arg);
#ifndef CONFIG_BK7258_AP_CORE
static bool bk7258_rptun_proof_ns_match(struct rpmsg_device *rdev,
                                        void *arg, const char *name,
                                        uint32_t dest);
static void bk7258_rptun_proof_ns_bind(struct rpmsg_device *rdev,
                                       void *arg, const char *name,
                                       uint32_t dest);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct rptun_ops_s g_bk7258_rptun_ops =
{
  .get_cpuname       = bk7258_rptun_get_cpuname,
  .get_resource      = bk7258_rptun_get_resource,
  .is_autostart      = bk7258_rptun_is_autostart,
  .is_master         = bk7258_rptun_is_master,
  .config            = bk7258_rptun_config,
  .start             = bk7258_rptun_start,
  .stop              = bk7258_rptun_stop,
  .notify            = bk7258_rptun_notify,
  .register_callback = bk7258_rptun_register_callback,
};

static struct bk7258_rptun_dev_s g_bk7258_rptun;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static volatile uint32_t *bk7258_rptun_outgoing_pending(
  volatile struct bk7258_rptun_control_s *control)
{
#ifdef CONFIG_BK7258_AP_CORE
  return &control->ap_to_cp_pending;
#else
  return &control->cp_to_ap_pending;
#endif
}

static volatile uint32_t *bk7258_rptun_incoming_pending(
  volatile struct bk7258_rptun_control_s *control)
{
#ifdef CONFIG_BK7258_AP_CORE
  return &control->cp_to_ap_pending;
#else
  return &control->ap_to_cp_pending;
#endif
}

static volatile uint32_t *bk7258_rptun_local_rx_sequence(
  volatile struct bk7258_rptun_control_s *control)
{
#ifdef CONFIG_BK7258_AP_CORE
  return &control->ap_rx_sequence;
#else
  return &control->cp_rx_sequence;
#endif
}

static uint32_t bk7258_rptun_vqid_mask(uint32_t vqid)
{
  if (vqid == RPTUN_NOTIFY_ALL || vqid >= 31u)
    {
      return BK7258_RPTUN_NOTIFY_ALL;
    }

  return 1u << vqid;
}

static bool bk7258_rptun_proof_generation_valid(
  struct bk7258_rptun_dev_s *priv)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t generation;

  if (!__atomic_load_n(&priv->initialized, __ATOMIC_ACQUIRE))
    {
      return false;
    }

  generation = __atomic_load_n(&priv->generation, __ATOMIC_ACQUIRE);
  __asm volatile ("dmb sy" ::: "memory");
  return generation != 0 && control->generation == generation;
}

/* This endpoint is transport infrastructure, not a test or supervisor
 * service.  The AP announces a generation-qualified name, the CP binds it
 * with a known destination (which makes OpenAMP send NS_CREATE_ACK), and the
 * AP's ns_bound callback observes that ACK.  Each core therefore stops its
 * local bootstrap scan only after its own RPMsg device completed probing.
 * Keep the endpoint until device_destroy so NuttX owns the normal remove and
 * restart ordering.
 */

static bool bk7258_rptun_proof_device_matches(struct rpmsg_device *rdev)
{
  const char *cpuname = rpmsg_get_cpuname(rdev);

  return cpuname != NULL &&
         strcmp(cpuname, BK7258_RPTUN_REMOTE_NAME) == 0;
}

static void bk7258_rptun_proof_fail(struct bk7258_rptun_dev_s *priv,
                                    int error)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t expected = BK7258_RPTUN_STATE_CONNECTING;

  if (error >= 0 || !bk7258_rptun_proof_generation_valid(priv))
    {
      return;
    }

  if (__atomic_compare_exchange_n(
        (uint32_t *)(uintptr_t)&control->state, &expected,
        BK7258_RPTUN_STATE_FAULTED, false,
        __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
    {
      control->error = (uint32_t)-error;
      __asm volatile ("dmb sy" ::: "memory");
    }
}

static int bk7258_rptun_proof_ept_cb(struct rpmsg_endpoint *ept,
                                     void *data, size_t len,
                                     uint32_t src, void *arg)
{
  (void)ept;
  (void)data;
  (void)len;
  (void)src;
  (void)arg;

  /* The proof is completed by Name Service CREATE/ACK and carries no data.
   * Ignore an unexpected payload without violating the RPMsg callback
   * contract or perturbing the transport lifecycle.
   */

  return OK;
}

#ifdef CONFIG_BK7258_AP_CORE
static void bk7258_rptun_proof_ns_bound(struct rpmsg_endpoint *ept)
{
  struct bk7258_rptun_dev_s *priv = ept->priv;
  uint32_t generation;

  if (priv == NULL || ept != &priv->proof_ept ||
      !bk7258_rptun_proof_generation_valid(priv) ||
      !is_rpmsg_ept_ready(ept) ||
      strcmp(ept->name, priv->proof_name) != 0)
    {
      return;
    }

  generation = __atomic_load_n(&priv->generation, __ATOMIC_ACQUIRE);
  if (__atomic_load_n(&priv->proof_generation, __ATOMIC_ACQUIRE) !=
      generation)
    {
      return;
    }

  bk7258_rptun_mark_connected();
}
#endif

static void bk7258_rptun_proof_device_created(
  struct rpmsg_device *rdev, void *arg)
{
  struct bk7258_rptun_dev_s *priv = arg;
#ifdef CONFIG_BK7258_AP_CORE
  int ret;
#endif

  if (!bk7258_rptun_proof_device_matches(rdev))
    {
      return;
    }

  if (!rdev->support_ns || !rdev->support_ack)
    {
      bk7258_rptun_proof_fail(priv, -EPROTONOSUPPORT);
      return;
    }

#ifdef CONFIG_BK7258_AP_CORE
  if (!bk7258_rptun_proof_generation_valid(priv))
    {
      return;
    }

  if (priv->proof_ept.rdev != NULL)
    {
      bk7258_rptun_proof_fail(priv, -EBUSY);
      return;
    }

  priv->proof_ept.priv = priv;
  priv->proof_ept.ns_bound_cb = bk7258_rptun_proof_ns_bound;
  __atomic_store_n(&priv->proof_generation, priv->generation,
                   __ATOMIC_RELEASE);
  ret = rpmsg_create_ept(&priv->proof_ept, rdev, priv->proof_name,
                         RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                         bk7258_rptun_proof_ept_cb, NULL);
  if (ret < 0)
    {
      __atomic_store_n(&priv->proof_generation, 0, __ATOMIC_RELEASE);
      memset(&priv->proof_ept, 0, sizeof(priv->proof_ept));
      bk7258_rptun_proof_fail(priv, ret);
    }
#endif
}

#ifndef CONFIG_BK7258_AP_CORE
static bool bk7258_rptun_proof_ns_match(struct rpmsg_device *rdev,
                                        void *arg, const char *name,
                                        uint32_t dest)
{
  struct bk7258_rptun_dev_s *priv = arg;

  (void)dest;
  return bk7258_rptun_proof_device_matches(rdev) &&
         rdev->support_ns && rdev->support_ack &&
         bk7258_rptun_proof_generation_valid(priv) &&
         strcmp(name, priv->proof_name) == 0;
}

static void bk7258_rptun_proof_ns_bind(struct rpmsg_device *rdev,
                                       void *arg, const char *name,
                                       uint32_t dest)
{
  struct bk7258_rptun_dev_s *priv = arg;
  int ret;

  if (!bk7258_rptun_proof_ns_match(rdev, arg, name, dest))
    {
      return;
    }

  if (priv->proof_ept.rdev != NULL)
    {
      bk7258_rptun_proof_fail(priv, -EBUSY);
      return;
    }

  priv->proof_ept.priv = priv;
  __atomic_store_n(&priv->proof_generation, priv->generation,
                   __ATOMIC_RELEASE);
  ret = rpmsg_create_ept(&priv->proof_ept, rdev, name,
                         RPMSG_ADDR_ANY, dest,
                         bk7258_rptun_proof_ept_cb, NULL);
  if (ret < 0)
    {
      __atomic_store_n(&priv->proof_generation, 0, __ATOMIC_RELEASE);
      memset(&priv->proof_ept, 0, sizeof(priv->proof_ept));
      bk7258_rptun_proof_fail(priv, ret);
      return;
    }

  if (bk7258_rptun_proof_generation_valid(priv) &&
      __atomic_load_n(&priv->proof_generation, __ATOMIC_ACQUIRE) ==
      priv->generation)
    {
      bk7258_rptun_mark_connected();
    }
}
#endif

static void bk7258_rptun_proof_device_destroyed(
  struct rpmsg_device *rdev, void *arg)
{
  struct bk7258_rptun_dev_s *priv = arg;

  if (!bk7258_rptun_proof_device_matches(rdev) ||
      priv->proof_ept.rdev != rdev)
    {
      return;
    }

  __atomic_store_n(&priv->proof_generation, 0, __ATOMIC_RELEASE);
  rpmsg_destroy_ept(&priv->proof_ept);
  memset(&priv->proof_ept, 0, sizeof(priv->proof_ept));
}

static int bk7258_rptun_proof_register(struct bk7258_rptun_dev_s *priv)
{
  int ret;

  ret = rpmsg_register_callback(priv,
                                bk7258_rptun_proof_device_created,
                                bk7258_rptun_proof_device_destroyed,
#ifdef CONFIG_BK7258_AP_CORE
                                NULL, NULL);
#else
                                bk7258_rptun_proof_ns_match,
                                bk7258_rptun_proof_ns_bind);
#endif
  if (ret >= 0)
    {
      priv->proof_registered = true;
    }

  return ret;
}

static void bk7258_rptun_proof_unregister(
  struct bk7258_rptun_dev_s *priv)
{
  if (!priv->proof_registered)
    {
      return;
    }

  rpmsg_unregister_callback(priv,
                            bk7258_rptun_proof_device_created,
                            bk7258_rptun_proof_device_destroyed,
#ifdef CONFIG_BK7258_AP_CORE
                            NULL, NULL);
#else
                            bk7258_rptun_proof_ns_match,
                            bk7258_rptun_proof_ns_bind);
#endif
  priv->proof_registered = false;
}

static void bk7258_rptun_receive(uint32_t generation, uint32_t notify)
{
  struct bk7258_rptun_dev_s *priv = &g_bk7258_rptun;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  volatile uint32_t *incoming = bk7258_rptun_incoming_pending(control);
  volatile uint32_t *outgoing = bk7258_rptun_outgoing_pending(control);
  rptun_callback_t callback;
  uint32_t pending;
  bool bootstrap;

  if (!priv->initialized || generation != priv->generation)
    {
      return;
    }

  /* Registration follows rptun_initialize() asynchronously.  Keep the
   * level-triggered shared pending bits intact until OpenAMP has installed
   * its callback; consuming them first loses the initial virtio handshake
   * permanently when the mailbox worker wins that startup race.
   */

  callback = priv->callback;
  if (callback == NULL)
    {
      return;
    }

  pending = __atomic_exchange_n((uint32_t *)(uintptr_t)incoming, 0,
                                __ATOMIC_ACQ_REL);
  bootstrap = __atomic_load_n(&priv->bootstrap, __ATOMIC_ACQUIRE);
  __asm volatile ("dmb sy" ::: "memory");
  pending |= notify & BK7258_RPTUN_NOTIFY_VALID;
  if ((pending & BK7258_RPTUN_NOTIFY_VALID) != 0)
    {
      __atomic_fetch_add(
        (uint32_t *)(uintptr_t)bk7258_rptun_local_rx_sequence(control),
        1u, __ATOMIC_RELEASE);
    }

  /* RPTUN installs the lower-half callback after remoteproc_start(), but
   * creates the virtio devices immediately afterwards in another step.
   * remoteproc_get_notification() returns success when its vdev list is
   * still empty, so an early edge cannot be acknowledged reliably.  Keep a
   * local bootstrap level until this core's Name Service callback proves its
   * RPMsg device exists.  Do not gate this rescan on the shared lifecycle
   * state: it is peer-owned during part of startup and may be cache-stale.
   */

  if ((pending & BK7258_RPTUN_NOTIFY_ALL) != 0 ||
      bootstrap)
    {
      callback(priv->callback_arg, RPTUN_NOTIFY_ALL);
    }
  else
    {
      if ((pending & BK7258_RPTUN_NOTIFY_VRING0) != 0)
        {
          callback(priv->callback_arg, 0);
        }

      if ((pending & BK7258_RPTUN_NOTIFY_VRING1) != 0)
        {
          callback(priv->callback_arg, 1);
        }
    }

  /* A peer pulse is also an opportunity to repair an edge that was queued
   * while the SDK's one-deep logical channel was busy.
   */

  pending = __atomic_load_n((uint32_t *)(uintptr_t)outgoing,
                            __ATOMIC_ACQUIRE);
  if (pending != 0)
    {
      (void)bk7258_rptun_mbox_notify(generation, pending);
    }
}

#ifndef CONFIG_BK7258_AP_CORE
static void bk7258_rptun_prepare_resource(uint32_t generation)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  struct rptun_rsc_s *rsc =
    (struct rptun_rsc_s *)(uintptr_t)BK7258_RPTUN_RESOURCE_ADDR;

  memset(rsc, 0, sizeof(*rsc));

  rsc->rsc_tbl_hdr.ver = 1;
  rsc->rsc_tbl_hdr.num = 2;
  rsc->offset[0] = offsetof(struct rptun_rsc_s, rpmsg_vdev);
  rsc->offset[1] = offsetof(struct rptun_rsc_s, carveout);

  rsc->rpmsg_vdev.type = RSC_VDEV;
  rsc->rpmsg_vdev.id = VIRTIO_ID_RPMSG;
  rsc->rpmsg_vdev.dfeatures = BK7258_RPTUN_FEATURES;
  rsc->rpmsg_vdev.config_len = sizeof(struct fw_rsc_config);
  rsc->rpmsg_vdev.num_of_vrings = BK7258_RPTUN_VRING_COUNT;

  /* RPTUN XORs is_master() with whether this field is DRIVER.  Marking the
   * table's native role as DRIVER therefore makes the CP master the virtio
   * driver and the AP remote the virtio device.
   */

  rsc->rpmsg_vdev.reserved[0] = VIRTIO_DEV_DRIVER;

  rsc->rpmsg_vring0.da = FW_RSC_U32_ADDR_ANY;
  rsc->rpmsg_vring0.align = BK7258_RPTUN_VRING_ALIGN;
  rsc->rpmsg_vring0.num = BK7258_RPTUN_VRING_NUM;
  rsc->rpmsg_vring0.notifyid = 0;
  rsc->rpmsg_vring1.da = FW_RSC_U32_ADDR_ANY;
  rsc->rpmsg_vring1.align = BK7258_RPTUN_VRING_ALIGN;
  rsc->rpmsg_vring1.num = BK7258_RPTUN_VRING_NUM;
  rsc->rpmsg_vring1.notifyid = 1;
  rsc->config.r2h_buf_size = BK7258_RPTUN_BUFFER_SIZE;
  rsc->config.h2r_buf_size = BK7258_RPTUN_BUFFER_SIZE;
  memcpy(rsc->config.host_cpuname, "cp", sizeof("cp"));
  memcpy(rsc->config.remote_cpuname, "ap", sizeof("ap"));

  rsc->carveout.type = RSC_CARVEOUT;
  rsc->carveout.da = BK7258_RPTUN_CARVEOUT_ADDR;
  rsc->carveout.pa = FW_RSC_U32_ADDR_ANY;
  rsc->carveout.len = BK7258_RPTUN_CARVEOUT_SIZE;
  memcpy(rsc->carveout.name, "rpmsg_shm", sizeof("rpmsg_shm"));

  control->generation = generation;
  control->resource_crc32 = 0;
  control->cp_to_ap_pending = 0;
  control->ap_to_cp_pending = 0;
  control->cp_rx_sequence = 0;
  control->ap_rx_sequence = 0;
  __asm volatile ("dmb sy" ::: "memory");
  control->state = BK7258_RPTUN_STATE_TABLE_READY;
  __asm volatile ("dmb sy; sev" ::: "memory");
}
#endif

static const char *bk7258_rptun_get_cpuname(struct rptun_dev_s *dev)
{
  (void)dev;
  return BK7258_RPTUN_REMOTE_NAME;
}

static struct resource_table *
bk7258_rptun_get_resource(struct rptun_dev_s *dev)
{
  struct bk7258_rptun_dev_s *priv =
    container_of(dev, struct bk7258_rptun_dev_s, rptun);

#ifdef CONFIG_BK7258_AP_CORE
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  int timeout = CONFIG_RPTUN_STATUS_TIMEOUT_MS;

  while (timeout-- > 0)
    {
      __asm volatile ("dmb sy" ::: "memory");
      if (control->magic == BK7258_RPTUN_CONTROL_MAGIC &&
          control->version == BK7258_RPTUN_CONTROL_VERSION &&
          control->size == sizeof(*control) &&
          control->generation == priv->generation &&
          control->state >= BK7258_RPTUN_STATE_TABLE_READY &&
          control->state < BK7258_RPTUN_STATE_QUIESCING)
        {
          break;
        }

      nxsig_usleep(1000);
    }

  if (timeout < 0)
    {
      return NULL;
    }
#else
  (void)priv;
#endif

  return (struct resource_table *)(uintptr_t)BK7258_RPTUN_RESOURCE_ADDR;
}

static bool bk7258_rptun_is_autostart(struct rptun_dev_s *dev)
{
  (void)dev;
  return true;
}

static bool bk7258_rptun_is_master(struct rptun_dev_s *dev)
{
  (void)dev;
#ifdef CONFIG_BK7258_AP_CORE
  return false;
#else
  return true;
#endif
}

static int bk7258_rptun_config(struct rptun_dev_s *dev, void *data)
{
  (void)dev;
  (void)data;
  return OK;
}

static int bk7258_rptun_start(struct rptun_dev_s *dev)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  (void)dev;
  __asm volatile ("dmb sy" ::: "memory");
  if (control->state == BK7258_RPTUN_STATE_TABLE_READY)
    {
      control->state = BK7258_RPTUN_STATE_CONNECTING;
      __asm volatile ("dmb sy" ::: "memory");
    }

  return OK;
}

static int bk7258_rptun_stop(struct rptun_dev_s *dev)
{
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();

  (void)dev;
  control->state = BK7258_RPTUN_STATE_QUIESCING;
  __asm volatile ("dmb sy" ::: "memory");
  return OK;
}

static int bk7258_rptun_notify(struct rptun_dev_s *dev, uint32_t vqid)
{
  struct bk7258_rptun_dev_s *priv =
    container_of(dev, struct bk7258_rptun_dev_s, rptun);
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  volatile uint32_t *outgoing = bk7258_rptun_outgoing_pending(control);
  uint32_t mask = bk7258_rptun_vqid_mask(vqid);

  __atomic_fetch_or((uint32_t *)(uintptr_t)outgoing, mask,
                    __ATOMIC_RELEASE);
  __asm volatile ("dmb sy" ::: "memory");
  return bk7258_rptun_mbox_notify(priv->generation, mask);
}

static int bk7258_rptun_register_callback(struct rptun_dev_s *dev,
                                          rptun_callback_t callback,
                                          void *arg)
{
  struct bk7258_rptun_dev_s *priv =
    container_of(dev, struct bk7258_rptun_dev_s, rptun);

  priv->callback_arg = arg;
  __atomic_store_n(&priv->bootstrap, callback != NULL, __ATOMIC_RELEASE);
  __asm volatile ("dmb sy" ::: "memory");
  priv->callback = callback;
  __asm volatile ("dmb sy" ::: "memory");
  bk7258_rptun_mbox_set_notify(callback != NULL ?
                              bk7258_rptun_receive : NULL);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bk7258_rptun_mark_connected(void)
{
  struct bk7258_rptun_dev_s *priv = &g_bk7258_rptun;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t expected = BK7258_RPTUN_STATE_CONNECTING;
  bool connected;

  /* Name Service binding is the first bidirectional proof that the remote
   * RPMsg stack consumed the shared table.  Do not overwrite a concurrent
   * lifecycle transition such as QUIESCING or FAULTED.
   */

  /* This callback is local proof that rptun_create_devices() completed.
   * Stop the periodic all-vring bootstrap scan on this core regardless of
   * which core won the shared CONNECTING -> CONNECTED transition.
   */

  if (!bk7258_rptun_proof_generation_valid(priv))
    {
      return;
    }

  __atomic_store_n(&priv->bootstrap, false, __ATOMIC_RELEASE);
  connected = __atomic_compare_exchange_n(
                (uint32_t *)(uintptr_t)&control->state, &expected,
                BK7258_RPTUN_STATE_CONNECTED, false,
                __ATOMIC_RELEASE, __ATOMIC_RELAXED);
  if (connected || expected == BK7258_RPTUN_STATE_CONNECTED)
    {
      /* Preserve proof that the vdev completed its asynchronous probe even
       * if the supervisor subsequently changes CONNECTED to FAULTED.  A
       * FAULTED instance that never reached this point is not safe to remove
       * with the pinned NuttX RPTUN implementation.
       */

      __atomic_fetch_or((uint32_t *)(uintptr_t)&control->flags,
                        BK7258_RPTUN_FLAG_CONNECTED_ONCE,
                        __ATOMIC_RELEASE);
    }
}

int bk7258_rptun_initialize(uint32_t generation)
{
  struct bk7258_rptun_dev_s *priv = &g_bk7258_rptun;
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  int ret;

  if (generation == 0)
    {
      return -EINVAL;
    }

  if (priv->initialized)
    {
      if (priv->generation == generation)
        {
          return OK;
        }

#ifdef CONFIG_BK7258_AP_CORE
      return -ESTALE;
#else
      /* The CP keeps its registered RPTUN lower half across an AP reset.
       * Rebuild the shared table for the new generation, then restart the
       * existing NuttX RPTUN instance.  The lifecycle owner quiesces it
       * before clearing shared SRAM, so no old worker can observe this table.
       */

      __atomic_store_n(&priv->generation, generation, __ATOMIC_RELEASE);
      __atomic_store_n(&priv->proof_generation, 0, __ATOMIC_RELEASE);
      snprintf(priv->proof_name, sizeof(priv->proof_name), "%s%08" PRIx32,
               BK7258_RPTUN_PROOF_NAME_PREFIX, generation);
      bk7258_rptun_prepare_resource(generation);
      ret = rptun_boot(BK7258_RPTUN_REMOTE_NAME);
      if (ret < 0)
        {
          control->error = (uint32_t)-ret;
          control->state = BK7258_RPTUN_STATE_FAULTED;
          __asm volatile ("dmb sy" ::: "memory");
        }

      return ret;
#endif
    }

#ifndef CONFIG_BK7258_AP_CORE
  bk7258_rptun_prepare_resource(generation);
#else
  __asm volatile ("dmb sy" ::: "memory");
  if (control->magic != BK7258_RPTUN_CONTROL_MAGIC ||
      control->version != BK7258_RPTUN_CONTROL_VERSION ||
      control->size != sizeof(*control) ||
      control->generation != generation)
    {
      return -EPROTO;
    }
#endif

  memset(priv, 0, sizeof(*priv));
  priv->rptun.ops = &g_bk7258_rptun_ops;
  __atomic_store_n(&priv->generation, generation, __ATOMIC_RELEASE);
  snprintf(priv->proof_name, sizeof(priv->proof_name), "%s%08" PRIx32,
           BK7258_RPTUN_PROOF_NAME_PREFIX, generation);
  __atomic_store_n(&priv->initialized, true, __ATOMIC_RELEASE);

  ret = bk7258_rptun_proof_register(priv);
  if (ret < 0)
    {
      __atomic_store_n(&priv->initialized, false, __ATOMIC_RELEASE);
      control->error = (uint32_t)-ret;
      control->state = BK7258_RPTUN_STATE_FAULTED;
      __asm volatile ("dmb sy" ::: "memory");
      return ret;
    }

#ifdef CONFIG_BK7258_AP_SUPERVISOR
  /* Register the board health service before creating the RPTUN device.
   * rpmsg_register_callback() also handles already-created devices, but this
   * order keeps an allocation failure from leaving a partially initialized
   * RPTUN instance that cannot be unregistered by this NuttX version.
   */

  ret = bk7258_rpmsg_health_initialize();
  if (ret < 0)
    {
      bk7258_rptun_proof_unregister(priv);
      __atomic_store_n(&priv->initialized, false, __ATOMIC_RELEASE);
      control->error = (uint32_t)-ret;
      control->state = BK7258_RPTUN_STATE_FAULTED;
      __asm volatile ("dmb sy" ::: "memory");
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_OTA_RPMSG
  ret = bk7258_ota_rpmsg_initialize();
  if (ret < 0)
    {
      bk7258_rptun_proof_unregister(priv);
      __atomic_store_n(&priv->initialized, false, __ATOMIC_RELEASE);
      control->error = (uint32_t)-ret;
      control->state = BK7258_RPTUN_STATE_FAULTED;
      __asm volatile ("dmb sy" ::: "memory");
      return ret;
    }
#endif

  ret = rptun_initialize(&priv->rptun);
  if (ret < 0)
    {
      bk7258_rptun_proof_unregister(priv);
      __atomic_store_n(&priv->initialized, false, __ATOMIC_RELEASE);
      control->error = (uint32_t)-ret;
      control->state = BK7258_RPTUN_STATE_FAULTED;
      __asm volatile ("dmb sy" ::: "memory");
      return ret;
    }

#ifdef CONFIG_BK7258_AP_CORE
  __atomic_fetch_or((uint32_t *)(uintptr_t)&control->flags,
                    BK7258_RPTUN_FLAG_AP_CORE_READY, __ATOMIC_RELEASE);
#endif

#ifdef CONFIG_BK7258_RPMSG_TEST
#ifdef CONFIG_BK7258_AP_CORE
  __atomic_fetch_or((uint32_t *)(uintptr_t)&control->flags,
                    BK7258_RPTUN_FLAG_AP_TEST_ENTER, __ATOMIC_RELEASE);
#endif
  ret = bk7258_rpmsg_test_initialize();
  if (ret < 0)
    {
      control->error = (uint32_t)-ret;
      control->state = BK7258_RPTUN_STATE_FAULTED;
      __asm volatile ("dmb sy" ::: "memory");
      return ret;
    }

#ifdef CONFIG_BK7258_AP_CORE
  __atomic_fetch_or((uint32_t *)(uintptr_t)&control->flags,
                    BK7258_RPTUN_FLAG_AP_TEST_READY, __ATOMIC_RELEASE);
#endif
#endif

#ifdef CONFIG_BK7258_RPMSGFS_TEST
  ret = bk7258_rpmsgfs_test_initialize();
  if (ret < 0)
    {
      control->error = (uint32_t)-ret;
      control->state = BK7258_RPTUN_STATE_FAULTED;
      __asm volatile ("dmb sy" ::: "memory");
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_BT_IPC_TEST
  ret = bk7258_bt_test_initialize();
  if (ret < 0)
    {
      control->error = (uint32_t)-ret;
      control->state = BK7258_RPTUN_STATE_FAULTED;
      __asm volatile ("dmb sy" ::: "memory");
      return ret;
    }
#endif

  return OK;
}

int bk7258_rptun_quiesce(void)
{
  struct bk7258_rptun_dev_s *priv = &g_bk7258_rptun;

  if (!priv->initialized)
    {
      return OK;
    }

#ifdef CONFIG_BK7258_AP_CORE
  return -EPERM;
#else
  volatile struct bk7258_rptun_control_s *control =
    bk7258_rptun_control();
  uint32_t flags;
  uint32_t state;

  state = __atomic_load_n((uint32_t *)(uintptr_t)&control->state,
                          __ATOMIC_ACQUIRE);
  flags = __atomic_load_n((uint32_t *)(uintptr_t)&control->flags,
                          __ATOMIC_ACQUIRE);

  /* rptun_initialize()/rptun_boot() complete in a worker.  Killing that
   * worker after remoteproc linked a vdev but before rpmsg_virtio_probe()
   * installed its private data makes rpmsg_virtio_remove() dereference a
   * partial vdev.  Refuse teardown until a Name Service bind proves that the
   * asynchronous probe completed.  This guard also protects callers other
   * than the AP lifecycle wrapper.
   */

  if (state == BK7258_RPTUN_STATE_PREPARING ||
      state == BK7258_RPTUN_STATE_TABLE_READY ||
      state == BK7258_RPTUN_STATE_CONNECTING ||
      (state == BK7258_RPTUN_STATE_FAULTED &&
       (flags & BK7258_RPTUN_FLAG_CONNECTED_ONCE) == 0))
    {
      return -EBUSY;
    }

  return rptun_poweroff(BK7258_RPTUN_REMOTE_NAME);
#endif
}

#endif /* CONFIG_BK7258_RPTUN */
