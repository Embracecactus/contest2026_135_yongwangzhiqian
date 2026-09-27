/****************************************************************************
 * app/bk7258/bk7258_pc_usb.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_pc_usb.h"
#include <errno.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkpc_usb_close(struct bkpc_usb_s *usb)
{
  int ret;

  if (usb == NULL)
    {
      return -EINVAL;
    }

  /* TLS owns transport callbacks, so release it before the descriptor. */

  bkpc_control_close(&usb->lease);
  if (usb->close_error)
    {
      return usb->close_error;
    }

  ret = bkcontrol_serial_close(&usb->serial);
  if (ret < 0)
    {
      usb->close_error = ret;
    }

  return ret;
}

int bkpc_usb_open(struct bkpc_usb_s *usb, struct bkcontrol_pair_s *pair,
                  const struct bkpc_source_s *source,
                  mbedtls_x509_crt *certificate, mbedtls_pk_context *key,
                  uint64_t (*now_ms)(void *), void *clock_context,
                  bkcontrol_execute_t execute, bkcontrol_config_t config,
                  void *context)
{
  struct bkprov_tls_transport_s transport;
  int ret;

  if (usb == NULL || pair == NULL || source == NULL ||
      source->snapshot == NULL || certificate == NULL || key == NULL ||
      now_ms == NULL || execute == NULL)
    {
      return -EINVAL;
    }

  if (usb->close_error)
    {
      return usb->close_error;
    }

  if (usb->serial.opened || usb->lease.open || pair->tls.initialized ||
      pair->session.open)
    {
      return -EBUSY;
    }

  ret = bkcontrol_serial_open(&usb->serial, &transport);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkpc_control_start(&usb->lease, pair, source, usb->serial.epoch,
                           certificate, key, now_ms, clock_context,
                           execute, config, context, &transport);
  if (ret < 0)
    {
      int closed = bkpc_usb_close(usb);
      return closed < 0 ? closed : ret;
    }

  return 0;
}

int bkpc_usb_step(struct bkpc_usb_s *usb)
{
  int ret;

  if (usb == NULL)
    {
      return -EINVAL;
    }

  if (usb->close_error)
    {
      return usb->close_error;
    }

  ret = bkpc_control_step(&usb->lease);
  if (ret < 0)
    {
      int closed = bkpc_usb_close(usb);
      return closed < 0 ? closed : ret;
    }

  return ret;
}
