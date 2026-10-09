/****************************************************************************
 * app/bk7258/bk7258_pc_authorization.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_PC_AUTHORIZATION_H
#define __APP_BK7258_PC_AUTHORIZATION_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_control_session.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Authenticated phone-only adapter. Caller supplies the validated current
 * main configuration revision and must stop PC admission/session ownership
 * before APPLY. The phone product caller is bound; no USB runtime owner
 * exists yet. Any USB consumer must join that close-before-mutation gate.
 * PCW1 is 88 bytes; PCS1 public snapshot is 64 bytes; PCR1 receipt is 32.
 * APPLY -EAGAIN requires transaction query, never a successful grant claim.
 * CANCEL only discards Session staging; it cannot cancel worker durability.
 */

int bkpc_authorization_control(uint64_t revision,
                               enum bkcontrol_command_e command,
                               uint32_t offset, const uint8_t *record,
                               size_t size,
                               struct bkcontrol_status_s *status);
#endif
