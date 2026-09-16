/****************************************************************************
 * app/bk7258/bk7258_voice_button.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-owned, polled GPIO PTT event source.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_BUTTON_H
#define __APP_BK7258_BK7258_VOICE_BUTTON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_BK7258_PRODUCT_KEYS
#  define BKVOICE_BUTTON_ENDPOINT           "bkvoice-keys-v1"
#  define BKVOICE_BUTTON_MAGIC              0x4b455931u /* KEY1 */
#  define BKVOICE_BUTTON_VERSION            2u
#else
#  define BKVOICE_BUTTON_ENDPOINT           "bkvoice-ptt-v1"
#  define BKVOICE_BUTTON_MAGIC              0x50545431u /* PTT1 */
#  define BKVOICE_BUTTON_VERSION            1u
#endif
#define BKVOICE_BUTTON_HEARTBEAT_MS         100u
#define BKVOICE_BUTTON_LEASE_MS             500u

/* This is deliberately a fixed 24-byte wire record. */

struct bkvoice_button_event_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t sequence;
  /* v1: boolean PTT level. v2: product key mask; reserved stays zero. */
  uint32_t pressed;
  uint32_t reserved[2];
};

_Static_assert(sizeof(struct bkvoice_button_event_s) == 24,
               "PTT event wire size");

int bkvoice_button_start(const char *devpath, bool active_low);
int bkvoice_button_stop(void);
bool bkvoice_button_running(void);

#endif /* __APP_BK7258_BK7258_VOICE_BUTTON_H */
