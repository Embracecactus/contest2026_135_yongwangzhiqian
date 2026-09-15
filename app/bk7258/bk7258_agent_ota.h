/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_AGENT_OTA_H
#define __APP_BK7258_AGENT_OTA_H

#include <nuttx/config.h>
#include "bk7258_control_session.h"

#if defined(CONFIG_BK7258_OTA_MANAGER) && \
    defined(CONFIG_BK7258_OTA_SOURCE_HTTP) && \
    defined(CONFIG_BK7258_VOICE_OTA_PERSISTENCE)
#define BKAGENT_APP_OTA_ENABLED 1

/* Called only by the serialized product/control owner. Admission must first
 * quiesce Trigger and reject an active official voice/configuration operation.
 * The OTA manager alone owns image staging, verification and cancellation. */
bool bkagent_ota_busy(void);
void bkagent_ota_poll(void);
int bkagent_ota_control(void *context, enum bkcontrol_command_e command,
  const uint8_t *record, size_t size, struct bkcontrol_status_s *status);
#else
static inline bool bkagent_ota_busy(void) { return false; }
static inline void bkagent_ota_poll(void) { }
#endif

#endif
