/****************************************************************************
 * app/vela_claw/include/claw_memory.h
 *
 * Structured memory: per-session history (for context) + long-term key/value
 * (identity/soul/user). File-backed under the data directory.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_MEMORY_H
#define VELA_CLAW_MEMORY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

claw_err_t claw_memory_init(const char *data_dir);
void       claw_memory_deinit(void);

claw_err_t claw_memory_append_session(const char *session,
                                      const char *role, const char *text);
claw_err_t claw_memory_get_session_history(const char *session,
                                           char *buf, size_t buflen);

claw_err_t claw_memory_set_long_term(const char *key, const char *value);
claw_err_t claw_memory_get_long_term(const char *key,
                                     char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_MEMORY_H */
