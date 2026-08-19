/****************************************************************************
 * app/vela_claw/include/claw_core.h
 *
 * Agent core: request queue + worker thread running the perceive->decide->
 * execute loop (LLM call, tool-call iteration, capability/Lua execution,
 * async out_message). Context is assembled from registered providers.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_CORE_H
#define VELA_CLAW_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLAW_CORE_MAX_TOOL_ITER 32

typedef void (*claw_core_response_cb)(const char *response, void *arg);

/* Called by core to execute one tool call (name + JSON args -> out text). */
typedef claw_err_t (*claw_tool_executor_t)(const char *name,
                                           const char *args_json,
                                           char *out, size_t outlen);

claw_err_t claw_core_init(void);
void       claw_core_deinit(void);

/* Submit a prompt for the given session; cb is invoked with the final text. */
claw_err_t claw_core_submit(const char *prompt, const char *session,
                            claw_core_response_cb cb, void *arg);

/* Context providers contribute a text block to the system prompt. */
typedef claw_err_t (*claw_context_provider_t)(char *buf, size_t buflen);
claw_err_t claw_core_register_context_provider(const char *name,
                                               claw_context_provider_t fn);

/* The executor that runs a tool_call (normally wired to the capability sys). */
void claw_core_set_tool_executor(claw_tool_executor_t fn);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_CORE_H */
