/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_AGENT_MEMORY_H
#define __APP_BK7258_AGENT_MEMORY_H
#include <stdint.h>
#include "bk7258_control_session.h"

/* Adapts the encrypted persistent snapshot only; the official Session still
 * owns conversations and messages. bind borrows the committed control
 * identity and never reads or changes the device signing identity. restore
 * runs before the first Trigger admission; commit runs after the official
 * reply completes.
 */
int bkagent_memory_bind(const uint8_t owner[32]);
/* Factory reset coordination only: caller has already made voice/Agent idle.
 * This prevents future memory writes, clears the runtime voice projection and
 * removes only the legacy SD ciphertext replicas. CP policy/snapshot records
 * remain owned by the provisioning storage reset worker. A later bind merely
 * loads a newly committed policy; it does not re-enable memory by itself.
 */
int bkagent_memory_reset(void);
int bkagent_memory_restore(unsigned int persona);
int bkagent_memory_commit(unsigned int persona, const char *reply);
int bkagent_memory_control(enum bkcontrol_command_e command, uint32_t value,
                           unsigned int persona);
uint32_t bkagent_memory_flags(void);
#endif
