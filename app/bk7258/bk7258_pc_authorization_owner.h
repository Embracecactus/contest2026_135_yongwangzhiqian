/****************************************************************************
 * app/bk7258/bk7258_pc_authorization_owner.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APP_BK7258_PC_AUTHORIZATION_OWNER_H
#define __APP_BK7258_PC_AUTHORIZATION_OWNER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_pc_authorization.h"

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* All calls belong to the serialized product owner, not the file worker.
 * Preparation validates the protected bundle against the bound phone owner
 * before copying its PC storage binding, independent of cloud readiness.
 * Queries do not prepare, decode, mount or perform file I/O.
 */

int bkpc_authorization_prepare(uint64_t revision, const void *bundle,
                               size_t size);
int bkpc_authorization_current(enum bkcontrol_command_e command,
                               uint32_t offset, const uint8_t *record,
                               size_t size,
                               struct bkcontrol_status_s *status);
void bkpc_authorization_unbind(void);
#endif
