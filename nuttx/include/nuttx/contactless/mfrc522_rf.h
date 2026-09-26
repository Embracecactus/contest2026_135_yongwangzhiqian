/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __INCLUDE_NUTTX_CONTACTLESS_MFRC522_RF_H
#define __INCLUDE_NUTTX_CONTACTLESS_MFRC522_RF_H
#include <nuttx/contactless/ioctl.h>

/* arg: 0 disables TX1/TX2, 1 enables. Other values fail without I/O.
 * Success verifies control-register readback, not physical field strength.
 * The single reader owner must serialize field control and card operations.
 */
#define MFRC522IOC_SET_RF _CLIOC(0x000f)
#endif
