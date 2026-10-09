/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __NUTTX_CONTACTLESS_MFRC522_FRAME_H
#define __NUTTX_CONTACTLESS_MFRC522_FRAME_H
#include <stdint.h>
#include <nuttx/contactless/ioctl.h>
#include <nuttx/contactless/mfrc522_rf.h>
#ifndef MFRC522IOC_EXCHANGE
#define MFRC522IOC_EXCHANGE _CLIOC(0x000e)
#endif

/* Selected-card, byte-aligned CRC_A frames. Caller serializes access to the
 * selected card. tx_length is 1..62, excluding CRC; driver appends/checks CRC.
 * rx_length is output only, excluding CRC. Every failure clears rx and length.
 * This is the frame transport, not an ISO-DEP session/APDU implementation;
 * timeout_ms = 0 keeps the hardware timeout; 1..300000 selects a software
 * wait, with 20 ms transmission allowance. Timer state is restored on return.
 */

struct mfrc522_exchange_s
{
  uint32_t timeout_ms;
  uint8_t tx_length;
  uint8_t rx_length;
  uint8_t tx[62];
  uint8_t rx[62];
};

#endif
