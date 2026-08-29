/****************************************************************************
 * boards/bk7258/aidk_ai_toy/src/bk7258_aidk_usb_ota.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AIDK native-USB transport for the existing signed paired OTA manager.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AIDK_USB_OTA

#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/signal.h>

#include <arch/chip/bk7258_ota_catalog.h>
#include <arch/chip/bk7258_ota_manager.h>
#include <arch/chip/bk7258_usbcdc.h>

#include <components/cherryusb/usbd_core.h>
#include <components/cherryusb/usbd_cdc.h>
#include <components/cherryusb/usb_cdc.h>

#define AIDK_USB_OTA_MAGIC            0x314f5441u /* "ATO1", little-endian */
#define AIDK_USB_OTA_VERSION          1u
#define AIDK_USB_OTA_HEADER_SIZE      36u
#define AIDK_USB_OTA_MAX_PAYLOAD      128u
#define AIDK_USB_OTA_PROGRESS_STEP    (64u * 1024u)
#define AIDK_USB_OTA_EP_OUT           0x02u
#define AIDK_USB_OTA_EP_IN            0x82u
#define AIDK_USB_OTA_EP_MAXPACKET     64u
#define AIDK_USB_OTA_RX_RING_SIZE     512u
#define AIDK_USB_OTA_REENUMERATE_US   250000u

enum aidk_usb_ota_type_e
{
  AIDK_USB_OTA_HELLO = 1,
  AIDK_USB_OTA_ACK,
  AIDK_USB_OTA_START,
  AIDK_USB_OTA_READ,
  AIDK_USB_OTA_DATA,
  AIDK_USB_OTA_PROGRESS,
  AIDK_USB_OTA_DONE,
  AIDK_USB_OTA_CANCEL,
  AIDK_USB_OTA_ERROR
};

enum aidk_usb_ota_object_e
{
  AIDK_USB_OTA_OBJECT_CATALOG = 0,
  AIDK_USB_OTA_OBJECT_SIGNATURE,
  AIDK_USB_OTA_OBJECT_CP,
  AIDK_USB_OTA_OBJECT_AP
};

struct aidk_usb_ota_frame_s
{
  uint16_t type;
  uint32_t sequence;
  uint32_t object;
  uint32_t offset;
  int32_t status;
  uint32_t value;
  uint32_t payload_size;
  uint8_t payload[AIDK_USB_OTA_MAX_PAYLOAD];
};

struct aidk_usb_ota_source_s
{
  uint32_t next_sequence;
  uint32_t catalog_size;
  uint32_t signature_size;
  uint32_t last_completed;
  enum bk7258_ota_phase_e last_phase;
  bool have_progress;
  volatile bool canceled;
  struct bk7258_ota_catalog_s catalog;
};

/* Endpoint callbacks are the sole producers and the pinned worker is the sole
 * consumer.  The naturally aligned 8/16/32-bit indices and flags are atomic
 * on Cortex-M33, so the IRQ path never depends on a non-exported OS lock.
 */

struct aidk_usb_ota_link_s
{
  uint8_t rx_packet[AIDK_USB_OTA_EP_MAXPACKET];
  uint8_t rx_ring[AIDK_USB_OTA_RX_RING_SIZE];
  volatile uint16_t rx_head;
  volatile uint16_t rx_tail;
  volatile bool rx_overflow;
  volatile bool configured;
  volatile bool tx_pending;
  volatile uint32_t tx_completed;
};

static struct aidk_usb_ota_link_s g_aidk_usb_ota_link;
static struct usbd_endpoint g_aidk_usb_ota_ep_out;
static struct usbd_endpoint g_aidk_usb_ota_ep_in;

/* CherryUSB consumes one flat descriptor stream terminated by zero.  The
 * reusable CDC lower half owns endpoints and class requests; this physical
 * board supplies the complete device+configuration stream and installs it
 * immediately after lower-half registration, before AP READY is published.
 */

static const uint8_t g_aidk_usb_ota_descriptors[] =
{
  /* Device descriptor. */

  18, 0x01, 0x00, 0x02, 0xef, 0x02, 0x01, 64,
  CONFIG_BK7258_USBCDC_VID & 0xff,
  (CONFIG_BK7258_USBCDC_VID >> 8) & 0xff,
  CONFIG_BK7258_USBCDC_PID & 0xff,
  (CONFIG_BK7258_USBCDC_PID >> 8) & 0xff,
  0x00, 0x01, 0, 0, 0, 1,

  /* Configuration, CDC control interface and CDC data interface. */

  9, 0x02, 0x4b, 0x00, 2, 1, 0, 0xc0, 0x32,
  8, 0x0b, 0, 2, 0x02, 0x02, 0x01, 0,
  9, 0x04, 0, 0, 1, 0x02, 0x02, 0x01, 0,
  5, 0x24, 0x00, 0x10, 0x01,
  5, 0x24, 0x01, 0x00, 1,
  4, 0x24, 0x02, 0x02,
  5, 0x24, 0x06, 0, 1,
  7, 0x05, 0x81, 0x03, 8, 0, 0x10,
  9, 0x04, 1, 0, 2, 0x0a, 0x00, 0x00, 0,
  7, 0x05, 0x02, 0x02, 64, 0, 0,
  7, 0x05, 0x82, 0x02, 64, 0, 0,
  0
};

/* The immutable AP bundle exposes CherryUSB core/device-controller symbols
 * but omits the small CDC ACM class object.  Keep the physical-board fallback
 * deliberately limited to the four mandatory ACM control requests.  The
 * reusable lower half registers the controller and CDC interfaces; this board
 * owns the OTA endpoint pump because the bundled MUSB OUT completion path does
 * not copy the received FIFO bytes into its generic serial buffer.
 */

static void aidk_usb_ota_ep_out_callback(uint8_t ep, uint32_t nbytes)
{
  struct aidk_usb_ota_link_s *link = &g_aidk_usb_ota_link;
  uint32_t index;
  int ret;

  if (ep != AIDK_USB_OTA_EP_OUT || nbytes == 0u ||
      nbytes > sizeof(link->rx_packet))
    {
      link->rx_overflow = true;
      return;
    }

  /* The Beken MUSB port reports OUT completion while the packet is still in
   * the FIFO.  Starting this exact-size read in the endpoint callback copies
   * the arrived packet synchronously and rearms the endpoint for the next one.
   */

  ret = usbd_ep_start_read(ep, link->rx_packet, nbytes);
  if (ret < 0)
    {
      link->rx_overflow = true;
      return;
    }

  for (index = 0; index < nbytes; index++)
    {
      if ((uint16_t)(link->rx_head - link->rx_tail) >=
          sizeof(link->rx_ring))
        {
          link->rx_overflow = true;
          break;
        }

      link->rx_ring[link->rx_head & (sizeof(link->rx_ring) - 1u)] =
        link->rx_packet[index];
      link->rx_head++;
    }
}

static void aidk_usb_ota_ep_in_callback(uint8_t ep, uint32_t nbytes)
{
  struct aidk_usb_ota_link_s *link = &g_aidk_usb_ota_link;

  if (ep != AIDK_USB_OTA_EP_IN)
    {
      return;
    }

  link->tx_completed = nbytes;
  link->tx_pending = false;
}

static int aidk_usb_ota_cdc_request(struct usb_setup_packet *setup,
                                    uint8_t **data, uint32_t *length)
{
  static struct cdc_line_coding coding =
  {
    .dwDTERate = 115200,
    .bCharFormat = 0,
    .bParityType = 0,
    .bDataBits = 8,
  };

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
        *data = (uint8_t *)&coding;
        *length = sizeof(coding);
        return 0;

      case CDC_REQUEST_SET_CONTROL_LINE_STATE:
      case CDC_REQUEST_SEND_BREAK:
        return 0;

      default:
        return -1;
    }
}

static void aidk_usb_ota_cdc_notify(uint8_t event, void *arg)
{
  struct aidk_usb_ota_link_s *link = &g_aidk_usb_ota_link;
  int ret;

  (void)arg;

  if (event == USBD_EVENT_CONFIGURED)
    {
      link->rx_head = 0;
      link->rx_tail = 0;
      link->rx_overflow = false;
      link->configured = true;

      /* The immutable MUSB controller leaves Bulk OUT interrupts disabled
       * until the first read is armed.  The completion callback then performs
       * the real FIFO copy and rearms every following packet.
       */

      ret = usbd_ep_start_read(AIDK_USB_OTA_EP_OUT, link->rx_packet,
                               sizeof(link->rx_packet));
      if (ret < 0)
        {
          link->configured = false;
          link->rx_overflow = true;
          syslog(LOG_ERR, "AIDK USB OTA: initial RX arm failed: %d\n", ret);
        }
    }
  else if (event == USBD_EVENT_RESET ||
           event == USBD_EVENT_DISCONNECTED)
    {
      link->configured = false;
      link->tx_pending = false;
      link->tx_completed = 0;
    }
}

struct usbd_interface *usbd_cdc_acm_init_intf(
  uint8_t busid, struct usbd_interface *intf)
{
  (void)busid;
  intf->class_interface_handler = aidk_usb_ota_cdc_request;
  intf->class_endpoint_handler = NULL;
  intf->vendor_handler = NULL;
  intf->notify_handler = aidk_usb_ota_cdc_notify;
  return intf;
}

static uint16_t aidk_usb_ota_get16(const uint8_t *value)
{
  return (uint16_t)value[0] | (uint16_t)value[1] << 8;
}

static uint32_t aidk_usb_ota_get32(const uint8_t *value)
{
  return (uint32_t)value[0] | (uint32_t)value[1] << 8 |
         (uint32_t)value[2] << 16 | (uint32_t)value[3] << 24;
}

static void aidk_usb_ota_put16(uint8_t *output, uint16_t value)
{
  output[0] = value & 0xffu;
  output[1] = value >> 8;
}

static void aidk_usb_ota_put32(uint8_t *output, uint32_t value)
{
  output[0] = value & 0xffu;
  output[1] = (value >> 8) & 0xffu;
  output[2] = (value >> 16) & 0xffu;
  output[3] = value >> 24;
}

static uint32_t aidk_usb_ota_crc32(const uint8_t *data, size_t size)
{
  uint32_t crc = 0xffffffffu;
  size_t index;
  unsigned int bit;

  for (index = 0; index < size; index++)
    {
      crc ^= data[index];
      for (bit = 0; bit < 8; bit++)
        {
          uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);

          crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }

  return crc ^ 0xffffffffu;
}

static int aidk_usb_ota_transfer(uint8_t *buffer, size_t size, bool writing)
{
  struct aidk_usb_ota_link_s *link = &g_aidk_usb_ota_link;
  clock_t started = clock_systime_ticks();
  clock_t limit = MSEC2TICK(CONFIG_BK7258_AIDK_USB_OTA_IO_TIMEOUT_MS);
  size_t done = 0;
  int ret;

  if (writing)
    {
      if (!link->configured || link->tx_pending)
        {
          return !link->configured ? -ENOTCONN : -EBUSY;
        }

      link->tx_completed = 0;
      link->tx_pending = true;

      ret = usbd_ep_start_write(AIDK_USB_OTA_EP_IN, buffer, size);
      if (ret < 0)
        {
          link->tx_pending = false;
          return ret;
        }

      while (link->tx_pending)
        {
          if (clock_systime_ticks() - started >= limit)
            {
              link->tx_pending = false;
              return -ETIMEDOUT;
            }

          nxsig_usleep(1000);
        }

      if (!link->configured)
        {
          return -ENOTCONN;
        }

      return link->tx_completed == size ? OK : -EIO;
    }

  while (done < size)
    {
      bool have_byte = false;
      bool overflow = false;

      if (link->rx_overflow)
        {
          link->rx_tail = link->rx_head;
          link->rx_overflow = false;
          overflow = true;
        }
      else if (link->rx_tail != link->rx_head)
        {
          buffer[done] =
            link->rx_ring[link->rx_tail & (sizeof(link->rx_ring) - 1u)];
          link->rx_tail++;
          have_byte = true;
        }

      if (overflow)
        {
          return -EOVERFLOW;
        }

      if (have_byte)
        {
          done++;
          continue;
        }

      if (clock_systime_ticks() - started >= limit)
        {
          return -ETIMEDOUT;
        }

      nxsig_usleep(1000);
    }

  return OK;
}

static int aidk_usb_ota_read_frame(struct aidk_usb_ota_frame_s *frame)
{
  uint8_t header[AIDK_USB_OTA_HEADER_SIZE];
  uint32_t window = 0;
  uint32_t crc;
  uint8_t byte;
  int ret;

  do
    {
      ret = aidk_usb_ota_transfer(&byte, 1, false);
      if (ret < 0)
        {
          return ret;
        }

      window = (window >> 8) | (uint32_t)byte << 24;
    }
  while (window != AIDK_USB_OTA_MAGIC);

  aidk_usb_ota_put32(header, AIDK_USB_OTA_MAGIC);
  ret = aidk_usb_ota_transfer(header + 4, sizeof(header) - 4, false);
  if (ret < 0)
    {
      return ret;
    }

  if (aidk_usb_ota_get16(header + 4) != AIDK_USB_OTA_VERSION)
    {
      return -EPROTO;
    }

  memset(frame, 0, sizeof(*frame));
  frame->type = aidk_usb_ota_get16(header + 6);
  frame->sequence = aidk_usb_ota_get32(header + 8);
  frame->object = aidk_usb_ota_get32(header + 12);
  frame->offset = aidk_usb_ota_get32(header + 16);
  frame->status = (int32_t)aidk_usb_ota_get32(header + 20);
  frame->value = aidk_usb_ota_get32(header + 24);
  frame->payload_size = aidk_usb_ota_get32(header + 28);
  crc = aidk_usb_ota_get32(header + 32);
  if (frame->payload_size > sizeof(frame->payload))
    {
      return -EMSGSIZE;
    }

  if (frame->payload_size > 0)
    {
      ret = aidk_usb_ota_transfer(frame->payload, frame->payload_size, false);
      if (ret < 0)
        {
          return ret;
        }
    }

  return aidk_usb_ota_crc32(frame->payload, frame->payload_size) == crc ?
         OK : -EBADMSG;
}

static int aidk_usb_ota_write_frame(uint16_t type,
                                    uint32_t sequence, uint32_t object,
                                    uint32_t offset, int32_t status,
                                    uint32_t value, const uint8_t *payload,
                                    uint32_t payload_size)
{
  uint8_t wire[AIDK_USB_OTA_HEADER_SIZE + AIDK_USB_OTA_MAX_PAYLOAD];

  if ((payload == NULL) != (payload_size == 0u) ||
      payload_size > AIDK_USB_OTA_MAX_PAYLOAD)
    {
      return -EINVAL;
    }

  aidk_usb_ota_put32(wire, AIDK_USB_OTA_MAGIC);
  aidk_usb_ota_put16(wire + 4, AIDK_USB_OTA_VERSION);
  aidk_usb_ota_put16(wire + 6, type);
  aidk_usb_ota_put32(wire + 8, sequence);
  aidk_usb_ota_put32(wire + 12, object);
  aidk_usb_ota_put32(wire + 16, offset);
  aidk_usb_ota_put32(wire + 20, (uint32_t)status);
  aidk_usb_ota_put32(wire + 24, value);
  aidk_usb_ota_put32(wire + 28, payload_size);
  aidk_usb_ota_put32(wire + 32,
                     aidk_usb_ota_crc32(payload, payload_size));
  if (payload_size > 0)
    {
      memcpy(wire + AIDK_USB_OTA_HEADER_SIZE, payload, payload_size);
    }

  return aidk_usb_ota_transfer(wire,
                               AIDK_USB_OTA_HEADER_SIZE + payload_size, true);
}

static int aidk_usb_ota_fetch(struct aidk_usb_ota_source_s *source,
                              enum aidk_usb_ota_object_e object,
                              uint32_t offset, uint8_t *buffer,
                              size_t nbytes)
{
  size_t done = 0;

  while (done < nbytes)
    {
      struct aidk_usb_ota_frame_s reply;
      uint32_t count = nbytes - done;
      uint32_t sequence;
      int ret;

      if (source->canceled)
        {
          return -ECANCELED;
        }

      if (count > AIDK_USB_OTA_MAX_PAYLOAD)
        {
          count = AIDK_USB_OTA_MAX_PAYLOAD;
        }

      if (++source->next_sequence == 0u)
        {
          source->next_sequence++;
        }

      sequence = source->next_sequence;
      ret = aidk_usb_ota_write_frame(AIDK_USB_OTA_READ,
                                     sequence, object, offset + done, 0,
                                     count, NULL, 0);
      if (ret < 0)
        {
          return ret;
        }

      ret = aidk_usb_ota_read_frame(&reply);
      if (ret < 0)
        {
          return ret;
        }

      if (reply.type == AIDK_USB_OTA_CANCEL)
        {
          source->canceled = true;
          return -ECANCELED;
        }

      if (reply.type != AIDK_USB_OTA_DATA || reply.sequence != sequence ||
          reply.object != (uint32_t)object ||
          reply.offset != offset + done || reply.status < 0 ||
          reply.payload_size != count)
        {
          return reply.status < 0 ? reply.status : -EPROTO;
        }

      memcpy(buffer + done, reply.payload, count);
      done += count;
    }

  return OK;
}

static int aidk_usb_ota_source_open(
  void *context, struct bk7258_ota_manifest_s *manifest)
{
  struct aidk_usb_ota_source_s *source = context;
  uint8_t catalog[BK7258_OTA_CATALOG_MAX_SIZE];
  uint8_t signature[BK7258_OTA_CATALOG_MAX_SIGNATURE];
  int ret;

  if (source == NULL || manifest == NULL ||
      source->catalog_size == 0u ||
      source->catalog_size > sizeof(catalog) ||
      source->signature_size == 0u ||
      source->signature_size > sizeof(signature))
    {
      return -EINVAL;
    }

  ret = aidk_usb_ota_fetch(source, AIDK_USB_OTA_OBJECT_CATALOG, 0,
                           catalog, source->catalog_size);
  if (ret == 0)
    {
      ret = aidk_usb_ota_fetch(source, AIDK_USB_OTA_OBJECT_SIGNATURE, 0,
                               signature, source->signature_size);
    }
  if (ret == 0)
    {
      ret = bk7258_ota_catalog_verify(catalog, source->catalog_size,
                                      signature, source->signature_size,
                                      &source->catalog);
    }
  if (ret == 0)
    {
      memcpy(manifest, &source->catalog.manifest, sizeof(*manifest));
    }

  return ret;
}

static int aidk_usb_ota_source_read(
  void *context, enum bk7258_ota_image_e image, uint32_t offset,
  uint8_t *buffer, size_t nbytes)
{
  struct aidk_usb_ota_source_s *source = context;
  enum aidk_usb_ota_object_e object;

  if (source == NULL || buffer == NULL || nbytes == 0u ||
      image < BK7258_OTA_IMAGE_CP || image > BK7258_OTA_IMAGE_AP ||
      (uint64_t)offset + nbytes >
        source->catalog.manifest.image[image].physical_size)
    {
      return -EINVAL;
    }

  object = image == BK7258_OTA_IMAGE_CP ? AIDK_USB_OTA_OBJECT_CP :
                                         AIDK_USB_OTA_OBJECT_AP;
  return aidk_usb_ota_fetch(source, object, offset, buffer, nbytes);
}

static int aidk_usb_ota_source_checkpoint(
  void *context, const struct bk7258_ota_progress_s *progress)
{
  struct aidk_usb_ota_source_s *source = context;
  uint8_t total[4];

  if (source == NULL || progress == NULL)
    {
      return -EINVAL;
    }

  if (source->canceled)
    {
      return -ECANCELED;
    }

  if (!source->have_progress || source->last_phase != progress->phase ||
      progress->completed == progress->total ||
      progress->completed - source->last_completed >=
        AIDK_USB_OTA_PROGRESS_STEP)
    {
      aidk_usb_ota_put32(total, progress->total);
      source->last_phase = progress->phase;
      source->last_completed = progress->completed;
      source->have_progress = true;
      return aidk_usb_ota_write_frame(
               AIDK_USB_OTA_PROGRESS, 0,
               (uint32_t)progress->phase, (uint32_t)progress->image, 0,
               progress->completed, total, sizeof(total));
    }

  return OK;
}

static int aidk_usb_ota_source_cancel(void *context)
{
  struct aidk_usb_ota_source_s *source = context;

  if (source != NULL)
    {
      source->canceled = true;
    }

  return OK;
}

static void aidk_usb_ota_source_close(void *context)
{
  struct aidk_usb_ota_source_s *source = context;

  if (source != NULL)
    {
      memset(&source->catalog, 0, sizeof(source->catalog));
    }
}

static const struct bk7258_ota_source_ops_s g_aidk_usb_ota_source_ops =
{
  .open = aidk_usb_ota_source_open,
  .read_at = aidk_usb_ota_source_read,
  .checkpoint = aidk_usb_ota_source_checkpoint,
  .cancel = aidk_usb_ota_source_cancel,
  .close = aidk_usb_ota_source_close,
};

static int aidk_usb_ota_worker(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  for (;;)
    {
      struct aidk_usb_ota_source_s source;
      struct aidk_usb_ota_frame_s frame;
      int ret;

      ret = aidk_usb_ota_read_frame(&frame);
      if (ret == -ETIMEDOUT)
        {
          continue;
        }

      if (ret < 0 || frame.type != AIDK_USB_OTA_HELLO ||
          frame.payload_size != 0u)
        {
          continue;
        }

      ret = aidk_usb_ota_write_frame(AIDK_USB_OTA_ACK,
                                     frame.sequence, 0, 0, 0, 0, NULL, 0);
      if (ret < 0)
        {
          continue;
        }

      ret = aidk_usb_ota_read_frame(&frame);
      if (ret < 0 || frame.type != AIDK_USB_OTA_START ||
          frame.payload_size != 8u)
        {
          continue;
        }

      memset(&source, 0, sizeof(source));
      source.catalog_size = aidk_usb_ota_get32(frame.payload);
      source.signature_size = aidk_usb_ota_get32(frame.payload + 4);
      if (source.catalog_size == 0u ||
          source.catalog_size > BK7258_OTA_CATALOG_MAX_SIZE ||
          source.signature_size == 0u ||
          source.signature_size > BK7258_OTA_CATALOG_MAX_SIGNATURE)
        {
          (void)aidk_usb_ota_write_frame(AIDK_USB_OTA_ERROR,
                                         frame.sequence, 0, 0, -EINVAL, 0,
                                         NULL, 0);
          continue;
        }

      ret = aidk_usb_ota_write_frame(AIDK_USB_OTA_ACK,
                                     frame.sequence, 0, 0, 0, 0, NULL, 0);
      if (ret == 0)
        {
          ret = bk7258_ota_manager_apply(
                  &g_aidk_usb_ota_source_ops, &source,
                  CONFIG_BK7258_OTA_RPMSG_CONTROL_TIMEOUT_MS);
        }

      (void)aidk_usb_ota_write_frame(AIDK_USB_OTA_DONE,
                                     frame.sequence, 0, 0, ret, 0, NULL, 0);
      syslog(ret == 0 ? LOG_INFO : LOG_ERR,
             "AIDK USB OTA: staging %s: %d\n",
             ret == 0 ? "complete" : "failed", ret);
    }

  return OK;
}

int bk7258_aidk_usb_ota_initialize(void)
{
  pid_t pid;
  int ret;

  ret = bk7258_usbcdc_initialize();
  if (ret < 0)
    {
      return ret;
    }

  /* Replace the generic serial endpoint callbacks with the board OTA packet
   * pump, then force a visible disconnect interval.  This also guarantees
   * that the host enumerates the complete board descriptor registered below,
   * rather than retaining a pre-reset Windows CDC session.
   */

  ret = usb_dc_deinit();
  if (ret < 0)
    {
      (void)bk7258_usbcdc_uninitialize();
      return ret;
    }

  usbd_desc_register(g_aidk_usb_ota_descriptors);
  g_aidk_usb_ota_ep_out.ep_addr = AIDK_USB_OTA_EP_OUT;
  g_aidk_usb_ota_ep_out.ep_cb = aidk_usb_ota_ep_out_callback;
  g_aidk_usb_ota_ep_in.ep_addr = AIDK_USB_OTA_EP_IN;
  g_aidk_usb_ota_ep_in.ep_cb = aidk_usb_ota_ep_in_callback;
  usbd_add_endpoint(&g_aidk_usb_ota_ep_out);
  usbd_add_endpoint(&g_aidk_usb_ota_ep_in);
  nxsig_usleep(AIDK_USB_OTA_REENUMERATE_US);
  ret = usb_dc_init();
  if (ret < 0)
    {
      (void)bk7258_usbcdc_uninitialize();
      return ret;
    }

  pid = kthread_create("aidk-usb-ota",
                       CONFIG_BK7258_AIDK_USB_OTA_PRIORITY,
                       CONFIG_BK7258_AIDK_USB_OTA_STACKSIZE,
                       aidk_usb_ota_worker, NULL);
  if (pid < 0)
    {
      ret = (int)pid;
      (void)bk7258_usbcdc_uninitialize();
      return ret;
    }

#ifdef CONFIG_SMP
  {
    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    ret = sched_setaffinity(pid, sizeof(cpuset), &cpuset);
    if (ret < 0)
      {
        kthread_delete(pid);
        (void)bk7258_usbcdc_uninitialize();
        return ret;
      }
  }
#endif

  syslog(LOG_INFO,
         "AIDK USB OTA: ready ep=%02x/%02x protocol=%u max-payload=%u\n",
         AIDK_USB_OTA_EP_OUT, AIDK_USB_OTA_EP_IN,
         AIDK_USB_OTA_VERSION, AIDK_USB_OTA_MAX_PAYLOAD);
  return OK;
}

#endif /* CONFIG_BK7258_AIDK_USB_OTA */
