/****************************************************************************
 * chips/bk7258/include/bk7258_usbcdc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 USB device CDC-ACM serial gadget.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBCDC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBCDC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/compiler.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_USBCDC) && defined(CONFIG_BK7258_AP_CORE)

#define BK7258_USBCDC_CONFIG_VERSION 1u

struct usb_setup_packet;

typedef void (*bk7258_usbcdc_endpoint_cb_t)(uint8_t ep, uint32_t nbytes);
typedef int (*bk7258_usbcdc_request_cb_t)(
  FAR struct usb_setup_packet *setup, FAR uint8_t **data,
  FAR uint32_t *length);
typedef void (*bk7258_usbcdc_notify_cb_t)(uint8_t event, FAR void *arg);

/* A board or product transport may replace the default serial data path
 * without registering descriptors or endpoints a second time.  The chip
 * layer remains the sole owner of CherryUSB class/controller registration.
 */

struct bk7258_usbcdc_config_s
{
  uint16_t version;
  uint16_t size;
  FAR const uint8_t *descriptors;
  uint8_t ep_intr_in;
  uint8_t ep_bulk_out;
  uint8_t ep_bulk_in;
  bk7258_usbcdc_endpoint_cb_t ep_out_callback;
  bk7258_usbcdc_endpoint_cb_t ep_in_callback;
  bk7258_usbcdc_request_cb_t class_request;
  bk7258_usbcdc_notify_cb_t notify;
  bool register_serial;
  FAR const char *devname;
};

/* Register the CherryUSB device controller and /dev/ttyGS0 CDC-ACM serial
 * gadget.  The MUSB controller is owned exclusively by either the host or
 * the device wrapper; enabling both is rejected at build time.
 */

int bk7258_usbcdc_initialize(void);
int bk7258_usbcdc_initialize_with_config(
  FAR const struct bk7258_usbcdc_config_s *config);
int bk7258_usbcdc_uninitialize(void);

#endif /* CONFIG_BK7258_USBCDC && CONFIG_BK7258_AP_CORE */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBCDC_H */
