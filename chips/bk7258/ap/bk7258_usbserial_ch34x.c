/****************************************************************************
 * board/bk7258/chip/ap/bk7258_usbserial_ch34x.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 board USB-host class for the CH340/CH341 asynchronous serial
 * protocol.  The class is deliberately limited to VID 0x1a86/PID 0x7523,
 * 115200 8N1, and no flow control.  It uses the standard NuttX USB host
 * registry and UART lower-half interfaces; it does not create a private
 * character-device ABI.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <debug.h>

#include <nuttx/clock.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/serial/serial.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>
#include <nuttx/usb/usb.h>
#include <nuttx/usb/usbhost.h>
#include <nuttx/wqueue.h>

#include <arch/chip/bk7258_usbserial_ch34x.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_USBHOST
#  warning "CH34x USB serial class requires CONFIG_USBHOST"
#endif

#ifndef CONFIG_USBHOST_ASYNCH
#  error "CH34x USB serial class requires CONFIG_USBHOST_ASYNCH"
#endif

#ifndef CONFIG_SCHED_WORKQUEUE
#  warning "CH34x USB serial class requires CONFIG_SCHED_WORKQUEUE"
#endif

#ifndef CONFIG_SCHED_LPWORK
#  warning "CH34x USB serial class requires CONFIG_SCHED_LPWORK"
#endif

#ifndef CONFIG_SERIAL_REMOVABLE
#  warning "CH34x USB serial class requires CONFIG_SERIAL_REMOVABLE"
#endif

#define BK7258_CH34X_VID             0x1a86
#define BK7258_CH34X_PID             0x7523
#define BK7258_CH34X_DEVNAME         "/dev/ttyUSB0"

#define BK7258_CH34X_RXBUFSIZE       256
#define BK7258_CH34X_TXBUFSIZE       256
#define BK7258_CH34X_XFERDELAY       MSEC2TICK(10)

#define BK7258_CH34X_FOUND_IF        (1 << 0)
#define BK7258_CH34X_FOUND_IN        (1 << 1)
#define BK7258_CH34X_FOUND_OUT       (1 << 2)
#define BK7258_CH34X_FOUND_ALL       (BK7258_CH34X_FOUND_IF | \
                                      BK7258_CH34X_FOUND_IN | \
                                      BK7258_CH34X_FOUND_OUT)

/* CH341 vendor requests and register encodings.  These values follow the
 * public Linux kernel CH341 protocol reference:
 * https://github.com/torvalds/linux/blob/master/drivers/usb/serial/ch341.c
 * No Linux source is copied here; the USB transactions are implemented
 * against the NuttX USB host controller contract.
 */

#define BK7258_CH34X_REQ_READ_VERSION 0x5f
#define BK7258_CH34X_REQ_WRITE_REG    0x9a
#define BK7258_CH34X_REQ_SERIAL_INIT  0xa1
#define BK7258_CH34X_REQ_MODEM_CTRL   0xa4

#define BK7258_CH34X_REG_PRESCALER    0x12
#define BK7258_CH34X_REG_DIVISOR      0x13
#define BK7258_CH34X_REG_LCR          0x18
#define BK7258_CH34X_REG_LCR2         0x25
#define BK7258_CH34X_REG_FLOW_CTL     0x27

#define BK7258_CH34X_LCR_ENABLE_RX    0x80
#define BK7258_CH34X_LCR_ENABLE_TX    0x40
#define BK7258_CH34X_LCR_CS8          0x03
#define BK7258_CH34X_FLOW_NONE        0x00
#define BK7258_CH34X_BIT_RTS          (1 << 6)
#define BK7258_CH34X_BIT_DTR          (1 << 5)

#define BK7258_CH34X_CLKRATE          48000000u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_usbserial_ch34x_s
{
  /* Must be first: USB host casts usbhost_class_s to this private type. */

  struct usbhost_class_s usbclass;
  struct uart_dev_s      uartdev;
  mutex_t                lock;

  volatile bool          disconnected;
  bool                   registered;
  bool                   endpoints;
  bool                   rxena;
  bool                   txena;
  bool                   rx_active;
  bool                   rx_pending;
  bool                   rx_working;
  ssize_t                rx_result;
  uint8_t                rx_worker;
  uint8_t                version;
  uint8_t                lcr;
  uint8_t                mcr;
  uint16_t               pktsize;
  int16_t                crefs;

  struct work_s           rxwork;
  struct work_s           rxcompletework;
  struct work_s           txwork;
  struct work_s           destroywork;
  spinlock_t              rxlock;
  FAR uint8_t            *ctrlreq;
  FAR uint8_t            *ctrlbuf;
  FAR uint8_t            *inbuf;
  FAR uint8_t            *outbuf;
  usbhost_ep_t            bulkin;
  usbhost_ep_t            bulkout;

  char                    rxbuffer[BK7258_CH34X_RXBUFSIZE];
  char                    txbuffer[BK7258_CH34X_TXBUFSIZE];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR struct usbhost_class_s *bk7258_ch34x_create(
  FAR struct usbhost_hubport_s *hport,
  FAR const struct usbhost_id_s *id);
static int bk7258_ch34x_connect(FAR struct usbhost_class_s *usbclass,
  FAR const uint8_t *configdesc, int desclen);
static int bk7258_ch34x_disconnected(FAR struct usbhost_class_s *usbclass);
static void bk7258_ch34x_destroy(FAR void *arg);

static int bk7258_ch34x_setup(FAR struct uart_dev_s *uartdev);
static void bk7258_ch34x_shutdown(FAR struct uart_dev_s *uartdev);
static int bk7258_ch34x_attach(FAR struct uart_dev_s *uartdev);
static void bk7258_ch34x_detach(FAR struct uart_dev_s *uartdev);
static int bk7258_ch34x_ioctl(FAR struct file *filep, int cmd,
  unsigned long arg);
static void bk7258_ch34x_rxint(FAR struct uart_dev_s *uartdev, bool enable);
static bool bk7258_ch34x_rxavailable(FAR struct uart_dev_s *uartdev);
static void bk7258_ch34x_txint(FAR struct uart_dev_s *uartdev, bool enable);
static bool bk7258_ch34x_txready(FAR struct uart_dev_s *uartdev);
static bool bk7258_ch34x_txempty(FAR struct uart_dev_s *uartdev);

static int bk7258_ch34x_cfgdesc(
  FAR struct bk7258_usbserial_ch34x_s *priv,
  FAR const uint8_t *configdesc, int desclen);
static int bk7258_ch34x_alloc_buffers(
  FAR struct bk7258_usbserial_ch34x_s *priv);
static void bk7258_ch34x_free_buffers(
  FAR struct bk7258_usbserial_ch34x_s *priv);
static int bk7258_ch34x_ctrlout(
  FAR struct bk7258_usbserial_ch34x_s *priv, uint8_t req,
  uint16_t value, uint16_t index);
static int bk7258_ch34x_ctrlin(
  FAR struct bk7258_usbserial_ch34x_s *priv, uint8_t req,
  uint16_t value, uint16_t index, uint16_t len);
static int bk7258_ch34x_write_reg(
  FAR struct bk7258_usbserial_ch34x_s *priv, uint8_t reg1,
  uint8_t reg2, uint16_t value);
static int bk7258_ch34x_set_baud_lcr(
  FAR struct bk7258_usbserial_ch34x_s *priv);
static int bk7258_ch34x_configure(
  FAR struct bk7258_usbserial_ch34x_s *priv);

static void bk7258_ch34x_rxwork(FAR void *arg);
static void bk7258_ch34x_rxcompletework(FAR void *arg);
static void bk7258_ch34x_rxcallback(FAR void *arg, ssize_t nread);
static void bk7258_ch34x_rxwork_common(
  FAR struct bk7258_usbserial_ch34x_s *priv, uint8_t slot);
static void bk7258_ch34x_txwork(FAR void *arg);
#ifdef CONFIG_BK7258_USBHOST_CH34X_VALIDATION
static int bk7258_ch34x_validation_thread(int argc, FAR char *argv[]);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct usbhost_id_s g_bk7258_ch34x_id[1] =
{
  {
    USB_CLASS_VENDOR_SPEC,
    0,
    0,
    BK7258_CH34X_VID,
    BK7258_CH34X_PID
  }
};

static struct usbhost_registry_s g_bk7258_ch34x_registry =
{
  NULL,
  bk7258_ch34x_create,
  1,
  g_bk7258_ch34x_id
};

static const struct uart_ops_s g_bk7258_ch34x_uart_ops =
{
  .setup       = bk7258_ch34x_setup,
  .shutdown    = bk7258_ch34x_shutdown,
  .attach      = bk7258_ch34x_attach,
  .detach      = bk7258_ch34x_detach,
  .ioctl       = bk7258_ch34x_ioctl,
  .receive     = NULL,
  .rxint       = bk7258_ch34x_rxint,
  .rxavailable = bk7258_ch34x_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  .rxflowcontrol = NULL,
#endif
  .send        = NULL,
  .txint       = bk7258_ch34x_txint,
  .txready     = bk7258_ch34x_txready,
  .txempty     = bk7258_ch34x_txempty,
  .release     = NULL,
  .recvbuf     = NULL,
  .sendbuf     = NULL
};

/* One statically owned instance is intentional.  USB host create() may run
 * from a context where heap allocation is not safe, while this board
 * contract exposes only one fixed /dev/ttyUSB0 endpoint. */

static struct bk7258_usbserial_ch34x_s g_bk7258_ch34x;
static spinlock_t g_bk7258_ch34x_alloc_lock = SP_UNLOCKED;
static bool g_bk7258_ch34x_inuse;
static mutex_t g_bk7258_ch34x_registry_lock = NXMUTEX_INITIALIZER;
static bool g_bk7258_ch34x_registered;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint16_t bk7258_ch34x_getle16(FAR const uint8_t *p)
{
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static inline void bk7258_ch34x_putle16(FAR uint8_t *p, uint16_t value)
{
  p[0] = value & 0xff;
  p[1] = value >> 8;
}

static inline int bk7258_ch34x_usbret(int ret)
{
  /* NuttX HCD APIs return zero/non-negative transfer lengths or negative
   * errno.  A positive control result cannot describe a successful zero
   * length control transaction and is treated as an I/O error. */

  return ret > 0 ? -EIO : ret;
}

static FAR struct bk7258_usbserial_ch34x_s *
bk7258_ch34x_from_uart(FAR struct uart_dev_s *uartdev)
{
  DEBUGASSERT(uartdev != NULL && uartdev->priv != NULL);
  return (FAR struct bk7258_usbserial_ch34x_s *)uartdev->priv;
}

static int bk7258_ch34x_queue_rx_slot(
  FAR struct bk7258_usbserial_ch34x_s *priv, uint8_t slot, clock_t delay)
{
  FAR struct work_s *work;
  worker_t worker;
  irqstate_t flags;
  bool enabled;
  int ret;

  if (slot == 0)
    {
      work = &priv->rxwork;
      worker = bk7258_ch34x_rxwork;
    }
  else
    {
      work = &priv->rxcompletework;
      worker = bk7258_ch34x_rxcompletework;
    }

  flags = spin_lock_irqsave(&priv->rxlock);
  enabled = priv->rxena && !priv->disconnected;
  spin_unlock_irqrestore(&priv->rxlock, flags);
  if (!enabled || !work_available(work))
    {
      return -EBUSY;
    }

  ret = work_queue(LPWORK, work, worker, priv, delay);
  return ret;
}

static void bk7258_ch34x_queue_rx(
  FAR struct bk7258_usbserial_ch34x_s *priv, clock_t delay)
{
  irqstate_t flags;
  bool idle;
  uint8_t slot;

  flags = spin_lock_irqsave(&priv->rxlock);
  idle = priv->rxena && !priv->disconnected && !priv->rx_active &&
         !priv->rx_working;
  slot = priv->rx_pending ? (priv->rx_worker == 0 ? 1 : 0) : 0;
  spin_unlock_irqrestore(&priv->rxlock, flags);

  if (idle)
    {
      (void)bk7258_ch34x_queue_rx_slot(priv, slot, delay);
    }
}

static void bk7258_ch34x_queue_tx(
  FAR struct bk7258_usbserial_ch34x_s *priv, clock_t delay)
{
  int ret;

  if (!priv->txena || priv->disconnected || !work_available(&priv->txwork))
    {
      return;
    }

  ret = work_queue(LPWORK, &priv->txwork, bk7258_ch34x_txwork, priv, delay);
  if (ret < 0 && ret != -EBUSY)
    {
      uerr("CH34x TX work_queue failed: %d\n", ret);
    }
}

static int bk7258_ch34x_ctrlout(
  FAR struct bk7258_usbserial_ch34x_s *priv, uint8_t req,
  uint16_t value, uint16_t index)
{
  FAR struct usb_ctrlreq_s *ctrlreq;
  FAR struct usbhost_hubport_s *hport = priv->usbclass.hport;

  DEBUGASSERT(priv->ctrlreq != NULL && hport != NULL);
  ctrlreq = (FAR struct usb_ctrlreq_s *)priv->ctrlreq;
  ctrlreq->type = USB_DIR_OUT | USB_REQ_TYPE_VENDOR |
                  USB_REQ_RECIPIENT_DEVICE;
  ctrlreq->req = req;
  bk7258_ch34x_putle16(ctrlreq->value, value);
  bk7258_ch34x_putle16(ctrlreq->index, index);
  bk7258_ch34x_putle16(ctrlreq->len, 0);

  return bk7258_ch34x_usbret(
    DRVR_CTRLOUT(hport->drvr, hport->ep0, ctrlreq, NULL));
}

static int bk7258_ch34x_ctrlin(
  FAR struct bk7258_usbserial_ch34x_s *priv, uint8_t req,
  uint16_t value, uint16_t index, uint16_t len)
{
  FAR struct usb_ctrlreq_s *ctrlreq;
  FAR struct usbhost_hubport_s *hport = priv->usbclass.hport;

  DEBUGASSERT(priv->ctrlreq != NULL && priv->ctrlbuf != NULL && hport != NULL);
  ctrlreq = (FAR struct usb_ctrlreq_s *)priv->ctrlreq;
  ctrlreq->type = USB_DIR_IN | USB_REQ_TYPE_VENDOR |
                  USB_REQ_RECIPIENT_DEVICE;
  ctrlreq->req = req;
  bk7258_ch34x_putle16(ctrlreq->value, value);
  bk7258_ch34x_putle16(ctrlreq->index, index);
  bk7258_ch34x_putle16(ctrlreq->len, len);

  return bk7258_ch34x_usbret(
    DRVR_CTRLIN(hport->drvr, hport->ep0, ctrlreq, priv->ctrlbuf));
}

static int bk7258_ch34x_write_reg(
  FAR struct bk7258_usbserial_ch34x_s *priv, uint8_t reg1,
  uint8_t reg2, uint16_t value)
{
  return bk7258_ch34x_ctrlout(priv, BK7258_CH34X_REQ_WRITE_REG,
                              (uint16_t)reg2 << 8 | reg1, value);
}

static int bk7258_ch34x_set_baud_lcr(
  FAR struct bk7258_usbserial_ch34x_s *priv)
{
  unsigned int divisor;
  int ret;

  /* This wrapper promises 115200 only.  The CH341 divisor formula is kept
   * explicit so the transaction cannot silently program a nearby rate. */

  divisor = BK7258_CH34X_CLKRATE / (4 * 115200);
  divisor /= 2;
  if (divisor < 2 || divisor > 255)
    {
      return -ERANGE;
    }

  divisor = (0x100 - divisor) << 8 | 3;
  if (priv->version > 0x27)
    {
      divisor |= 1 << 7;
    }

  ret = bk7258_ch34x_write_reg(priv, BK7258_CH34X_REG_PRESCALER,
                               BK7258_CH34X_REG_DIVISOR, divisor);
  if (ret < 0)
    {
      return ret;
    }

  /* CH341 versions before 0x30 use the reset-default 8N1 line format and
   * expose separate legacy registers.  Version 0x30+ accepts the combined
   * LCR/LCR2 write used here. */

  if (priv->version >= 0x30)
    {
      ret = bk7258_ch34x_write_reg(priv, BK7258_CH34X_REG_LCR,
                                   BK7258_CH34X_REG_LCR2, priv->lcr);
    }

  return ret;
}

static int bk7258_ch34x_configure(
  FAR struct bk7258_usbserial_ch34x_s *priv)
{
  int ret;

  ret = bk7258_ch34x_ctrlin(priv, BK7258_CH34X_REQ_READ_VERSION, 0, 0, 2);
  if (ret < 0)
    {
      return ret;
    }

  priv->version = priv->ctrlbuf[0];

  ret = bk7258_ch34x_ctrlout(priv, BK7258_CH34X_REQ_SERIAL_INIT, 0, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_ch34x_set_baud_lcr(priv);
  if (ret < 0)
    {
      return ret;
    }

  /* No software/hardware flow control.  The public CH341 transaction uses
   * both bytes of the flow-control register and zero as the value. */

  ret = bk7258_ch34x_write_reg(priv, BK7258_CH34X_REG_FLOW_CTL,
                               BK7258_CH34X_REG_FLOW_CTL,
                               BK7258_CH34X_FLOW_NONE << 8 |
                               BK7258_CH34X_FLOW_NONE);
  if (ret < 0)
    {
      return ret;
    }

  /* Assert DTR and RTS.  The CH341 modem-control request takes the
   * complement of the asserted bit mask, as documented by the reference
   * protocol implementation. */

  return bk7258_ch34x_ctrlout(priv, BK7258_CH34X_REQ_MODEM_CTRL,
                              (uint16_t)~priv->mcr, 0);
}

static int bk7258_ch34x_cfgdesc(
  FAR struct bk7258_usbserial_ch34x_s *priv,
  FAR const uint8_t *configdesc, int desclen)
{
  FAR struct usbhost_hubport_s *hport = priv->usbclass.hport;
  struct usbhost_epdesc_s bindesc;
  struct usbhost_epdesc_s boutdesc;
  uint8_t found = 0;
  bool in_interface = false;
  int remaining;
  int ret;

  memset(&bindesc, 0, sizeof(bindesc));
  memset(&boutdesc, 0, sizeof(boutdesc));
  if (desclen < USB_SIZEOF_CFGDESC ||
      ((FAR const struct usb_desc_s *)configdesc)->type !=
      USB_DESC_TYPE_CONFIG)
    {
      return -EINVAL;
    }

  remaining = bk7258_ch34x_getle16(configdesc + 2);
  if (remaining < USB_SIZEOF_CFGDESC || remaining > desclen)
    {
      return -EINVAL;
    }

  {
    uint8_t cfglen = configdesc[0];
    if (cfglen < USB_SIZEOF_CFGDESC || cfglen > remaining)
      {
        return -EINVAL;
      }

    configdesc += cfglen;
    remaining -= cfglen;
  }
  while (remaining >= (int)sizeof(struct usb_desc_s))
    {
      FAR const struct usb_desc_s *desc =
        (FAR const struct usb_desc_s *)configdesc;

      if (desc->len < sizeof(struct usb_desc_s) || desc->len > remaining)
        {
          return -EINVAL;
        }

      if (desc->type == USB_DESC_TYPE_INTERFACE &&
          desc->len >= USB_SIZEOF_IFDESC)
        {
          FAR const struct usb_ifdesc_s *ifdesc =
            (FAR const struct usb_ifdesc_s *)configdesc;
          in_interface = ifdesc->classid == USB_CLASS_VENDOR_SPEC &&
                         (found & BK7258_CH34X_FOUND_IF) == 0;
          if (in_interface)
            {
              found |= BK7258_CH34X_FOUND_IF;
            }
        }
      else if (desc->type == USB_DESC_TYPE_ENDPOINT && in_interface &&
               desc->len >= USB_SIZEOF_EPDESC)
        {
          FAR const struct usb_epdesc_s *epdesc =
            (FAR const struct usb_epdesc_s *)configdesc;
          uint16_t mps = bk7258_ch34x_getle16(epdesc->mxpacketsize);

          if ((epdesc->attr & USB_EP_ATTR_XFERTYPE_MASK) ==
              USB_EP_ATTR_XFER_BULK && mps != 0)
            {
              if (USB_ISEPIN(epdesc->addr))
                {
                  if ((found & BK7258_CH34X_FOUND_IN) != 0)
                    {
                      return -EINVAL;
                    }

                  found |= BK7258_CH34X_FOUND_IN;
                  bindesc.hport = hport;
                  bindesc.addr = epdesc->addr & USB_EP_ADDR_NUMBER_MASK;
                  bindesc.in = true;
                  bindesc.xfrtype = USB_EP_ATTR_XFER_BULK;
                  bindesc.interval = epdesc->interval;
                  bindesc.mxpacketsize = mps;
                }
              else
                {
                  if ((found & BK7258_CH34X_FOUND_OUT) != 0)
                    {
                      return -EINVAL;
                    }

                  found |= BK7258_CH34X_FOUND_OUT;
                  boutdesc.hport = hport;
                  boutdesc.addr = epdesc->addr & USB_EP_ADDR_NUMBER_MASK;
                  boutdesc.in = false;
                  boutdesc.xfrtype = USB_EP_ATTR_XFER_BULK;
                  boutdesc.interval = epdesc->interval;
                  boutdesc.mxpacketsize = mps;
                }
            }
        }

      configdesc += desc->len;
      remaining -= desc->len;
    }

  if (found != BK7258_CH34X_FOUND_ALL)
    {
      return -EINVAL;
    }

  ret = DRVR_EPALLOC(hport->drvr, &boutdesc, &priv->bulkout);
  if (ret < 0)
    {
      return bk7258_ch34x_usbret(ret);
    }

  ret = DRVR_EPALLOC(hport->drvr, &bindesc, &priv->bulkin);
  if (ret < 0)
    {
      DRVR_EPFREE(hport->drvr, priv->bulkout);
      priv->bulkout = NULL;
      return bk7258_ch34x_usbret(ret);
    }

  priv->endpoints = true;
  priv->pktsize = bindesc.mxpacketsize;
  if (boutdesc.mxpacketsize < priv->pktsize)
    {
      priv->pktsize = boutdesc.mxpacketsize;
    }

  return OK;
}

static int bk7258_ch34x_alloc_buffers(
  FAR struct bk7258_usbserial_ch34x_s *priv)
{
  FAR struct usbhost_hubport_s *hport = priv->usbclass.hport;
  size_t maxlen;
  int ret;

  ret = DRVR_ALLOC(hport->drvr, &priv->ctrlreq, &maxlen);
  if (ret < 0 || maxlen < sizeof(struct usb_ctrlreq_s))
    {
      ret = ret < 0 ? bk7258_ch34x_usbret(ret) : -ENOMEM;
      goto errout;
    }

  ret = DRVR_IOALLOC(hport->drvr, &priv->ctrlbuf, 2);
  if (ret < 0)
    {
      ret = bk7258_ch34x_usbret(ret);
      goto errout;
    }

  ret = DRVR_IOALLOC(hport->drvr, &priv->inbuf, priv->pktsize);
  if (ret < 0)
    {
      ret = bk7258_ch34x_usbret(ret);
      goto errout;
    }

  ret = DRVR_IOALLOC(hport->drvr, &priv->outbuf, priv->pktsize);
  if (ret < 0)
    {
      ret = bk7258_ch34x_usbret(ret);
      goto errout;
    }

  return OK;

errout:
  bk7258_ch34x_free_buffers(priv);
  return ret;
}

static void bk7258_ch34x_free_buffers(
  FAR struct bk7258_usbserial_ch34x_s *priv)
{
  FAR struct usbhost_hubport_s *hport = priv->usbclass.hport;

  if (priv->outbuf != NULL)
    {
      DRVR_IOFREE(hport->drvr, priv->outbuf);
      priv->outbuf = NULL;
    }

  if (priv->inbuf != NULL)
    {
      DRVR_IOFREE(hport->drvr, priv->inbuf);
      priv->inbuf = NULL;
    }

  if (priv->ctrlbuf != NULL)
    {
      DRVR_IOFREE(hport->drvr, priv->ctrlbuf);
      priv->ctrlbuf = NULL;
    }

  if (priv->ctrlreq != NULL)
    {
      DRVR_FREE(hport->drvr, priv->ctrlreq);
      priv->ctrlreq = NULL;
    }
}

static void bk7258_ch34x_rxcallback(FAR void *arg, ssize_t nread)
{
  FAR struct bk7258_usbserial_ch34x_s *priv = arg;
  irqstate_t flags;
  uint8_t slot;
  bool queue;
  clock_t delay;

  /* The HCD may invoke this callback from interrupt context.  In
   * particular, do not touch the UART ring or wait for a worker here.  The
   * callback is also the point at which ownership of inbuf returns from the
   * HCD to this class. */

  flags = spin_lock_irqsave(&priv->rxlock);
  if (!priv->rx_active)
    {
      spin_unlock_irqrestore(&priv->rxlock, flags);
      return;
    }

  priv->rx_active = false;
  priv->rx_pending = true;
  priv->rx_result = nread;
  slot = priv->rx_worker == 0 ? 1 : 0;
  queue = priv->rxena && !priv->disconnected && nread != -ESHUTDOWN &&
          nread != -ENODEV;
  delay = nread == -EAGAIN ? BK7258_CH34X_XFERDELAY : 0;
  spin_unlock_irqrestore(&priv->rxlock, flags);

  if (queue)
    {
      /* The alternate item is normally free because the completion callback
       * runs after the submitting worker returned.  If an unusual HCD calls
       * back inline, the submitting item is the safe fallback once that
       * worker has returned; retaining rx_pending lets a later enable retry
       * if both items are temporarily busy. */

      if (bk7258_ch34x_queue_rx_slot(priv, slot, delay) == -EBUSY)
        {
          (void)bk7258_ch34x_queue_rx_slot(priv,
                                           slot == 0 ? 1 : 0, delay);
        }
    }
}

static void bk7258_ch34x_rxwork_common(
  FAR struct bk7258_usbserial_ch34x_s *priv, uint8_t slot)
{
  FAR struct usbhost_hubport_s *hport;
  FAR struct uart_dev_s *uartdev;
  irqstate_t flags;
  ssize_t nread = 0;
  size_t ndx;
  size_t copied = 0;
  bool have_result;
  bool retry;
  int ret;

  DEBUGASSERT(priv != NULL);

  /* Mark the worker before inspecting the state.  This closes the race in
   * which a repeated RX enable queues the other work item while this one is
   * consuming the previous completion. */

  flags = spin_lock_irqsave(&priv->rxlock);
  if (priv->rx_working)
    {
      spin_unlock_irqrestore(&priv->rxlock, flags);
      return;
    }

  priv->rx_working = true;
  if (!priv->rxena || priv->disconnected)
    {
      priv->rx_pending = false;
      priv->rx_working = false;
      spin_unlock_irqrestore(&priv->rxlock, flags);
      return;
    }

  have_result = priv->rx_pending;
  if (have_result)
    {
      nread = priv->rx_result;
      priv->rx_pending = false;
    }
  spin_unlock_irqrestore(&priv->rxlock, flags);

  hport = priv->usbclass.hport;
  uartdev = &priv->uartdev;
  if (hport == NULL || priv->bulkin == NULL || priv->inbuf == NULL)
    {
      flags = spin_lock_irqsave(&priv->rxlock);
      priv->rx_working = false;
      spin_unlock_irqrestore(&priv->rxlock, flags);
      return;
    }

  if (have_result)
    {
      if (nread > priv->pktsize)
        {
          uerr("CH34x RX over-length transfer: %d\n", (int)nread);
          nread = -EIO;
        }

      if (nread > 0)
        {
          flags = uart_spinlock(uartdev, false);
          for (ndx = 0; ndx < (size_t)nread; ndx++)
            {
              unsigned int next = uartdev->recv.head + 1;
              if (next >= (unsigned int)uartdev->recv.size)
                {
                  next = 0;
                }

              if (next == (unsigned int)uartdev->recv.tail)
                {
                  break;
                }

              uartdev->recv.buffer[uartdev->recv.head] = priv->inbuf[ndx];
              uartdev->recv.head = next;
              copied++;
            }
          uart_spinunlock(uartdev, false, flags);
          if (copied != 0)
            {
              uart_datareceived(uartdev);
            }
        }
      else if (nread < 0 && nread != -EAGAIN && nread != -ESHUTDOWN &&
               nread != -ENODEV && !priv->disconnected)
        {
          uerr("CH34x RX transfer failed: %d\n", (int)nread);
        }
    }

  /* Reserve the shared DMA buffer before calling the HCD.  There can be at
   * most one asynchronous IN request, and no worker may reuse inbuf while it
   * is owned by that request. */

  flags = spin_lock_irqsave(&priv->rxlock);
  if (!priv->rxena || priv->disconnected || priv->rx_active ||
      priv->rx_pending)
    {
      priv->rx_working = false;
      spin_unlock_irqrestore(&priv->rxlock, flags);
      return;
    }

  priv->rx_worker = slot;
  priv->rx_active = true;
  spin_unlock_irqrestore(&priv->rxlock, flags);

  ret = DRVR_ASYNCH(hport->drvr, priv->bulkin, priv->inbuf, priv->pktsize,
                    bk7258_ch34x_rxcallback, priv);
  if (ret > 0)
    {
      /* The standard asynchronous contract returns zero or a negative
       * errno.  Do not leave rx_active latched if a non-conforming HCD
       * reports a transfer length here. */

      ret = -EIO;
    }

  flags = spin_lock_irqsave(&priv->rxlock);
  if (ret < 0)
    {
      priv->rx_active = false;
    }
  retry = ret < 0 && priv->rxena && !priv->disconnected;
  priv->rx_working = false;
  spin_unlock_irqrestore(&priv->rxlock, flags);

  if (ret < 0 && retry && ret != -ESHUTDOWN && ret != -ENODEV)
    {
      (void)bk7258_ch34x_queue_rx_slot(priv, slot == 0 ? 1 : 0,
                                       BK7258_CH34X_XFERDELAY);
    }
}

static void bk7258_ch34x_rxwork(FAR void *arg)
{
  bk7258_ch34x_rxwork_common(arg, 0);
}

static void bk7258_ch34x_rxcompletework(FAR void *arg)
{
  bk7258_ch34x_rxwork_common(arg, 1);
}

static void bk7258_ch34x_txwork(FAR void *arg)
{
  FAR struct bk7258_usbserial_ch34x_s *priv = arg;
  FAR struct usbhost_hubport_s *hport;
  FAR struct uart_dev_s *uartdev;
  irqstate_t flags;
  unsigned int starttail;
  unsigned int count = 0;
  unsigned int ndx;
  ssize_t nwritten;

  DEBUGASSERT(priv != NULL);
  hport = priv->usbclass.hport;
  uartdev = &priv->uartdev;
  if (!priv->txena || priv->disconnected || hport == NULL ||
      priv->bulkout == NULL)
    {
      return;
    }

  /* Copy without advancing tail.  If a short transfer occurs, bytes remain
   * queued and are retried in their original order. */

  flags = uart_spinlock(uartdev, false);
  starttail = uartdev->xmit.tail;
  ndx = starttail;
  while (ndx != (unsigned int)uartdev->xmit.head && count < priv->pktsize)
    {
      priv->outbuf[count++] = uartdev->xmit.buffer[ndx];
      if (++ndx >= (unsigned int)uartdev->xmit.size)
        {
          ndx = 0;
        }
    }
  uart_spinunlock(uartdev, false, flags);

  if (count != 0)
    {
      nwritten = DRVR_TRANSFER(hport->drvr, priv->bulkout, priv->outbuf,
                               count);
      if (nwritten > (ssize_t)count)
        {
          nwritten = -EIO;
        }

      if (nwritten > 0)
        {
          flags = uart_spinlock(uartdev, false);
          /* No other worker advances tail, so this remains the start of the
           * packet.  The upper half may only have changed head meanwhile. */
          uartdev->xmit.tail = starttail;
          for (ndx = 0; ndx < (unsigned int)nwritten; ndx++)
            {
              if (++uartdev->xmit.tail >= uartdev->xmit.size)
                {
                  uartdev->xmit.tail = 0;
                }
            }
          uart_spinunlock(uartdev, false, flags);
          uart_datasent(uartdev);
        }
      else if (nwritten < 0 && nwritten != -EAGAIN &&
               nwritten != -ESHUTDOWN && !priv->disconnected)
        {
          uerr("CH34x TX transfer failed: %d\n", (int)nwritten);
        }
    }

  bk7258_ch34x_queue_tx(priv, BK7258_CH34X_XFERDELAY);
}

#ifdef CONFIG_BK7258_USBHOST_CH34X_VALIDATION
static int bk7258_ch34x_validation_thread(int argc, FAR char *argv[])
{
  static const uint8_t pattern[] = "BK7258-CH34X-LOOP";
  uint8_t received[sizeof(pattern) - 1];
  struct pollfd pfd;
  size_t offset = 0;
  ssize_t nbytes;
  int fd;
  int ret;

  (void)argc;
  (void)argv;
  nxsig_usleep(300 * 1000);

  fd = open(BK7258_CH34X_DEVNAME, O_RDWR | O_NONBLOCK);
  if (fd < 0)
    {
      syslog(LOG_ERR, "bk7258-ch34x: open failed: %d\n", errno);
      return -errno;
    }

  nbytes = write(fd, pattern, sizeof(pattern) - 1);
  if (nbytes != sizeof(pattern) - 1)
    {
      syslog(LOG_ERR, "bk7258-ch34x: TX failed: %d/%ld\n",
             errno, (long)nbytes);
      close(fd);
      return nbytes < 0 ? -errno : -EIO;
    }

  syslog(LOG_INFO, "bk7258-ch34x: TX PASS bytes=%u\n",
         (unsigned int)(sizeof(pattern) - 1));

  pfd.fd = fd;
  pfd.events = POLLIN;
  while (offset < sizeof(received))
    {
      pfd.revents = 0;
      ret = poll(&pfd, 1, 10000);
      if (ret <= 0)
        {
          syslog(LOG_ERR, "bk7258-ch34x: RX timeout/error: %d\n",
                 ret < 0 ? errno : ETIMEDOUT);
          close(fd);
          return ret < 0 ? -errno : -ETIMEDOUT;
        }

      nbytes = read(fd, received + offset, sizeof(received) - offset);
      if (nbytes < 0)
        {
          if (errno == EAGAIN)
            {
              continue;
            }

          syslog(LOG_ERR, "bk7258-ch34x: RX failed: %d\n", errno);
          close(fd);
          return -errno;
        }

      offset += nbytes;
    }

  close(fd);
  if (memcmp(pattern, received, sizeof(received)) != 0)
    {
      syslog(LOG_ERR, "bk7258-ch34x: loopback mismatch\n");
      return -EIO;
    }

  syslog(LOG_INFO, "bk7258-ch34x: LOOPBACK PASS bytes=%u\n",
         (unsigned int)sizeof(received));
  return OK;
}
#endif

static FAR struct usbhost_class_s *bk7258_ch34x_create(
  FAR struct usbhost_hubport_s *hport,
  FAR const struct usbhost_id_s *id)
{
  FAR struct bk7258_usbserial_ch34x_s *priv = &g_bk7258_ch34x;
  irqstate_t flags;

  (void)id;
  if (hport == NULL || hport->drvr == NULL)
    {
      return NULL;
    }

  flags = spin_lock_irqsave(&g_bk7258_ch34x_alloc_lock);
  if (g_bk7258_ch34x_inuse)
    {
      spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);
      return NULL;
    }

  g_bk7258_ch34x_inuse = true;
  spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);

  memset(priv, 0, sizeof(*priv));
  nxmutex_init(&priv->lock);
  spin_lock_init(&priv->rxlock);
  priv->usbclass.hport = hport;
  priv->usbclass.connect = bk7258_ch34x_connect;
  priv->usbclass.disconnected = bk7258_ch34x_disconnected;
  priv->crefs = 1;
  priv->lcr = BK7258_CH34X_LCR_ENABLE_RX |
             BK7258_CH34X_LCR_ENABLE_TX |
             BK7258_CH34X_LCR_CS8;
  priv->mcr = BK7258_CH34X_BIT_RTS | BK7258_CH34X_BIT_DTR;

  priv->uartdev.recv.size = sizeof(priv->rxbuffer);
  priv->uartdev.recv.buffer = priv->rxbuffer;
  priv->uartdev.xmit.size = sizeof(priv->txbuffer);
  priv->uartdev.xmit.buffer = priv->txbuffer;
  priv->uartdev.ops = &g_bk7258_ch34x_uart_ops;
  priv->uartdev.priv = priv;
  return &priv->usbclass;
}

static int bk7258_ch34x_connect(FAR struct usbhost_class_s *usbclass,
  FAR const uint8_t *configdesc, int desclen)
{
  FAR struct bk7258_usbserial_ch34x_s *priv =
    (FAR struct bk7258_usbserial_ch34x_s *)usbclass;
  bool destroy = false;
  irqstate_t flags;
  int ret;

  if (priv == NULL || configdesc == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&g_bk7258_ch34x_alloc_lock);
  priv->crefs++;
  spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);
  ret = bk7258_ch34x_cfgdesc(priv, configdesc, desclen);
  if (ret < 0)
    {
      uerr("CH34x descriptor/endpoint setup failed: %d\n", ret);
    }

  if (ret >= 0)
    {
      ret = bk7258_ch34x_alloc_buffers(priv);
      if (ret < 0)
        {
          uerr("CH34x transfer-buffer allocation failed: %d\n", ret);
        }
    }
  if (ret >= 0)
    {
      ret = bk7258_ch34x_configure(priv);
      if (ret < 0)
        {
          uerr("CH34x device configuration failed: %d\n", ret);
        }
    }
  if (ret >= 0)
    {
      flags = spin_lock_irqsave(&g_bk7258_ch34x_alloc_lock);
      if (priv->disconnected)
        {
          ret = -ENODEV;
        }
      spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);

      if (ret >= 0)
        {
          ret = uart_register(BK7258_CH34X_DEVNAME, &priv->uartdev);
          if (ret < 0)
            {
              uerr("CH34x UART registration failed: %d\n", ret);
            }

          if (ret >= 0)
            {
              priv->registered = true;
              uinfo("Registered %s (CH34x version 0x%02x)\n",
                    BK7258_CH34X_DEVNAME, priv->version);
#ifdef CONFIG_SERIAL_REMOVABLE
              uart_connected(&priv->uartdev, true);
#endif
#ifdef CONFIG_BK7258_USBHOST_CH34X_VALIDATION
              ret = kthread_create("bk7258-ch34x", SCHED_PRIORITY_DEFAULT,
                                   2048, bk7258_ch34x_validation_thread,
                                   NULL);
              if (ret < 0)
                {
                  uerr("CH34x validation worker failed: %d\n", ret);
                }
              else
                {
                  ret = OK;
                }
#endif
            }
        }
    }

  flags = spin_lock_irqsave(&g_bk7258_ch34x_alloc_lock);
  priv->crefs--;
  destroy = priv->disconnected && priv->crefs == 1;
  spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);
  nxmutex_unlock(&priv->lock);

  /* A disconnect can race the non-ISR connect callback.  The USB host has
   * already delivered disconnected() in this case, so the final connect
   * reference owns the cleanup rather than waiting for a second callback. */

  if (destroy)
    {
      bk7258_ch34x_destroy(priv);
    }

  return ret;
}

static int bk7258_ch34x_disconnected(FAR struct usbhost_class_s *usbclass)
{
  FAR struct bk7258_usbserial_ch34x_s *priv =
    (FAR struct bk7258_usbserial_ch34x_s *)usbclass;
  FAR struct usbhost_hubport_s *hport;
  bool destroy = false;
  int16_t crefs;

  DEBUGASSERT(priv != NULL && priv->usbclass.hport != NULL);
  hport = priv->usbclass.hport;
  irqstate_t rxflags = spin_lock_irqsave(&priv->rxlock);
  priv->disconnected = true;
  priv->rxena = false;
  spin_unlock_irqrestore(&priv->rxlock, rxflags);
  priv->txena = false;
#ifdef CONFIG_SERIAL_REMOVABLE
  if (priv->registered)
    {
      uart_connected(&priv->uartdev, false);
    }
#endif

  work_cancel(LPWORK, &priv->rxwork);
  work_cancel(LPWORK, &priv->rxcompletework);
  work_cancel(LPWORK, &priv->txwork);
  if (priv->bulkin != NULL)
    {
      (void)DRVR_CANCEL(hport->drvr, priv->bulkin);
    }
  if (priv->bulkout != NULL)
    {
      (void)DRVR_CANCEL(hport->drvr, priv->bulkout);
    }

  irqstate_t flags = spin_lock_irqsave(&g_bk7258_ch34x_alloc_lock);
  crefs = priv->crefs;
  spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);
  if (crefs == 1)
    {
      destroy = true;
    }

  if (destroy)
    {
      if (up_interrupt_context())
        {
          int ret = work_queue(HPWORK, &priv->destroywork,
                               bk7258_ch34x_destroy, priv, 0);
          DEBUGASSERT(ret >= 0);
          (void)ret;
        }
      else
        {
          bk7258_ch34x_destroy(priv);
        }
    }

  return OK;
}

static void bk7258_ch34x_destroy(FAR void *arg)
{
  FAR struct bk7258_usbserial_ch34x_s *priv = arg;
  FAR struct usbhost_hubport_s *hport;

  DEBUGASSERT(priv != NULL && priv->usbclass.hport != NULL);
  hport = priv->usbclass.hport;
  work_cancel_sync(LPWORK, &priv->rxwork);
  work_cancel_sync(LPWORK, &priv->rxcompletework);
  work_cancel_sync(LPWORK, &priv->txwork);

  /* DRVR_CANCEL completes the outstanding asynchronous callback before it
   * returns.  The work cancellations above then guarantee that no worker can
   * still inspect the result or inbuf before those buffers are freed.  The
   * disconnect callback normally performs this cancellation first; repeat it
   * here so every destroy path owns the same endpoint lifetime guarantee. */

  if (priv->bulkin != NULL)
    {
      (void)DRVR_CANCEL(hport->drvr, priv->bulkin);
    }

  irqstate_t rxflags = spin_lock_irqsave(&priv->rxlock);
  priv->rx_active = false;
  priv->rx_pending = false;
  priv->rx_working = false;
  spin_unlock_irqrestore(&priv->rxlock, rxflags);

  if (priv->registered)
    {
      unregister_driver(BK7258_CH34X_DEVNAME);
      priv->registered = false;
    }
  if (priv->endpoints)
    {
      DRVR_EPFREE(hport->drvr, priv->bulkin);
      DRVR_EPFREE(hport->drvr, priv->bulkout);
      priv->bulkin = NULL;
      priv->bulkout = NULL;
      priv->endpoints = false;
    }
  bk7258_ch34x_free_buffers(priv);
  nxmutex_destroy(&priv->lock);
  DRVR_DISCONNECT(hport->drvr, hport);

  irqstate_t flags = spin_lock_irqsave(&g_bk7258_ch34x_alloc_lock);
  g_bk7258_ch34x_inuse = false;
  spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);
}

static int bk7258_ch34x_setup(FAR struct uart_dev_s *uartdev)
{
  FAR struct bk7258_usbserial_ch34x_s *priv =
    bk7258_ch34x_from_uart(uartdev);
  irqstate_t flags;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&g_bk7258_ch34x_alloc_lock);
  if (priv->disconnected)
    {
      ret = -ENODEV;
    }
  else
    {
      priv->crefs++;
      ret = OK;
    }
  spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);
  nxmutex_unlock(&priv->lock);
  return ret;
}

static void bk7258_ch34x_shutdown(FAR struct uart_dev_s *uartdev)
{
  FAR struct bk7258_usbserial_ch34x_s *priv =
    bk7258_ch34x_from_uart(uartdev);
  irqstate_t flags;
  bool destroy;

  nxmutex_lock(&priv->lock);
  flags = spin_lock_irqsave(&g_bk7258_ch34x_alloc_lock);
  if (priv->crefs > 1)
    {
      priv->crefs--;
    }
  destroy = priv->disconnected && priv->crefs == 1;
  spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);
  nxmutex_unlock(&priv->lock);

  if (destroy)
    {
      bk7258_ch34x_destroy(priv);
    }
}

static int bk7258_ch34x_attach(FAR struct uart_dev_s *uartdev)
{
  (void)uartdev;
  return OK;
}

static void bk7258_ch34x_detach(FAR struct uart_dev_s *uartdev)
{
  (void)uartdev;
}

static int bk7258_ch34x_ioctl(FAR struct file *filep, int cmd,
                              unsigned long arg)
{
  FAR struct uart_dev_s *uartdev = filep->f_inode->i_private;
  FAR struct bk7258_usbserial_ch34x_s *priv =
    bk7258_ch34x_from_uart(uartdev);
  FAR struct termios *termiosp = (FAR struct termios *)arg;
  int ret;

  if (priv->disconnected)
    {
      return -ENODEV;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  switch (cmd)
    {
#ifdef CONFIG_SERIAL_TERMIOS
      case TCGETS:
        if (termiosp == NULL)
          {
            ret = -EINVAL;
            break;
          }

        cfsetispeed(termiosp, 115200);
        cfsetospeed(termiosp, 115200);
        termiosp->c_cflag = CS8;
        ret = OK;
        break;

      case TCSETS:
        if (termiosp == NULL)
          {
            ret = -EINVAL;
            break;
          }

        if (cfgetispeed(termiosp) != 115200 ||
            cfgetospeed(termiosp) != 115200 ||
            (termiosp->c_cflag & (CSIZE | PARENB | PARODD | CSTOPB |
                                  CRTSCTS)) != CS8 ||
            (termiosp->c_iflag & (IXON | IXOFF)) != 0)
          {
            ret = -ENOTSUP;
            break;
          }

        ret = OK;
        break;
#endif

      default:
        ret = -ENOTTY;
        break;
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

static void bk7258_ch34x_rxint(FAR struct uart_dev_s *uartdev, bool enable)
{
  FAR struct bk7258_usbserial_ch34x_s *priv =
    bk7258_ch34x_from_uart(uartdev);
  irqstate_t flags = spin_lock_irqsave(&priv->rxlock);

  priv->rxena = enable && !priv->disconnected;
  if (!enable)
    {
      priv->rx_pending = false;
    }
  spin_unlock_irqrestore(&priv->rxlock, flags);
  if (enable)
    {
      bk7258_ch34x_queue_rx(priv, 0);
    }
  else
    {
      work_cancel(LPWORK, &priv->rxwork);
      work_cancel(LPWORK, &priv->rxcompletework);
    }
}

static bool bk7258_ch34x_rxavailable(FAR struct uart_dev_s *uartdev)
{
  irqstate_t flags;
  bool available;

  flags = uart_spinlock(uartdev, false);
  available = uartdev->recv.head != uartdev->recv.tail;
  uart_spinunlock(uartdev, false, flags);
  return available;
}

static void bk7258_ch34x_txint(FAR struct uart_dev_s *uartdev, bool enable)
{
  FAR struct bk7258_usbserial_ch34x_s *priv =
    bk7258_ch34x_from_uart(uartdev);
  irqstate_t flags = spin_lock_irqsave(&g_bk7258_ch34x_alloc_lock);

  priv->txena = enable && !priv->disconnected;
  spin_unlock_irqrestore(&g_bk7258_ch34x_alloc_lock, flags);
  if (enable)
    {
      bk7258_ch34x_queue_tx(priv, 0);
    }
  else
    {
      work_cancel(LPWORK, &priv->txwork);
    }
}

static bool bk7258_ch34x_txready(FAR struct uart_dev_s *uartdev)
{
  irqstate_t flags;
  unsigned int next;
  bool ready;

  flags = uart_spinlock(uartdev, false);
  next = uartdev->xmit.head + 1;
  if (next >= (unsigned int)uartdev->xmit.size)
    {
      next = 0;
    }
  ready = next != (unsigned int)uartdev->xmit.tail;
  uart_spinunlock(uartdev, false, flags);
  return ready;
}

static bool bk7258_ch34x_txempty(FAR struct uart_dev_s *uartdev)
{
  irqstate_t flags;
  bool empty;

  flags = uart_spinlock(uartdev, false);
  empty = uartdev->xmit.head == uartdev->xmit.tail;
  uart_spinunlock(uartdev, false, flags);
  return empty;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_usbserial_ch34x_initialize(void)
{
  int ret = OK;

  ret = nxmutex_lock(&g_bk7258_ch34x_registry_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_bk7258_ch34x_registered)
    {
      ret = usbhost_registerclass(&g_bk7258_ch34x_registry);
      if (ret >= 0)
        {
          g_bk7258_ch34x_registered = true;
        }
    }

  nxmutex_unlock(&g_bk7258_ch34x_registry_lock);
  return ret;
}
