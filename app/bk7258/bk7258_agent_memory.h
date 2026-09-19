/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_AGENT_MEMORY_H
#define __APP_BK7258_AGENT_MEMORY_H
#include <stdint.h>
#include "bk7258_control_session.h"

/* 仅适配加密持久快照；会话和消息仍由官方 Session 管理。
 * bind 借用已提交的控制身份，不读取、改变设备签名身份。
 * restore 在首次 Trigger 准入前执行；commit 在官方完成回答后执行。
 */
int bkagent_memory_bind(const uint8_t owner[32]);
int bkagent_memory_restore(unsigned int persona);
int bkagent_memory_commit(unsigned int persona, const char *reply);
int bkagent_memory_control(enum bkcontrol_command_e command, uint32_t value,
                           unsigned int persona);
uint32_t bkagent_memory_flags(void);
#endif
