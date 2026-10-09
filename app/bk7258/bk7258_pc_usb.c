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
#include <stdlib.h>
#include <string.h>
#include <mbedtls/platform_util.h>

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

int bkpc_usb_owner_stop(struct bkpc_usb_owner_s *owner)
{
  int ret;

  if (owner == NULL)
    {
      return -EINVAL;
    }

  ret = bkpc_usb_close(&owner->usb);
  free(owner->pair);
  owner->pair = NULL;
  owner->result = ret;
  return ret;
}

int bkpc_usb_owner_step(struct bkpc_usb_owner_s *owner,
                        const struct bkpc_usb_config_s *config,
                        bool admitted, bool start_allowed)
{
  struct bkprov_pc_snapshot_s view;
  uint64_t binding = 0;
  uint64_t now;
  int ret;

  if (owner == NULL)
    {
      return -EINVAL;
    }

  if (!admitted)
    {
      return bkpc_usb_owner_stop(owner);
    }

  if (config == NULL || config->source == NULL ||
      config->source->snapshot == NULL || config->certificate == NULL ||
      config->key == NULL || config->now_ms == NULL ||
      config->execute == NULL)
    {
      ret = bkpc_usb_owner_stop(owner);
      return ret < 0 ? ret : -EINVAL;
    }

  now = config->now_ms(config->clock_context);
  if (now < owner->last)
    {
      ret = bkpc_usb_owner_stop(owner);
      return ret < 0 ? ret : -ETIMEDOUT;
    }

  owner->last = now;
  if (owner->usb.close_error)
    {
      return owner->usb.close_error;
    }

  if (owner->pair)
    {
      ret = bkpc_usb_step(&owner->usb);
      if (ret >= 0)
        {
          owner->result = 0;
          return ret;
        }

      goto failed;
    }

  if (!start_allowed || now < owner->retry_at)
    {
      return owner->result;
    }

  memset(&view, 0, sizeof(view));
  ret = config->source->snapshot(config->source->context, &binding, &view);
  if (ret > 0)
    {
      ret = -EIO;
    }
  else if (ret == 0 && (binding == 0 || view.revision == 0 ||
                       view.capabilities == 0))
    {
      ret = -EACCES;
    }

  mbedtls_platform_zeroize(&view, sizeof(view));
  if (ret < 0)
    {
      goto failed;
    }

  owner->pair = calloc(1, sizeof(*owner->pair));
  if (owner->pair == NULL)
    {
      ret = -ENOMEM;
      goto failed;
    }

  ret = bkpc_usb_open(&owner->usb, owner->pair, config->source,
                      config->certificate, config->key, config->now_ms,
                      config->clock_context, config->execute, config->config,
                      config->context);
  if (ret == 0)
    {
      owner->result = 0;
      return 0;
    }

failed:
    {
      int closed = bkpc_usb_owner_stop(owner);
      if (closed < 0)
        {
          ret = closed;
        }
    }

  /* At most one failed open/allocation per second; protocol deadlines never
   * grow. The caller continues local work and never replays a command.
   */

  owner->retry_at = now > UINT64_MAX - 1000 ? UINT64_MAX : now + 1000;
  owner->result = ret;
  return ret;
}
