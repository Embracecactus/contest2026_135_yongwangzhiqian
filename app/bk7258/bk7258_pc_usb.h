/****************************************************************************
 * app/bk7258/bk7258_pc_usb.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_PC_USB_H
#define __APP_BK7258_PC_USB_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_control_serial.h"
#include "bk7258_pc_control.h"

/* One serialized caller owns this connection. The pair and identity outlive
 * close. No automatic retry, USB mode change, Shell or DTR control. A close
 * failure is latched: unknown descriptor ownership cannot authorize reopen.
 * Successful fd close is not a USB controller/DMA shutdown acknowledgement.
 */

struct bkpc_usb_s
{
  struct bkcontrol_serial_s serial;
  struct bkpc_control_s lease;
  int close_error;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bkpc_usb_open(struct bkpc_usb_s *usb, struct bkcontrol_pair_s *pair,
                  const struct bkpc_source_s *source,
                  mbedtls_x509_crt *certificate, mbedtls_pk_context *key,
                  uint64_t (*now_ms)(void *), void *clock_context,
                  bkcontrol_execute_t execute, bkcontrol_config_t config,
                  void *context);
int bkpc_usb_step(struct bkpc_usb_s *usb);
int bkpc_usb_close(struct bkpc_usb_s *usb);
#endif
