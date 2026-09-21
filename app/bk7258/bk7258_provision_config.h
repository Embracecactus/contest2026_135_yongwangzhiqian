/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_PROVISION_CONFIG_H
#define __APP_BK7258_PROVISION_CONFIG_H

#include "bk7258_control_session.h"

#define BKPROV_PATCH_HEADER 52u
#define BKPROV_PATCH_MAX 9216u
#define BKPROV_SETTINGS_PUBLIC_MAX 824u

/* SCP1: magic, operation[16], expected revision[8], flags[4], UTC[8],
 * four BE16 lengths (SSID, PSK, CCF1, CA DER), IPv4[4], then the fields.
 * Flags: Wi-Fi=1, cloud=2, replace API key=4, replace PSK=8,
 * explicitly clear cloud=16. An omitted secret means retain, not clear.
 * CCF1 may omit its key only when flag 4 is absent and the endpoint is
 * unchanged. Neither owner keys nor identity material are accepted here.
 * SCS1 is a public, frozen readback with operation outcome and revision.
 */
int bkprov_config_control(enum bkcontrol_command_e command, uint32_t offset,
                          const uint8_t *record, size_t size,
                          struct bkcontrol_status_s *status);
void bkprov_config_step(void);
bool bkprov_config_busy(void);

#endif
