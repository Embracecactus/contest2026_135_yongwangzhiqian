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
int bkagent_memory_restore(unsigned int persona);
int bkagent_memory_commit(unsigned int persona, const char *reply);
int bkagent_memory_control(enum bkcontrol_command_e command, uint32_t value,
                           unsigned int persona);
uint32_t bkagent_memory_flags(void);
#endif
