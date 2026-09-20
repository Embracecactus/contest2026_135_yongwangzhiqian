/****************************************************************************
 * app/bk7258/bk7258_agent_trigger.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cross-module contract for the Media Trigger model backend. The product
 * coordinator (bk7258_agent_product.c) drives the wake lifecycle through
 * exactly these entry points; the implementation lives in
 * bk7258_agent_trigger.c and serializes load/detect/unload on its own
 * worker. No other module may call into the trigger.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_AGENT_TRIGGER_H
#define __APP_BK7258_BK7258_AGENT_TRIGGER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bk7258_control_session.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Wake admission and lifecycle; safe from the product config task. */
int bk7258_agent_trigger_prepare(void);
int bk7258_agent_trigger_start(void);
int bk7258_agent_trigger_stop(void);
int bk7258_agent_trigger_process(void);

/* Media Trigger model transaction state. */
bool bk7258_agent_trigger_model_pending(void);
int bk7258_agent_trigger_model_step(bool arm);

/* Wake re-arming and admission state. */
int bk7258_agent_trigger_rearm(void);
bool bk7258_agent_trigger_armed(void);

/* Persisted wake threshold bridge (percent 50..90). */
unsigned int bk7258_agent_trigger_threshold_get(void);
int bk7258_agent_trigger_threshold_set(unsigned int percent);

/* Control-channel callback forwarded from the provisioning owner; the
 * trigger accepts only its own configuration kinds and reports
 * -ENOTSUP for everything else.
 */
int bk7258_agent_trigger_control(void *context,
                                 enum bkcontrol_command_e command,
                                 uint32_t kind, uint32_t offset,
                                 const uint8_t *record, size_t size,
                                 struct bkcontrol_status_s *status);

#endif /* __APP_BK7258_BK7258_AGENT_TRIGGER_H */
