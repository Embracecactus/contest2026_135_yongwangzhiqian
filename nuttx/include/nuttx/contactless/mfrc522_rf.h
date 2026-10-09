/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __INCLUDE_NUTTX_CONTACTLESS_MFRC522_RF_H
#define __INCLUDE_NUTTX_CONTACTLESS_MFRC522_RF_H
#include <stdint.h>
#include <nuttx/contactless/ioctl.h>

/* arg: 0 disables TX1/TX2, 1 enables. Other values fail without I/O.
 * Success verifies control-register readback, not physical field strength.
 * The single reader owner must serialize field control and card operations.
 */
#define MFRC522IOC_SET_RF _CLIOC(0x000f)
/* arg points to a freshly filled observation. Success with present=0 means
 * REQA reached the reader hardware timer with no reported RF error. It is not
 * physical proof that a card moved. Software watchdog, malformed ATQA, and
 * selection failures remain negative/unknown; all failures clear the output.
 * RF must already be enabled; this ioctl never turns on the field itself.
 */
#define MFRC522IOC_OBSERVE _CLIOC(0x0010)
struct mfrc522_observation_s
{
  uint32_t present;
  struct picc_uid_s uid;
};
#endif
