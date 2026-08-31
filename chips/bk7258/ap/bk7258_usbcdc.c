/****************************************************************************
 * chips/bk7258/ap/bk7258_usbcdc.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 USB device CDC-ACM serial gadget.
 *
 * The immutable v3.1.1.9 CherryUSB component owns the MUSB device
 * controller and endpoint management.  This chip wrapper owns the single
 * class/controller registration and optionally publishes the NuttX serial
 * lower half /dev/ttyGS0.  Board transports bind typed callbacks instead of
 * replacing class symbols or registering endpoints a second time.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_USBCDC

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/irq.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>
#include <nuttx/serial/serial.h>

#include <arch/chip/bk7258_usbcdc.h>

#include <components/cherryusb/usbd_core.h>
#include <components/cherryusb/usbd_cdc.h>
#include <components/cherryusb/usb_cdc.h>

#define BK7258_USBCDC_DEVNAME     "/dev/ttyGS0"
#define BK7258_USBCDC_RXBUFSIZE   256
#define BK7258_USBCDC_TXBUFSIZE   256

#define BK7258_USBCDC_EP_INTR_IN  0x81u
#define BK7258_USBCDC_EP_BULK_OUT 0x02u
#define BK7258_USBCDC_EP_BULK_IN  0x82u
#define BK7258_USBCDC_MPS_BULK    64u
#define BK7258_USBCDC_MPS_INTR    8u

#ifdef CONFIG_BK7258_USBHOST
#  error "BK7258_USBCDC and BK7258_USBHOST share the MUSB controller"
#endif

/* USB descriptor type and CDC functional descriptor constants. */

#define USB_DT_DEVICE     0x01u
#define USB_DT_CONFIG     0x02u
#define USB_DT_STRING     0x03u
#define USB_DT_INTERFACE  0x04u
#define USB_DT_ENDPOINT   0x05u

#define USB_DT_CS_INTERFACE  0x24u
#define CDC_HEADER           0x00u
#define CDC_CALL_MGMT        0x01u
#define CDC_ACM              0x02u
#define CDC_UNION            0x06u

#define USB_ENDPOINT_XFER_INT 0x03u
#define USB_ENDPOINT_XFER_BULK 0x02u

struct bk7258_usbcdc_ring_s
{
  uint8_t data[BK7258_USBCDC_RXBUFSIZE];
  volatile uint16_t head;
  volatile uint16_t tail;
};

struct bk7258_usbcdc_priv_s
{
  struct uart_dev_s uartdev;
  mutex_t lock;
  struct bk7258_usbcdc_ring_s rx;
  struct bk7258_usbcdc_ring_s tx;
  uint8_t rxbuf[BK7258_USBCDC_RXBUFSIZE];
  uint8_t txbuf[BK7258_USBCDC_TXBUFSIZE];
  bool inited;
  bool configured;
  bool rx_enabled;
  bool tx_enabled;
  bool tx_pending;
  bool serial_registered;
  bool opened;
  struct bk7258_usbcdc_config_s config;
};

static const uint8_t g_bk7258_usbcdc_descriptors[] =
{
  18,                       /* bLength */
  USB_DT_DEVICE,            /* bDescriptorType */
  0x00, 0x02,               /* bcdUSB 2.0 */
  0xef,                     /* bDeviceClass (Misc) */
  0x02,                     /* bDeviceSubClass */
  0x01,                     /* bDeviceProtocol */
  BK7258_USBCDC_MPS_BULK,   /* bMaxPacketSize0 */
  CONFIG_BK7258_USBCDC_VID & 0xff,
  (CONFIG_BK7258_USBCDC_VID >> 8) & 0xff,
  CONFIG_BK7258_USBCDC_PID & 0xff,
  (CONFIG_BK7258_USBCDC_PID >> 8) & 0xff,
  0x00, 0x01,               /* bcdDevice 1.0 */
  0,                        /* iManufacturer */
  0,                        /* iProduct */
  0,                        /* iSerialNumber */
  1,                        /* bNumConfigurations */

  /* Configuration */
  9, USB_DT_CONFIG, 0x43, 0x00, 2, 1, 0, 0xc0, 0x32,

  /* Interface 0: CDC ACM */
  9, USB_DT_INTERFACE, 0, 0, 1, 0x02, 0x02, 0x01, 0,

  /* CDC Header */
  5, USB_DT_CS_INTERFACE, CDC_HEADER, 0x10, 0x01,
  /* CDC Call Management */
  5, USB_DT_CS_INTERFACE, CDC_CALL_MGMT, 0x00, 1,
  /* CDC ACM */
  4, USB_DT_CS_INTERFACE, CDC_ACM, 0x02,
  /* CDC Union */
  5, USB_DT_CS_INTERFACE, CDC_UNION, 0, 1,

  /* Endpoint: interrupt IN */
  7, USB_DT_ENDPOINT, BK7258_USBCDC_EP_INTR_IN,
  USB_ENDPOINT_XFER_INT, BK7258_USBCDC_MPS_INTR, 0x10, 0x00,

  /* Interface 1: CDC Data */
  9, USB_DT_INTERFACE, 1, 0, 2, 0x0a, 0x00, 0x00, 0,

  /* Endpoint: bulk OUT */
  7, USB_DT_ENDPOINT, BK7258_USBCDC_EP_BULK_OUT,
  USB_ENDPOINT_XFER_BULK, BK7258_USBCDC_MPS_BULK, 0x00, 0x00,

  /* Endpoint: bulk IN */
  7, USB_DT_ENDPOINT, BK7258_USBCDC_EP_BULK_IN,
  USB_ENDPOINT_XFER_BULK, BK7258_USBCDC_MPS_BULK, 0x00, 0x00,

  0,
};

static struct usbd_endpoint g_bk7258_usbcdc_ep_intr;
static struct usbd_endpoint g_bk7258_usbcdc_ep_out;
static struct usbd_endpoint g_bk7258_usbcdc_ep_in;
static struct usbd_interface g_bk7258_usbcdc_intf;
static struct usbd_interface g_bk7258_usbcdc_data_intf;

static struct bk7258_usbcdc_priv_s g_bk7258_usbcdc =
{
  .lock = NXMUTEX_INITIALIZER,
};

static void bk7258_usbcdc_ep_in_cb(uint8_t ep, uint32_t nbytes);
static void bk7258_usbcdc_ep_out_cb(uint8_t ep, uint32_t nbytes);
static int bk7258_usbcdc_setup(FAR struct uart_dev_s *uartdev);
static void bk7258_usbcdc_shutdown(FAR struct uart_dev_s *uartdev);
static int bk7258_usbcdc_attach(FAR struct uart_dev_s *uartdev);
static void bk7258_usbcdc_detach(FAR struct uart_dev_s *uartdev);
static int bk7258_usbcdc_ioctl(FAR struct file *filep, int cmd,
                               unsigned long arg);
static int bk7258_usbcdc_receive(FAR struct uart_dev_s *uartdev,
                                 FAR unsigned int *ch);
static void bk7258_usbcdc_rxint(FAR struct uart_dev_s *uartdev, bool enable);
static bool bk7258_usbcdc_rxavailable(FAR struct uart_dev_s *uartdev);
static void bk7258_usbcdc_send(FAR struct uart_dev_s *uartdev, int ch);
static void bk7258_usbcdc_txint(FAR struct uart_dev_s *uartdev, bool enable);
static bool bk7258_usbcdc_txready(FAR struct uart_dev_s *uartdev);
static bool bk7258_usbcdc_txempty(FAR struct uart_dev_s *uartdev);
static int bk7258_usbcdc_class_request(
  FAR struct usb_setup_packet *setup, FAR uint8_t **data,
  FAR uint32_t *length);
static void bk7258_usbcdc_notify(uint8_t event, FAR void *arg);

static const struct uart_ops_s g_bk7258_usbcdc_uart_ops =
{
  .setup       = bk7258_usbcdc_setup,
  .shutdown    = bk7258_usbcdc_shutdown,
  .attach      = bk7258_usbcdc_attach,
  .detach      = bk7258_usbcdc_detach,
  .ioctl       = bk7258_usbcdc_ioctl,
  .receive     = bk7258_usbcdc_receive,
  .rxint       = bk7258_usbcdc_rxint,
  .rxavailable = bk7258_usbcdc_rxavailable,
  .send        = bk7258_usbcdc_send,
  .txint       = bk7258_usbcdc_txint,
  .txready     = bk7258_usbcdc_txready,
  .txempty     = bk7258_usbcdc_txempty,
};

static const struct bk7258_usbcdc_config_s g_bk7258_usbcdc_default_config =
{
  .version = BK7258_USBCDC_CONFIG_VERSION,
  .size = sizeof(struct bk7258_usbcdc_config_s),
  .descriptors = g_bk7258_usbcdc_descriptors,
  .ep_intr_in = BK7258_USBCDC_EP_INTR_IN,
  .ep_bulk_out = BK7258_USBCDC_EP_BULK_OUT,
  .ep_bulk_in = BK7258_USBCDC_EP_BULK_IN,
  .ep_out_callback = bk7258_usbcdc_ep_out_cb,
  .ep_in_callback = bk7258_usbcdc_ep_in_cb,
  .class_request = bk7258_usbcdc_class_request,
  .notify = bk7258_usbcdc_notify,
  .register_serial = true,
  .devname = BK7258_USBCDC_DEVNAME,
};

static inline uint16_t bk7258_usbcdc_ring_used(
  FAR const struct bk7258_usbcdc_ring_s *ring)
{
  return (uint16_t)(ring->head - ring->tail);
}

static inline uint16_t bk7258_usbcdc_ring_free(
  FAR const struct bk7258_usbcdc_ring_s *ring)
{
  return (uint16_t)(sizeof(ring->data) - bk7258_usbcdc_ring_used(ring));
}

static bool bk7258_usbcdc_ring_push(FAR struct bk7258_usbcdc_ring_s *ring,
                                    uint8_t byte)
{
  if (bk7258_usbcdc_ring_free(ring) == 0)
    {
      return false;
    }

  ring->data[ring->head & (sizeof(ring->data) - 1)] = byte;
  ring->head++;
  return true;
}

static bool bk7258_usbcdc_ring_pop(FAR struct bk7258_usbcdc_ring_s *ring,
                                   FAR uint8_t *byte)
{
  if (bk7258_usbcdc_ring_used(ring) == 0)
    {
      return false;
    }

  *byte = ring->data[ring->tail & (sizeof(ring->data) - 1)];
  ring->tail++;
  return true;
}

static void bk7258_usbcdc_arm_rx(void)
{
  int ret = usbd_ep_start_read(g_bk7258_usbcdc.config.ep_bulk_out,
                               g_bk7258_usbcdc.rxbuf,
                               sizeof(g_bk7258_usbcdc.rxbuf));
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 USBCDC: RX arm failed: %d\n", ret);
    }
}

static int bk7258_usbcdc_class_request(
  FAR struct usb_setup_packet *setup, FAR uint8_t **data,
  FAR uint32_t *length)
{
  static struct cdc_line_coding coding =
  {
    .dwDTERate = 115200,
    .bCharFormat = 0,
    .bParityType = 0,
    .bDataBits = 8,
  };

  if (setup == NULL || data == NULL || length == NULL)
    {
      return -1;
    }

  switch (setup->bRequest)
    {
      case CDC_REQUEST_SET_LINE_CODING:
        if (setup->wLength != sizeof(coding) || *data == NULL)
          {
            return -1;
          }

        memcpy(&coding, *data, sizeof(coding));
        return 0;

      case CDC_REQUEST_GET_LINE_CODING:
        *data = (FAR uint8_t *)&coding;
        *length = sizeof(coding);
        return 0;

      case CDC_REQUEST_SET_CONTROL_LINE_STATE:
      case CDC_REQUEST_SEND_BREAK:
        return 0;

      default:
        return -1;
    }
}

static void bk7258_usbcdc_notify(uint8_t event, FAR void *arg)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;

  (void)arg;
  if (event == USBD_EVENT_CONFIGURED)
    {
      if (!priv->configured)
        {
          priv->configured = true;
          bk7258_usbcdc_arm_rx();
        }
    }
  else if (event == USBD_EVENT_RESET || event == USBD_EVENT_DISCONNECTED)
    {
      priv->configured = false;
      priv->tx_pending = false;
    }
}

void usbd_configure_done_callback(void)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;

  if (priv->serial_registered)
    {
      bk7258_usbcdc_notify(USBD_EVENT_CONFIGURED, NULL);
    }
}

static void bk7258_usbcdc_ep_out_cb(uint8_t ep, uint32_t nbytes)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;
  irqstate_t flags;
  uint32_t i;

  (void)ep;

  flags = enter_critical_section();
  for (i = 0; i < nbytes; i++)
    {
      (void)bk7258_usbcdc_ring_push(&priv->rx, priv->rxbuf[i]);
    }

  if (priv->rx_enabled)
    {
      uart_recvchars(&priv->uartdev);
    }

  leave_critical_section(flags);
  bk7258_usbcdc_arm_rx();
}

static void bk7258_usbcdc_ep_in_cb(uint8_t ep, uint32_t nbytes)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;
  irqstate_t flags;

  (void)ep;
  (void)nbytes;

  flags = enter_critical_section();
  priv->tx_pending = false;
  if (priv->tx_enabled)
    {
      uart_datasent(&priv->uartdev);
    }

  leave_critical_section(flags);
}

static void bk7258_usbcdc_kick_tx(FAR struct bk7258_usbcdc_priv_s *priv)
{
  irqstate_t flags;
  uint16_t used;
  uint16_t i;
  int ret;

  flags = enter_critical_section();
  if (priv->tx_pending || !priv->configured)
    {
      leave_critical_section(flags);
      return;
    }

  used = bk7258_usbcdc_ring_used(&priv->tx);
  if (used == 0)
    {
      leave_critical_section(flags);
      return;
    }

  if (used > sizeof(priv->txbuf))
    {
      used = sizeof(priv->txbuf);
    }

  for (i = 0; i < used; i++)
    {
      (void)bk7258_usbcdc_ring_pop(&priv->tx, &priv->txbuf[i]);
    }

  priv->tx_pending = true;
  leave_critical_section(flags);

  ret = usbd_ep_start_write(priv->config.ep_bulk_in, priv->txbuf, used);
  if (ret < 0)
    {
      flags = enter_critical_section();
      priv->tx_pending = false;
      leave_critical_section(flags);
      syslog(LOG_ERR, "BK7258 USBCDC: TX start failed: %d\n", ret);
    }
}

static int bk7258_usbcdc_setup(FAR struct uart_dev_s *uartdev)
{
  FAR struct bk7258_usbcdc_priv_s *priv = uartdev->priv;

  priv->opened = true;
  return OK;
}

static void bk7258_usbcdc_shutdown(FAR struct uart_dev_s *uartdev)
{
  FAR struct bk7258_usbcdc_priv_s *priv = uartdev->priv;

  priv->opened = false;
}

static int bk7258_usbcdc_attach(FAR struct uart_dev_s *uartdev)
{
  (void)uartdev;
  return OK;
}

static void bk7258_usbcdc_detach(FAR struct uart_dev_s *uartdev)
{
  (void)uartdev;
}

static int bk7258_usbcdc_ioctl(FAR struct file *filep, int cmd,
                               unsigned long arg)
{
  (void)filep;
  (void)cmd;
  (void)arg;
  return -ENOTTY;
}

static int bk7258_usbcdc_receive(FAR struct uart_dev_s *uartdev,
                                 FAR unsigned int *ch)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;
  uint8_t byte;
  irqstate_t flags;

  (void)uartdev;
  flags = enter_critical_section();
  if (!bk7258_usbcdc_ring_pop(&priv->rx, &byte))
    {
      leave_critical_section(flags);
      return -EAGAIN;
    }

  leave_critical_section(flags);
  *ch = byte;
  return 1;
}

static void bk7258_usbcdc_rxint(FAR struct uart_dev_s *uartdev, bool enable)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;
  irqstate_t flags;

  (void)uartdev;
  flags = enter_critical_section();
  priv->rx_enabled = enable;
  if (enable)
    {
      uart_recvchars(&priv->uartdev);
    }

  leave_critical_section(flags);
}

static bool bk7258_usbcdc_rxavailable(FAR struct uart_dev_s *uartdev)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;

  (void)uartdev;
  return bk7258_usbcdc_ring_used(&priv->rx) != 0;
}

static void bk7258_usbcdc_send(FAR struct uart_dev_s *uartdev, int ch)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;
  irqstate_t flags;

  (void)uartdev;
  flags = enter_critical_section();
  (void)bk7258_usbcdc_ring_push(&priv->tx, (uint8_t)ch);
  leave_critical_section(flags);
  bk7258_usbcdc_kick_tx(priv);
}

static void bk7258_usbcdc_txint(FAR struct uart_dev_s *uartdev, bool enable)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;
  irqstate_t flags;

  (void)uartdev;
  flags = enter_critical_section();
  priv->tx_enabled = enable;
  leave_critical_section(flags);
  if (enable)
    {
      bk7258_usbcdc_kick_tx(priv);
    }
}

static bool bk7258_usbcdc_txready(FAR struct uart_dev_s *uartdev)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;

  (void)uartdev;
  return bk7258_usbcdc_ring_free(&priv->tx) != 0;
}

static bool bk7258_usbcdc_txempty(FAR struct uart_dev_s *uartdev)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;

  (void)uartdev;
  return bk7258_usbcdc_ring_used(&priv->tx) == 0 && !priv->tx_pending;
}

int bk7258_usbcdc_initialize(void)
{
  return bk7258_usbcdc_initialize_with_config(
    &g_bk7258_usbcdc_default_config);
}

int bk7258_usbcdc_initialize_with_config(
  FAR const struct bk7258_usbcdc_config_s *config)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;
  FAR const char *devname;
  int ret;

  if (config == NULL ||
      config->version != BK7258_USBCDC_CONFIG_VERSION ||
      config->size < sizeof(*config) || config->descriptors == NULL ||
      config->ep_intr_in == 0u || config->ep_bulk_out == 0u ||
      config->ep_bulk_in == 0u || config->ep_out_callback == NULL ||
      config->ep_in_callback == NULL || config->class_request == NULL ||
      (config->register_serial &&
       (config->devname == NULL || config->devname[0] == '\0')))
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->inited)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  memcpy(&priv->config, config, sizeof(priv->config));
  devname = priv->config.devname;
  memset(&g_bk7258_usbcdc_ep_intr, 0,
         sizeof(g_bk7258_usbcdc_ep_intr));
  memset(&g_bk7258_usbcdc_ep_out, 0,
         sizeof(g_bk7258_usbcdc_ep_out));
  memset(&g_bk7258_usbcdc_ep_in, 0,
         sizeof(g_bk7258_usbcdc_ep_in));
  memset(&g_bk7258_usbcdc_intf, 0, sizeof(g_bk7258_usbcdc_intf));
  memset(&g_bk7258_usbcdc_data_intf, 0,
         sizeof(g_bk7258_usbcdc_data_intf));

  g_bk7258_usbcdc_ep_intr.ep_addr = config->ep_intr_in;
  g_bk7258_usbcdc_ep_intr.ep_cb   = NULL;
  g_bk7258_usbcdc_ep_out.ep_addr  = config->ep_bulk_out;
  g_bk7258_usbcdc_ep_out.ep_cb    = config->ep_out_callback;
  g_bk7258_usbcdc_ep_in.ep_addr   = config->ep_bulk_in;
  g_bk7258_usbcdc_ep_in.ep_cb     = config->ep_in_callback;

  g_bk7258_usbcdc_intf.class_interface_handler = config->class_request;
  g_bk7258_usbcdc_intf.class_endpoint_handler = NULL;
  g_bk7258_usbcdc_intf.vendor_handler = NULL;
  g_bk7258_usbcdc_intf.notify_handler = config->notify;

  usbd_desc_register(config->descriptors);
  usbd_add_interface(&g_bk7258_usbcdc_intf);
  usbd_add_interface(&g_bk7258_usbcdc_data_intf);
  usbd_add_endpoint(&g_bk7258_usbcdc_ep_intr);
  usbd_add_endpoint(&g_bk7258_usbcdc_ep_out);
  usbd_add_endpoint(&g_bk7258_usbcdc_ep_in);

  if (config->register_serial)
    {
      memset(&priv->rx, 0, sizeof(priv->rx));
      memset(&priv->tx, 0, sizeof(priv->tx));
      priv->uartdev.recv.size   = sizeof(priv->rx.data);
      priv->uartdev.recv.buffer = (FAR char *)priv->rx.data;
      priv->uartdev.xmit.size   = sizeof(priv->tx.data);
      priv->uartdev.xmit.buffer = (FAR char *)priv->tx.data;
      priv->uartdev.ops         = &g_bk7258_usbcdc_uart_ops;
      priv->uartdev.priv        = priv;
      priv->opened              = false;

      ret = uart_register(devname, &priv->uartdev);
      if (ret < 0)
        {
          nxmutex_unlock(&priv->lock);
          return ret;
        }

      priv->serial_registered = true;
    }

  ret = usbd_initialize();
  if (ret < 0)
    {
      if (priv->serial_registered)
        {
          unregister_driver(devname);
          priv->serial_registered = false;
        }

      nxmutex_unlock(&priv->lock);
      return ret;
    }

  priv->inited = true;
  syslog(LOG_INFO, "BK7258 USBCDC: ready consumer=%s vid=%04x pid=%04x\n",
         priv->serial_registered ? devname : "callback",
         CONFIG_BK7258_USBCDC_VID, CONFIG_BK7258_USBCDC_PID);

  nxmutex_unlock(&priv->lock);
  return OK;
}

int bk7258_usbcdc_uninitialize(void)
{
  FAR struct bk7258_usbcdc_priv_s *priv = &g_bk7258_usbcdc;
  int ret;

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->inited)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  if (priv->serial_registered && priv->opened)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  ret = usbd_deinitialize();
  if (ret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  if (priv->serial_registered)
    {
      unregister_driver(priv->config.devname);
      priv->serial_registered = false;
    }

  priv->inited = false;
  priv->configured = false;
  memset(&priv->config, 0, sizeof(priv->config));

  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif /* CONFIG_BK7258_USBCDC */
