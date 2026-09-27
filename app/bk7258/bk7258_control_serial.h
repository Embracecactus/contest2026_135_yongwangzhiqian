/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CONTROL_SERIAL_H
#define __APP_BK7258_CONTROL_SERIAL_H

#include "bk7258_provision_tls.h"

/* Zero initialize once. One serialized product owner exclusively owns the fd
 * and this object, including transport callbacks. Never reset epoch on close.
 * No application authentication, USB mode change, Shell or automatic reopen.
 */
struct bkcontrol_serial_s
{
  int fd;
  uint32_t epoch;
  bool opened;
  bool live;
  int close_error;
};

/* Open only the native CDC node, nonblocking and raw. No driver/USB startup.
 * Failure leaves the output descriptor untouched. Successful open creates a
 * fresh epoch; callers must close the old TLS before reopening. Epoch exhaustion
 * fails closed. Setup rollback uses the same close contract; a close error
 * is retained and forbids reopen. Linux PTY is not target DCD/IRQ proof.
 */
int bkcontrol_serial_open(struct bkcontrol_serial_s *serial,
                          struct bkprov_tls_transport_s *transport);
/* Invalidates callbacks before closing exactly once. O_NONBLOCK avoids the
 * NuttX serial close drain. Close does not acknowledge remote cancellation or
 * hardware DMA exit. The owner must separately close/wipe its control pair.
 * Failed close is never retried: the fd may already have been reused by the
 * kernel. Its error stays latched, including on repeated close/open calls.
 */
int bkcontrol_serial_close(struct bkcontrol_serial_s *serial);
#endif
