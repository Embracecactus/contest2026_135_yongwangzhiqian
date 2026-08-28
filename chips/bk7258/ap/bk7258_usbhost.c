/****************************************************************************
 * board/bk7258/chip/ap/bk7258_usbhost.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP USB host adapter.  The immutable v3.1.1.9 bundle contains a
 * CherryUSB MUSB HCD, but its hub/class stack is not used here.  NuttX owns
 * root-port waiting, enumeration, and class binding; this file adapts only
 * the exported HCD pipe and URB calls.
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/clock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>
#include <nuttx/usb/usb.h>
#include <nuttx/usb/usbhost.h>
#include <nuttx/usb/usbhost_devaddr.h>

#include <arch/chip/bk7258_usbhost.h>

/*
 * usbh_core.h is a public SDK header, but it uses generic CONFIG_USBHOST_*
 * names to lay out vendor hub structures.  Those names are also used by
 * NuttX and can have different values.  The constants below are the values
 * in the v3.1.1.9 AP bundle; NuttX's header was parsed above, and the names
 * are removed again after the vendor types have been parsed.
 */

#ifdef CONFIG_USBHOST_MAX_RHPORTS
#  undef CONFIG_USBHOST_MAX_RHPORTS
#endif
#define CONFIG_USBHOST_MAX_RHPORTS          1
#ifdef CONFIG_USBHOST_MAX_EHPORTS
#  undef CONFIG_USBHOST_MAX_EHPORTS
#endif
#define CONFIG_USBHOST_MAX_EHPORTS          4
#ifdef CONFIG_USBHOST_MAX_INTERFACES
#  undef CONFIG_USBHOST_MAX_INTERFACES
#endif
#define CONFIG_USBHOST_MAX_INTERFACES       10
#ifdef CONFIG_USBHOST_MAX_INTF_ALTSETTINGS
#  undef CONFIG_USBHOST_MAX_INTF_ALTSETTINGS
#endif
#define CONFIG_USBHOST_MAX_INTF_ALTSETTINGS 8
#ifdef CONFIG_USBHOST_MAX_ENDPOINTS
#  undef CONFIG_USBHOST_MAX_ENDPOINTS
#endif
#define CONFIG_USBHOST_MAX_ENDPOINTS        14
#ifdef CONFIG_USBHOST_DEV_NAMELEN
#  undef CONFIG_USBHOST_DEV_NAMELEN
#endif
#define CONFIG_USBHOST_DEV_NAMELEN          16

#ifdef CLASS_CONNECT
#  undef CLASS_CONNECT
#endif
#ifdef CLASS_DISCONNECT
#  undef CLASS_DISCONNECT
#endif
#include <components/usb.h>
#include <components/cherryusb/usbh_core.h>
#include <driver/int.h>

#undef CONFIG_USBHOST_MAX_RHPORTS
#undef CONFIG_USBHOST_MAX_EHPORTS
#undef CONFIG_USBHOST_MAX_INTERFACES
#undef CONFIG_USBHOST_MAX_INTF_ALTSETTINGS
#undef CONFIG_USBHOST_MAX_ENDPOINTS
#undef CONFIG_USBHOST_DEV_NAMELEN

#define BK7258_USBH_EVENT_DEPTH 4
#define BK7258_USBH_DESC_BUFSIZE 1024
#define BK7258_USBH_XFER_TIMEOUT 500
#define BK7258_USBH_PIPE_LIMIT 10
#define BK7258_USBH_MAGIC 0x42555348u

/* The v3.1.1.9 MUSB port routes its interrupt to physical CPU2 because that
 * is the SDK application's default owner.  This project's NuttX AP primary
 * is physical CPU1.  The SDK deliberately makes its low-level HCD hooks
 * weak, so keep the HCD itself immutable and override only IRQ ownership.
 */

#define BK7258_USB_AP_PRIMARY_CORE_ID  1u
#define BK7258_USB_SDK_DEFAULT_CORE_ID 2u
#define BK7258_USB_INTERRUPT_CTRL_BIT  (1u << 19)

_Static_assert(INT_SRC_USB == 19,
               "BK7258 USB IRQ routing bit must match v3.1.1.9");

extern int32_t sys_drv_core_intr_group1_disable(uint32_t core_id,
                                                uint32_t param);
extern int32_t sys_drv_core_intr_group1_enable(uint32_t core_id,
                                               uint32_t param);
extern void USBH_IRQHandler(void);

struct bk7258_usbhost_s;

struct bk7258_usbep_s
{
  uint32_t magic;
  FAR struct bk7258_usbhost_s *priv;
  usbh_pipe_t pipe;
  bool ep0;
  bool closing;
  bool sem_initialized;
  bool async_active;
  bool async_running;
  bool async_canceling;
  bool sync_active;
  usbhost_asynch_t async_callback;
  FAR void *async_arg;
  FAR struct usbh_urb *sync_urb;
  sem_t async_done;
  struct usbh_urb async_urb;
};

struct bk7258_usb_event_s
{
  uint8_t port;
  FAR void *callback;
};

struct bk7258_usbhost_s
{
  struct usbhost_driver_s drvr;
  struct usbhost_connection_s conn;
  struct usbhost_roothubport_s rhport;
  struct usbhost_devaddr_s devgen;

  /* These are the public CherryUSB types used by the immutable HCD ABI. */

  struct usbh_hub vendor_roothub;
  struct usbh_hubport vendor_hport;

  struct bk7258_usbep_s ep0;
  spinlock_t lock;
  sem_t event_sem;
  struct work_s event_work;
  struct bk7258_usb_event_s events[BK7258_USBH_EVENT_DEPTH];
  uint8_t event_head;
  uint8_t event_count;
  uint32_t event_sequence;
  uint32_t wait_sequence;
  uint8_t event_work_queued;
  uint8_t endpoint_count;
  uint8_t active_sync;
  bool initialized;
  bool event_sem_initialized;
  bool accepting_events;
  bool shutting_down;
  bool driver_initialized;
  bool usb_open;
  bool hcd_initialized;
};

static struct bk7258_usbhost_s g_bk7258_usbhost;
static mutex_t g_bk7258_usbhost_init_lock = NXMUTEX_INITIALIZER;
static volatile int g_bk7258_usb_hcd_init_status = -ENODEV;
static volatile int g_bk7258_usb_hcd_deinit_status = -ENODEV;
static volatile int g_bk7258_usb_irq_route_status = -ENODEV;

static inline FAR struct bk7258_usbhost_s *bk7258_priv_from_drvr(
  FAR struct usbhost_driver_s *drvr)
{
  return (FAR struct bk7258_usbhost_s *)((uintptr_t)drvr -
                                         offsetof(struct bk7258_usbhost_s,
                                                  drvr));
}

static inline FAR struct bk7258_usbhost_s *bk7258_priv_from_conn(
  FAR struct usbhost_connection_s *conn)
{
  return (FAR struct bk7258_usbhost_s *)((uintptr_t)conn -
                                         offsetof(struct bk7258_usbhost_s,
                                                  conn));
}

static int bk7258_sdk_error(int ret)
{
  if (ret == 0)
    {
      return 0;
    }

  /* The v3.1.1.9 MUSB implementation returns negated errno values.  Keep
   * those values intact; a positive, unknown vendor result is not success.
   */

  return ret < 0 ? ret : -EIO;
}

static int bk7258_bk_error(int ret)
{
  if (ret == BK_OK)
    {
      return 0;
    }
  if (ret == BK_ERR_PARAM || ret == BK_ERR_NULL_PARAM ||
      ret == BK_ERR_USB_OPERATION_NULL_POINTER)
    {
      return -EINVAL;
    }
  if (ret == BK_ERR_NO_MEM)
    {
      return -ENOMEM;
    }
  if (ret == BK_ERR_TIMEOUT)
    {
      return -ETIMEDOUT;
    }
  if (ret == BK_ERR_BUSY || ret == BK_ERR_IN_PROGRESS)
    {
      return -EBUSY;
    }
  if (ret == BK_ERR_NOT_SUPPORT)
    {
      return -ENOTSUP;
    }
  if (ret == BK_ERR_NO_DEV || ret == BK_ERR_USB_NOT_CONNECT)
    {
      return -ENODEV;
    }
  if (ret == BK_ERR_SHUT_DOWN)
    {
      return -ESHUTDOWN;
    }

  /* BK_ERR_NOT_INIT/NOT_OPEN/NOT_CLOSE and module-specific negative values
   * are SDK state failures, not Linux errno values. */

  return -EIO;
}

/*
 * Override the SDK HCD's weak board hooks.  bk_int_isr_register() binds the
 * vendor ISR to NuttX's external IRQ dispatch; the sys-driver call selects
 * which physical CPU receives that interrupt source.
 */

void usb_hc_low_level_init(void)
{
  bk_err_t error;
  int32_t ret;

  g_bk7258_usb_irq_route_status = -EAGAIN;
  error = bk_int_isr_register(INT_SRC_USB, USBH_IRQHandler, NULL);
  if (error != BK_OK)
    {
      g_bk7258_usb_irq_route_status = -EIO;
      return;
    }

  (void)sys_drv_core_intr_group1_disable(BK7258_USB_SDK_DEFAULT_CORE_ID,
                                         BK7258_USB_INTERRUPT_CTRL_BIT);
  ret = sys_drv_core_intr_group1_enable(BK7258_USB_AP_PRIMARY_CORE_ID,
                                        BK7258_USB_INTERRUPT_CTRL_BIT);
  if (ret != 0)
    {
      (void)bk_int_isr_unregister(INT_SRC_USB);
      g_bk7258_usb_irq_route_status = -EIO;
      return;
    }

  g_bk7258_usb_irq_route_status = 0;
}

void usb_hc_low_level_deinit(void)
{
  (void)sys_drv_core_intr_group1_disable(BK7258_USB_AP_PRIMARY_CORE_ID,
                                         BK7258_USB_INTERRUPT_CTRL_BIT);
  (void)bk_int_isr_unregister(INT_SRC_USB);
  g_bk7258_usb_irq_route_status = -ENODEV;
}

/*
 * bk_usb_open()/bk_usb_close() call usbh_initialize()/usbh_deinitialize()
 * internally.  The immutable CherryUSB implementations would start their
 * private hub/class task, so the board link must use --wrap for these two
 * symbols.  The wrappers intentionally expose only the HCD lifecycle needed
 * by this NuttX adapter; they do not call the vendor upper stack.
 */

int __wrap_usbh_initialize(void)
{
  int ret = bk7258_sdk_error(usb_hc_init());

  if (ret == 0 && g_bk7258_usb_irq_route_status != 0)
    {
      int route_ret = g_bk7258_usb_irq_route_status;

      (void)usb_hc_deinit();
      ret = route_ret;
    }

  g_bk7258_usb_hcd_init_status = ret;
  return ret;
}

int __wrap_usbh_deinitialize(void)
{
  int ret = bk7258_sdk_error(usb_hc_deinit());
  g_bk7258_usb_hcd_deinit_status = ret;
  return ret;
}

static inline bool bk7258_valid_speed(uint8_t speed)
{
  return speed == USB_SPEED_LOW || speed == USB_SPEED_FULL ||
         speed == USB_SPEED_HIGH;
}

static inline uint8_t bk7258_vendor_speed(uint8_t speed)
{
  return speed;
}

static inline FAR struct bk7258_usbep_s *bk7258_ep_from_handle(
  FAR struct bk7258_usbhost_s *priv, usbhost_ep_t handle)
{
  FAR struct bk7258_usbep_s *ep = (FAR struct bk7258_usbep_s *)handle;

  if (ep == NULL || ep->magic != BK7258_USBH_MAGIC || ep->priv != priv ||
      ep->pipe == NULL || ep->closing)
    {
      return NULL;
    }

  return ep;
}

static void bk7258_event_worker(FAR void *arg);

/*
 * The vendor ISR calls usbh_roothub_thread_send_queue().  libbk_usb.a also
 * contains CherryUSB's upper-layer implementation of that symbol, so the
 * board link wraps it instead of defining a second strong copy.  Only a
 * bounded pointer ring and work_queue() are used in the ISR path; the vendor
 * status callback runs in the worker context.
 */

void __wrap_usbh_roothub_thread_send_queue(uint8_t port, FAR void *callback)
{
  FAR struct bk7258_usbhost_s *priv = &g_bk7258_usbhost;
  irqstate_t flags;
  bool queue = false;

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->accepting_events && !priv->shutting_down)
    {
      uint8_t index;

      if (priv->event_count < BK7258_USBH_EVENT_DEPTH)
        {
          index = (uint8_t)((priv->event_head + priv->event_count) %
                            BK7258_USBH_EVENT_DEPTH);
          priv->event_count++;
        }
      else
        {
          /* Keep the newest edge.  The worker always re-reads the root-port
           * status, so coalescing an overloaded edge queue is safe. */

          index = (uint8_t)((priv->event_head +
                             BK7258_USBH_EVENT_DEPTH - 1) %
                            BK7258_USBH_EVENT_DEPTH);
        }

      priv->events[index].port = port;
      priv->events[index].callback = callback;

      if (!priv->event_work_queued)
        {
          priv->event_work_queued = 1;
          queue = true;
        }
    }
  spin_unlock_irqrestore(&priv->lock, flags);

  if (queue)
    {
      int ret = work_queue(HPWORK, &priv->event_work,
                           bk7258_event_worker, priv, 0);
      if (ret < 0)
        {
          flags = spin_lock_irqsave(&priv->lock);
          priv->event_work_queued = 0;
          spin_unlock_irqrestore(&priv->lock, flags);
        }
    }
}

static int bk7258_read_root_status(FAR struct bk7258_usbhost_s *priv,
                                   FAR bool *connected,
                                   FAR uint8_t *speed)
{
  struct usb_setup_packet setup;
  uint8_t status_buf[4];
  uint32_t status;
  int ret;

  (void)priv;

  memset(&setup, 0, sizeof(setup));
  setup.bmRequestType = USB_REQUEST_DIR_IN | USB_REQUEST_CLASS |
                        USB_REQUEST_RECIPIENT_OTHER;
  setup.bRequest = USB_REQUEST_GET_STATUS;
  setup.wIndex = 1; /* Vendor root-hub port numbering is one-based. */
  setup.wLength = sizeof(status_buf);
  memset(status_buf, 0, sizeof(status_buf));

  ret = bk7258_sdk_error(usbh_roothub_control(&setup, status_buf));
  if (ret < 0)
    {
      return ret;
    }

  status = (uint32_t)status_buf[0] | ((uint32_t)status_buf[1] << 8) |
           ((uint32_t)status_buf[2] << 16) |
           ((uint32_t)status_buf[3] << 24);
  *connected = (status & (1u << HUB_PORT_FEATURE_CONNECTION)) != 0;
  if ((status & (1u << HUB_PORT_FEATURE_HIGHSPEED)) != 0)
    {
      *speed = USB_SPEED_HIGH;
    }
  else if ((status & (1u << HUB_PORT_FEATURE_LOWSPEED)) != 0)
    {
      *speed = USB_SPEED_LOW;
    }
  else
    {
      *speed = USB_SPEED_FULL;
    }

  return 0;
}

static void bk7258_update_connection(FAR struct bk7258_usbhost_s *priv,
                                     bool connected, uint8_t speed,
                                     bool signal)
{
  irqstate_t flags;
  bool changed;

  flags = spin_lock_irqsave(&priv->lock);
  changed = priv->rhport.hport.connected != connected;
  if (connected && priv->rhport.hport.speed != speed)
    {
      changed = true;
    }
  priv->vendor_hport.connected = connected;
  priv->vendor_hport.speed = speed;
  priv->rhport.hport.connected = connected;
  priv->rhport.hport.speed = speed;
  if (!connected)
    {
      priv->rhport.hport.devclass = NULL;
      priv->rhport.hport.funcaddr = 0;
      priv->vendor_hport.dev_addr = 0;
    }

  /* A vendor connect callback and the startup status sample can describe
   * the same physical edge.  Only signal an actual logical state edge so
   * NuttX cannot start enumeration twice for one connection. */

  if (signal && changed)
    {
      priv->event_sequence++;
    }
  spin_unlock_irqrestore(&priv->lock, flags);

  if (signal && changed)
    {
      nxsem_post(&priv->event_sem);
    }
}

static void bk7258_event_worker(FAR void *arg)
{
  FAR struct bk7258_usbhost_s *priv = arg;

  for (;;)
    {
      struct bk7258_usb_event_s event;
      irqstate_t flags;
      bool connected;
      uint8_t speed;
      int ret;

      flags = spin_lock_irqsave(&priv->lock);
      if (priv->event_count == 0 || !priv->accepting_events)
        {
          priv->event_work_queued = 0;
          spin_unlock_irqrestore(&priv->lock, flags);
          return;
        }

      event = priv->events[priv->event_head];
      priv->event_head = (uint8_t)((priv->event_head + 1) %
                                   BK7258_USBH_EVENT_DEPTH);
      priv->event_count--;
      spin_unlock_irqrestore(&priv->lock, flags);

      /* The immutable MUSB callback updates its private HCD state.  It is
       * intentionally invoked only on this normal work-queue thread. */

      if (event.callback != NULL)
        {
          ((void (*)(void))event.callback)();
        }

      ret = bk7258_read_root_status(priv, &connected, &speed);
      if (ret < 0)
        {
          /* A callback is still an edge notification.  Preserve the current
           * logical state and wake the waiter; the next edge can retry. */

          nxsem_post(&priv->event_sem);
        }
      else
        {
          bk7258_update_connection(priv, connected,
                                   bk7258_vendor_speed(speed), true);
        }
    }
}

static int bk7258_wait(FAR struct usbhost_connection_s *conn,
                       FAR struct usbhost_hubport_s **hport)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_conn(conn);
  irqstate_t flags;
  int ret;

  if (hport == NULL)
    {
      return -EINVAL;
    }

  for (;;)
    {
      flags = spin_lock_irqsave(&priv->lock);
      if (!priv->initialized || priv->shutting_down)
        {
          spin_unlock_irqrestore(&priv->lock, flags);
          return -ESHUTDOWN;
        }

      if (priv->wait_sequence != priv->event_sequence)
        {
          priv->wait_sequence = priv->event_sequence;
          *hport = &priv->rhport.hport;
          spin_unlock_irqrestore(&priv->lock, flags);
          return 0;
        }
      spin_unlock_irqrestore(&priv->lock, flags);

      ret = nxsem_wait_uninterruptible(&priv->event_sem);
      if (ret < 0)
        {
          return ret;
        }
    }
}

static int bk7258_ep0configure(FAR struct usbhost_driver_s *drvr,
                               usbhost_ep_t ep0, uint8_t funcaddr,
                               uint8_t speed, uint16_t maxpacketsize)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);
  FAR struct bk7258_usbep_s *ep;
  irqstate_t flags;
  int ret;

  ep = bk7258_ep_from_handle(priv, ep0);
  if (ep == NULL || !ep->ep0 || funcaddr > 127 ||
      !bk7258_valid_speed(speed) || maxpacketsize == 0 ||
      maxpacketsize > 64)
    {
      return -EINVAL;
    }

  ret = bk7258_sdk_error(usbh_ep0_pipe_reconfigure(ep->pipe, funcaddr,
                                                   (uint8_t)maxpacketsize,
                                                   bk7258_vendor_speed(speed)));
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  priv->vendor_hport.dev_addr = funcaddr;
  priv->vendor_hport.speed = speed;
  priv->vendor_hport.connected = true;
  priv->rhport.hport.funcaddr = funcaddr;
  priv->rhport.hport.speed = speed;
  priv->rhport.hport.connected = true;
  spin_unlock_irqrestore(&priv->lock, flags);
  return 0;
}

static int bk7258_epalloc(FAR struct usbhost_driver_s *drvr,
                          FAR const struct usbhost_epdesc_s *epdesc,
                          FAR usbhost_ep_t *handle)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);
  FAR struct bk7258_usbep_s *ep;
  struct usbh_endpoint_cfg cfg;
  irqstate_t flags;
  uint8_t type;
  int ret;

  if (epdesc == NULL || handle == NULL ||
      epdesc->hport != &priv->rhport.hport || epdesc->addr > 15 ||
      epdesc->mxpacketsize == 0 || epdesc->mxpacketsize > 1024 ||
      !bk7258_valid_speed(epdesc->hport->speed))
    {
      return -EINVAL;
    }

  switch (epdesc->xfrtype)
    {
      case USB_EP_ATTR_XFER_ISOC:
        type = USB_ENDPOINT_TYPE_ISOCHRONOUS;
        break;
      case USB_EP_ATTR_XFER_BULK:
        type = USB_ENDPOINT_TYPE_BULK;
        break;
      case USB_EP_ATTR_XFER_INT:
        type = USB_ENDPOINT_TYPE_INTERRUPT;
        break;
      default:
        return -ENOTSUP;
    }

  ep = kmm_zalloc(sizeof(*ep));
  if (ep == NULL)
    {
      return -ENOMEM;
    }

  ep->magic = BK7258_USBH_MAGIC;
  ep->priv = priv;
  ep->pipe = NULL;
  ep->ep0 = false;
  ep->closing = false;
  ret = nxsem_init(&ep->async_done, 0, 0);
  if (ret < 0)
    {
      kmm_free(ep);
      return ret;
    }
  ep->sem_initialized = true;

  memset(&cfg, 0, sizeof(cfg));
  cfg.hport = &priv->vendor_hport;
  cfg.ep_addr = (uint8_t)(epdesc->addr | (epdesc->in ? 0x80 : 0));
  cfg.ep_type = type;
  cfg.ep_mps = epdesc->mxpacketsize;
  cfg.ep_interval = epdesc->interval;
  cfg.mult = 0;

  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->initialized || priv->shutting_down ||
      !priv->vendor_hport.connected ||
      priv->endpoint_count >= BK7258_USBH_PIPE_LIMIT)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      nxsem_destroy(&ep->async_done);
      kmm_free(ep);
      return -ENODEV;
    }
  priv->endpoint_count++;
  spin_unlock_irqrestore(&priv->lock, flags);

  ret = bk7258_sdk_error(usbh_pipe_alloc(&ep->pipe, &cfg));
  if (ret < 0 || ep->pipe == NULL)
    {
      if (ret == 0)
        {
          ret = -ENOMEM;
        }
      if (ep->pipe != NULL)
        {
          (void)usbh_pipe_free(ep->pipe);
        }
      nxsem_destroy(&ep->async_done);
      kmm_free(ep);
      flags = spin_lock_irqsave(&priv->lock);
      priv->endpoint_count--;
      spin_unlock_irqrestore(&priv->lock, flags);
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  if (priv->shutting_down)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      usbh_pipe_free(ep->pipe);
      nxsem_destroy(&ep->async_done);
      kmm_free(ep);
      flags = spin_lock_irqsave(&priv->lock);
      priv->endpoint_count--;
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ESHUTDOWN;
    }
  spin_unlock_irqrestore(&priv->lock, flags);
  *handle = ep;
  return 0;
}

static void bk7258_async_complete(FAR void *arg, int nbytes)
{
  FAR struct bk7258_usbep_s *ep = arg;
  usbhost_asynch_t callback;
  FAR void *callback_arg;
  irqstate_t flags;

  flags = spin_lock_irqsave(&ep->priv->lock);
  if (!ep->async_active || ep->closing || ep->async_canceling)
    {
      spin_unlock_irqrestore(&ep->priv->lock, flags);
      return;
    }

  ep->async_active = false;
  ep->async_running = true;
  callback = ep->async_callback;
  callback_arg = ep->async_arg;
  ep->async_callback = NULL;
  spin_unlock_irqrestore(&ep->priv->lock, flags);

  if (callback != NULL)
    {
      callback(callback_arg, nbytes < 0 ? nbytes : (ssize_t)nbytes);
    }

  flags = spin_lock_irqsave(&ep->priv->lock);
  ep->async_running = false;
  spin_unlock_irqrestore(&ep->priv->lock, flags);
  nxsem_post(&ep->async_done);
}

static int bk7258_cancel_ep(FAR struct bk7258_usbep_s *ep, bool closing)
{
  FAR struct bk7258_usbhost_s *priv = ep->priv;
  FAR struct usbh_urb *urb = NULL;
  usbhost_asynch_t callback = NULL;
  FAR void *callback_arg = NULL;
  FAR struct usbh_urb *sync_urb = NULL;
  bool running;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);
  if (closing)
    {
      ep->closing = true;
    }
  running = ep->async_running || ep->sync_active;
  sync_urb = ep->sync_urb;
  if (ep->async_active)
    {
      ep->async_active = false;
      ep->async_canceling = true;
      urb = &ep->async_urb;
      callback = ep->async_callback;
      callback_arg = ep->async_arg;
      ep->async_callback = NULL;
    }
  spin_unlock_irqrestore(&priv->lock, flags);

  if (urb != NULL)
    {
      /* SDK kill does not invoke asynchronous completion callbacks. */

      (void)bk7258_sdk_error(usbh_kill_urb(urb));
    }

  if (sync_urb != NULL)
    {
      (void)bk7258_sdk_error(usbh_kill_urb(sync_urb));
    }

  if (callback != NULL)
    {
      callback(callback_arg, -ESHUTDOWN);
    }

  if (running)
    {
      (void)nxsem_wait_uninterruptible(&ep->async_done);
    }

  flags = spin_lock_irqsave(&priv->lock);
  ep->async_canceling = false;
  spin_unlock_irqrestore(&priv->lock, flags);
  return 0;
}

static int bk7258_epfree(FAR struct usbhost_driver_s *drvr, usbhost_ep_t handle)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);
  FAR struct bk7258_usbep_s *ep = bk7258_ep_from_handle(priv, handle);
  irqstate_t flags;
  int ret;

  if (ep == NULL || ep->ep0)
    {
      return -EINVAL;
    }

  ret = bk7258_cancel_ep(ep, true);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_sdk_error(usbh_pipe_free(ep->pipe));
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&priv->lock);
      ep->closing = false;
      spin_unlock_irqrestore(&priv->lock, flags);
      return ret;
    }

  flags = spin_lock_irqsave(&priv->lock);
  ep->magic = 0;
  if (priv->endpoint_count > 0)
    {
      priv->endpoint_count--;
    }
  spin_unlock_irqrestore(&priv->lock, flags);
  nxsem_destroy(&ep->async_done);
  kmm_free(ep);
  return 0;
}

static int bk7258_alloc(FAR struct usbhost_driver_s *drvr,
                        FAR uint8_t **buffer, FAR size_t *maxlen)
{
  FAR uint8_t *ptr;

  (void)drvr;
  if (buffer == NULL || maxlen == NULL)
    {
      return -EINVAL;
    }

  ptr = kmm_malloc(BK7258_USBH_DESC_BUFSIZE);
  if (ptr == NULL)
    {
      return -ENOMEM;
    }
  *buffer = ptr;
  *maxlen = BK7258_USBH_DESC_BUFSIZE;
  return 0;
}

static int bk7258_free(FAR struct usbhost_driver_s *drvr, FAR uint8_t *buffer)
{
  (void)drvr;
  if (buffer == NULL)
    {
      return -EINVAL;
    }
  kmm_free(buffer);
  return 0;
}

static int bk7258_ioalloc(FAR struct usbhost_driver_s *drvr,
                          FAR uint8_t **buffer, size_t buflen)
{
  (void)drvr;
  if (buffer == NULL || buflen == 0)
    {
      return -EINVAL;
    }
  *buffer = kmm_malloc(buflen);
  return *buffer == NULL ? -ENOMEM : 0;
}

static int bk7258_iofree(FAR struct usbhost_driver_s *drvr,
                         FAR uint8_t *buffer)
{
  return bk7258_free(drvr, buffer);
}

static int bk7258_setup_urb(FAR struct bk7258_usbhost_s *priv,
                            FAR struct bk7258_usbep_s *ep,
                            FAR const struct usb_ctrlreq_s *req,
                            FAR uint8_t *buffer, bool out)
{
  struct usb_setup_packet setup;
  struct usbh_urb urb;
  irqstate_t flags;
  int ret;

  if (req == NULL || ep == NULL || !ep->ep0 ||
      (out && req->type & USB_REQ_DIR_MASK) != 0 ||
      (!out && (req->type & USB_REQ_DIR_MASK) == 0))
    {
      return -EINVAL;
    }

  memset(&setup, 0, sizeof(setup));
  setup.bmRequestType = req->type;
  setup.bRequest = req->req;
  setup.wValue = (uint16_t)req->value[0] | ((uint16_t)req->value[1] << 8);
  setup.wIndex = (uint16_t)req->index[0] | ((uint16_t)req->index[1] << 8);
  setup.wLength = (uint16_t)req->len[0] | ((uint16_t)req->len[1] << 8);
  memset(&urb, 0, sizeof(urb));
  urb.pipe = ep->pipe;
  urb.setup = &setup;
  urb.transfer_buffer = buffer;
  urb.transfer_buffer_length = setup.wLength;
  urb.timeout = BK7258_USBH_XFER_TIMEOUT;

  (void)nxsem_reset(&ep->async_done, 0);
  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->initialized || priv->shutting_down ||
      !priv->vendor_hport.connected)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ENODEV;
    }
  if (ep->sync_active || ep->async_active || ep->async_running ||
      ep->async_canceling)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EBUSY;
    }
  priv->active_sync++;
  ep->sync_active = true;
  ep->sync_urb = &urb;
  spin_unlock_irqrestore(&priv->lock, flags);

  ret = bk7258_sdk_error(usbh_submit_urb(&urb));

  flags = spin_lock_irqsave(&priv->lock);
  ep->sync_active = false;
  ep->sync_urb = NULL;
  priv->active_sync--;
  spin_unlock_irqrestore(&priv->lock, flags);
  nxsem_post(&ep->async_done);
  return ret;
}

static int bk7258_ctrlin(FAR struct usbhost_driver_s *drvr,
                         usbhost_ep_t handle,
                         FAR const struct usb_ctrlreq_s *req,
                         FAR uint8_t *buffer)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);
  return bk7258_setup_urb(priv, bk7258_ep_from_handle(priv, handle), req,
                          buffer, false);
}

static int bk7258_ctrlout(FAR struct usbhost_driver_s *drvr,
                          usbhost_ep_t handle,
                          FAR const struct usb_ctrlreq_s *req,
                          FAR const uint8_t *buffer)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);
  return bk7258_setup_urb(priv, bk7258_ep_from_handle(priv, handle), req,
                          (FAR uint8_t *)buffer, true);
}

static ssize_t bk7258_transfer(FAR struct usbhost_driver_s *drvr,
                               usbhost_ep_t handle, FAR uint8_t *buffer,
                               size_t buflen)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);
  FAR struct bk7258_usbep_s *ep = bk7258_ep_from_handle(priv, handle);
  struct usbh_urb urb;
  irqstate_t flags;
  int ret;

  if (ep == NULL || ep->ep0 || buffer == NULL || buflen == 0)
    {
      return -EINVAL;
    }

  memset(&urb, 0, sizeof(urb));
  urb.pipe = ep->pipe;
  urb.transfer_buffer = buffer;
  urb.transfer_buffer_length = buflen;
  urb.timeout = BK7258_USBH_XFER_TIMEOUT;

  (void)nxsem_reset(&ep->async_done, 0);
  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->initialized || priv->shutting_down ||
      !priv->vendor_hport.connected)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -ENODEV;
    }
  if (ep->sync_active || ep->async_active || ep->async_running ||
      ep->async_canceling)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EBUSY;
    }
  priv->active_sync++;
  ep->sync_active = true;
  ep->sync_urb = &urb;
  spin_unlock_irqrestore(&priv->lock, flags);

  ret = bk7258_sdk_error(usbh_submit_urb(&urb));
  flags = spin_lock_irqsave(&priv->lock);
  ep->sync_active = false;
  ep->sync_urb = NULL;
  priv->active_sync--;
  spin_unlock_irqrestore(&priv->lock, flags);
  nxsem_post(&ep->async_done);
  return ret < 0 ? ret : (ssize_t)urb.actual_length;
}

#ifdef CONFIG_USBHOST_ASYNCH
static int bk7258_asynch(FAR struct usbhost_driver_s *drvr,
                         usbhost_ep_t handle, FAR uint8_t *buffer,
                         size_t buflen, usbhost_asynch_t callback,
                         FAR void *arg)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);
  FAR struct bk7258_usbep_s *ep = bk7258_ep_from_handle(priv, handle);
  irqstate_t flags;
  int ret;

  if (ep == NULL || ep->ep0 || buffer == NULL || buflen == 0 ||
      callback == NULL)
    {
      return -EINVAL;
    }

  (void)nxsem_reset(&ep->async_done, 0);
  flags = spin_lock_irqsave(&priv->lock);
  if (!priv->vendor_hport.connected || priv->shutting_down ||
      ep->sync_active || ep->async_active || ep->async_running ||
      ep->async_canceling)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EBUSY;
    }
  memset(&ep->async_urb, 0, sizeof(ep->async_urb));
  ep->async_urb.pipe = ep->pipe;
  ep->async_urb.transfer_buffer = buffer;
  ep->async_urb.transfer_buffer_length = buflen;
  ep->async_urb.complete = bk7258_async_complete;
  ep->async_urb.arg = ep;
  ep->async_callback = callback;
  ep->async_arg = arg;
  ep->async_active = true;
  spin_unlock_irqrestore(&priv->lock, flags);

  ret = bk7258_sdk_error(usbh_submit_urb(&ep->async_urb));
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&priv->lock);
      ep->async_active = false;
      ep->async_callback = NULL;
      ep->async_arg = NULL;
      spin_unlock_irqrestore(&priv->lock, flags);
    }
  return ret;
}
#endif

static int bk7258_cancel(FAR struct usbhost_driver_s *drvr,
                         usbhost_ep_t handle)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);
  FAR struct bk7258_usbep_s *ep = bk7258_ep_from_handle(priv, handle);

  if (ep == NULL)
    {
      return -EINVAL;
    }

  return bk7258_cancel_ep(ep, false);
}

#ifdef CONFIG_USBHOST_HUB
static int bk7258_connect(FAR struct usbhost_driver_s *drvr,
                          FAR struct usbhost_hubport_s *hport,
                          bool connected)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);

  /* The immutable AP HCD exposes one root port only.  NuttX hub class
   * enumeration cannot be backed by the vendor hub's private structures. */

  if (hport != &priv->rhport.hport)
    {
      return -ENOTSUP;
    }
  bk7258_update_connection(priv, connected, priv->rhport.hport.speed, true);
  return 0;
}
#endif

static void bk7258_disconnect(FAR struct usbhost_driver_s *drvr,
                              FAR struct usbhost_hubport_s *hport)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_drvr(drvr);

  if (hport == &priv->rhport.hport)
    {
      bk7258_update_connection(priv, false, USB_SPEED_FULL, true);
    }
}

static int bk7258_enumerate(FAR struct usbhost_connection_s *conn,
                            FAR struct usbhost_hubport_s *hport)
{
  FAR struct bk7258_usbhost_s *priv = bk7258_priv_from_conn(conn);
  struct usb_setup_packet setup;
  bool connected;
  uint8_t speed;
  int ret;

  if (hport != &priv->rhport.hport)
    {
      return -EINVAL;
    }

  ret = bk7258_read_root_status(priv, &connected, &speed);
  if (ret < 0 || !connected)
    {
      return ret < 0 ? ret : -ENODEV;
    }

  /* The SDK root-hub SET_FEATURE RESET path performs the MUSB reset and its
   * documented 20 ms settle delays.  Keep the standard pre/post reset delay
   * here, in the caller's enumeration thread, not in the ISR worker. */

  nxsig_usleep(50 * 1000);
  memset(&setup, 0, sizeof(setup));
  setup.bmRequestType = USB_REQUEST_DIR_OUT | USB_REQUEST_CLASS |
                        USB_REQUEST_RECIPIENT_OTHER;
  setup.bRequest = USB_REQUEST_SET_FEATURE;
  setup.wValue = HUB_PORT_FEATURE_RESET;
  setup.wIndex = 1;
  ret = bk7258_sdk_error(usbh_roothub_control(&setup, NULL));
  if (ret < 0)
    {
      return ret;
    }
  nxsig_usleep(50 * 1000);

  ret = bk7258_read_root_status(priv, &connected, &speed);
  if (ret < 0 || !connected)
    {
      return ret < 0 ? ret : -ENODEV;
    }
  bk7258_update_connection(priv, true, speed, false);

  return usbhost_enumerate(hport, &hport->devclass);
}

static void bk7258_initialize_driver(FAR struct bk7258_usbhost_s *priv)
{
  memset(&priv->drvr, 0, sizeof(priv->drvr));
  priv->drvr.ep0configure = bk7258_ep0configure;
  priv->drvr.epalloc = bk7258_epalloc;
  priv->drvr.epfree = bk7258_epfree;
  priv->drvr.alloc = bk7258_alloc;
  priv->drvr.free = bk7258_free;
  priv->drvr.ioalloc = bk7258_ioalloc;
  priv->drvr.iofree = bk7258_iofree;
  priv->drvr.ctrlin = bk7258_ctrlin;
  priv->drvr.ctrlout = bk7258_ctrlout;
  priv->drvr.transfer = bk7258_transfer;
#ifdef CONFIG_USBHOST_ASYNCH
  priv->drvr.asynch = bk7258_asynch;
#endif
  priv->drvr.cancel = bk7258_cancel;
#ifdef CONFIG_USBHOST_HUB
  priv->drvr.connect = bk7258_connect;
#endif
  priv->drvr.disconnect = bk7258_disconnect;

  priv->conn.wait = bk7258_wait;
  priv->conn.enumerate = bk7258_enumerate;
}

static bool bk7258_usbhost_has_ownership(
  FAR const struct bk7258_usbhost_s *priv)
{
  return priv->event_sem_initialized || priv->ep0.sem_initialized ||
         priv->ep0.pipe != NULL || priv->driver_initialized ||
         priv->usb_open || priv->hcd_initialized;
}

/* Tear down only the stages that are still owned.  A failure leaves those
 * ownership bits intact so another uninitialize call can resume at the
 * failed stage.  This function is called with the init mutex held.
 */

static int bk7258_usbhost_teardown_locked(
  FAR struct bk7258_usbhost_s *priv)
{
  bool was_initialized;
  irqstate_t flags;
  int first_error = OK;
  int ret;

  flags = spin_lock_irqsave(&priv->lock);
  was_initialized = priv->initialized;
  if (was_initialized &&
      (priv->endpoint_count != 1 || priv->active_sync != 0))
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EBUSY;
    }

  priv->accepting_events = false;
  priv->shutting_down = true;
  priv->initialized = false;
  spin_unlock_irqrestore(&priv->lock, flags);

  if (priv->event_sem_initialized)
    {
      (void)nxsem_post(&priv->event_sem);
    }

  (void)work_cancel_sync(HPWORK, &priv->event_work);

  if (priv->ep0.pipe != NULL)
    {
      ret = bk7258_sdk_error(usbh_pipe_free(priv->ep0.pipe));
      if (ret < 0)
        {
          /* No irreversible stage has run yet for a live host, so it can
           * safely resume accepting events.  A partially initialized host
           * stays quiesced and retains ownership for a later retry.
           */

          if (was_initialized)
            {
              flags = spin_lock_irqsave(&priv->lock);
              priv->initialized = true;
              priv->shutting_down = false;
              priv->accepting_events = true;
              spin_unlock_irqrestore(&priv->lock, flags);
            }

          return ret;
        }

      priv->ep0.pipe = NULL;
      priv->vendor_hport.ep0 = NULL;
      priv->rhport.hport.ep0 = NULL;
      priv->endpoint_count = 0;
    }

  if (priv->ep0.sem_initialized)
    {
      ret = nxsem_destroy(&priv->ep0.async_done);
      if (ret < 0)
        {
          return ret;
        }

      priv->ep0.sem_initialized = false;
    }

  if (priv->usb_open)
    {
      g_bk7258_usb_hcd_deinit_status = -EAGAIN;
      ret = bk7258_bk_error(bk_usb_close());

      /* The SDK clears s_usb_open_close_flag even when its nested HCD
       * deinit reports an error.  Record the two ownership stages
       * separately so HCD cleanup can be retried directly below.
       */

      priv->usb_open = false;
      if (g_bk7258_usb_hcd_deinit_status == OK)
        {
          priv->hcd_initialized = false;
        }

      if (ret < 0)
        {
          first_error = ret;
        }
    }

  if (priv->hcd_initialized)
    {
      ret = __wrap_usbh_deinitialize();
      if (ret < 0)
        {
          return first_error < 0 ? first_error : ret;
        }

      priv->hcd_initialized = false;
    }

  if (priv->driver_initialized)
    {
      ret = bk7258_bk_error(bk_usb_driver_deinit());
      if (ret < 0)
        {
          return first_error < 0 ? first_error : ret;
        }

      priv->driver_initialized = false;
    }

  if (priv->event_sem_initialized)
    {
      ret = nxsem_destroy(&priv->event_sem);
      if (ret < 0)
        {
          return first_error < 0 ? first_error : ret;
        }

      priv->event_sem_initialized = false;
    }

  memset(priv, 0, sizeof(*priv));
  return first_error;
}

FAR struct usbhost_connection_s *bk7258_usbhost_initialize(void)
{
  FAR struct bk7258_usbhost_s *priv = &g_bk7258_usbhost;
  struct usbh_endpoint_cfg cfg;
  irqstate_t flags;
  int ret;
  int open_ret;

  ret = nxmutex_lock(&g_bk7258_usbhost_init_lock);
  if (ret < 0)
    {
      return NULL;
    }
  if (priv->initialized)
    {
      nxmutex_unlock(&g_bk7258_usbhost_init_lock);
      return &priv->conn;
    }

  if (bk7258_usbhost_has_ownership(priv) || priv->shutting_down)
    {
      /* A previous teardown stopped at a failed vendor stage.  Require the
       * caller to retry uninitialize instead of overwriting live ownership.
       */

      nxmutex_unlock(&g_bk7258_usbhost_init_lock);
      return NULL;
    }

  memset(priv, 0, sizeof(*priv));
  bk7258_initialize_driver(priv);

  priv->rhport.hport.drvr = &priv->drvr;
#ifdef CONFIG_USBHOST_HUB
  priv->rhport.hport.parent = NULL;
#endif
  priv->rhport.hport.devclass = NULL;
  priv->rhport.hport.connected = false;
  priv->rhport.hport.port = 0;
  priv->rhport.hport.funcaddr = 0;
  priv->rhport.hport.speed = USB_SPEED_FULL;
  ret = usbhost_devaddr_initialize(&priv->devgen);
  if (ret < 0)
    {
      nxmutex_unlock(&g_bk7258_usbhost_init_lock);
      return NULL;
    }
  priv->rhport.pdevgen = &priv->devgen;

  memset(&priv->vendor_roothub, 0, sizeof(priv->vendor_roothub));
  priv->vendor_roothub.is_roothub = true;
  memset(&priv->vendor_hport, 0, sizeof(priv->vendor_hport));
  priv->vendor_hport.connected = false;
  priv->vendor_hport.port = 1;
  priv->vendor_hport.dev_addr = 0;
  priv->vendor_hport.speed = USB_SPEED_FULL;
  priv->vendor_hport.parent = &priv->vendor_roothub;

  ret = nxsem_init(&priv->event_sem, 0, 0);
  if (ret < 0)
    {
      nxmutex_unlock(&g_bk7258_usbhost_init_lock);
      return NULL;
    }

  priv->event_sem_initialized = true;

  priv->accepting_events = true;
  g_bk7258_usb_hcd_init_status = -EAGAIN;
  g_bk7258_usb_hcd_deinit_status = -EAGAIN;
  /* bk_usb_open() performs the AP PM vote, USB clock/analog-PHY setup and
   * host custom-register programming.  Its internal usbh_initialize() call
   * reaches __wrap_usbh_initialize() at link time, so no CherryUSB hub task
   * is started here. */
  ret = bk7258_bk_error(bk_usb_driver_init());
  if (ret < 0)
    {
      (void)bk7258_usbhost_teardown_locked(priv);
      nxmutex_unlock(&g_bk7258_usbhost_init_lock);
      return NULL;
    }
  priv->driver_initialized = true;

  open_ret = bk7258_bk_error(bk_usb_open(USB_HOST_MODE));
  if (open_ret == OK)
    {
      priv->usb_open = true;
    }

  if (g_bk7258_usb_hcd_init_status == OK)
    {
      priv->hcd_initialized = true;
    }

  ret = open_ret;
  if (open_ret < 0 || g_bk7258_usb_hcd_init_status != 0)
    {
      if (ret == 0)
        {
          ret = g_bk7258_usb_hcd_init_status;
        }
      (void)bk7258_usbhost_teardown_locked(priv);
      nxmutex_unlock(&g_bk7258_usbhost_init_lock);
      return NULL;
    }

  memset(&priv->ep0, 0, sizeof(priv->ep0));
  priv->ep0.magic = BK7258_USBH_MAGIC;
  priv->ep0.priv = priv;
  priv->ep0.ep0 = true;
  memset(&cfg, 0, sizeof(cfg));
  cfg.hport = &priv->vendor_hport;
  cfg.ep_addr = USB_CONTROL_OUT_EP0;
  cfg.ep_type = USB_ENDPOINT_TYPE_CONTROL;
  cfg.ep_mps = 8;
  cfg.ep_interval = 0;
  cfg.mult = 0;
  ret = nxsem_init(&priv->ep0.async_done, 0, 0);
  if (ret < 0)
    {
      (void)bk7258_usbhost_teardown_locked(priv);
      nxmutex_unlock(&g_bk7258_usbhost_init_lock);
      return NULL;
    }
  priv->ep0.sem_initialized = true;

  ret = bk7258_sdk_error(usbh_pipe_alloc(&priv->ep0.pipe, &cfg));
  if (ret < 0 || priv->ep0.pipe == NULL)
    {
      if (ret == 0)
        {
          ret = -ENOMEM;
        }
      (void)bk7258_usbhost_teardown_locked(priv);
      nxmutex_unlock(&g_bk7258_usbhost_init_lock);
      return NULL;
    }

  priv->vendor_hport.ep0 = priv->ep0.pipe;
  priv->rhport.hport.ep0 = &priv->ep0;
  priv->endpoint_count = 1;
  flags = spin_lock_irqsave(&priv->lock);
  priv->initialized = true;
  spin_unlock_irqrestore(&priv->lock, flags);

  /* Sample the root port after the HCD and EP0 are ready.  A device may
   * have been inserted before firmware reached this point, so relying only
   * on a connect edge would leave NuttX waiting forever.  update_connection
   * suppresses a duplicate signal if the event worker wins this race. */

  {
    bool connected = false;
    uint8_t speed;

    ret = bk7258_read_root_status(priv, &connected, &speed);
    syslog(LOG_INFO,
           "BK7258 USB host: ready irq=CPU1 initial=%s status=%d\n",
           connected ? "connected" : "disconnected", ret);
    if (ret == 0 && connected)
      {
        bk7258_update_connection(priv, true, bk7258_vendor_speed(speed),
                                 true);
      }
  }

  nxmutex_unlock(&g_bk7258_usbhost_init_lock);
  return &priv->conn;
}

int bk7258_usbhost_uninitialize(void)
{
  FAR struct bk7258_usbhost_s *priv = &g_bk7258_usbhost;
  int ret;

  ret = nxmutex_lock(&g_bk7258_usbhost_init_lock);
  if (ret < 0)
    {
      return ret;
    }
  if (!priv->initialized && !bk7258_usbhost_has_ownership(priv) &&
      !priv->shutting_down)
    {
      nxmutex_unlock(&g_bk7258_usbhost_init_lock);
      return 0;
    }
  ret = bk7258_usbhost_teardown_locked(priv);
  nxmutex_unlock(&g_bk7258_usbhost_init_lock);
  return ret;
}
