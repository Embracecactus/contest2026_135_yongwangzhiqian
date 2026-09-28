/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BK7258_AGENT_POWER_H
#define BK7258_AGENT_POWER_H
#include <stdint.h>
/* Read-only snapshot. Query success does not mean power transition success.
 * Low byte: running=0, preparing=1, CP pending=2, failed=3.
 * Bit 8: a submitted CP request remains unresolved. No retry is implied.
 * Before first publication returns -EAGAIN; unsupported builds -ENOTSUP.
 */
int bk7258_agent_power_status(void *context, uint32_t *state, int32_t *error);
#endif
