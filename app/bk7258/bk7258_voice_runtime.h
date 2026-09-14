/****************************************************************************
 * app/bk7258/bk7258_voice_runtime.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_RUNTIME_H
#define __APP_BK7258_BK7258_VOICE_RUNTIME_H

#include "bk7258_voice_protocol.h"
#include "bk7258_voice_ptt.h"
#include "bk7258_control_session.h"
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>

/* Commands, step and status belong to the existing AP voice service worker.
 * The receiver and RPMsg callbacks only publish bounded events to that owner.
 */

int bkvoice_runtime_initialize(struct bkvoice_ptt_s *ptt,
                               uint32_t boot_generation, sem_t *wake);
int bkvoice_runtime_uninitialize(void);
int bkvoice_runtime_command(const struct bkvoice_rpc_request_s *request,
                            struct bkvoice_rpc_response_s *response);
void bkvoice_runtime_step(bool command_link);
void bkvoice_runtime_status(struct bkvoice_rpc_response_s *response);
bool bkvoice_runtime_busy(void);
bool bkvoice_runtime_settings_busy(void);
/* SDC1 execute callback for the same AP service worker. Transport must
 * authenticate the committed owner key before dispatch. context is unused.
 */
int bkvoice_runtime_control(void *context, enum bkcontrol_command_e command,
                            uint32_t value, struct bkcontrol_status_s *status);
int bkvoice_runtime_control_config(void *context, enum bkcontrol_command_e command,
                                   uint32_t kind, uint32_t offset,
                                   const uint8_t *record, size_t size,
                                   struct bkcontrol_status_s *status);
/* Optional SDC1 OTA callback. START copies the borrowed transport record
 * before accepting it; STATUS and CANCEL receive NULL/zero. Wire state is
 * idle=0, queued=1, active=2, terminal=3. Phase is a
 * bkvoice_companion_ota_phase_e value; progress/total are percentage/100
 * when known and UINT32_MAX otherwise. The OTA worker cannot begin image
 * reads until the owner has persisted the authenticated manifest target.
 */
int bkvoice_runtime_control_ota(void *context,
                               enum bkcontrol_command_e command,
                               const uint8_t *record, size_t size,
                               struct bkcontrol_status_s *status);

#endif
