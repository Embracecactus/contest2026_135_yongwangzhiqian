/****************************************************************************
 * app/vela_claw/include/claw_cap.h
 *
 * Capability (tool) registry. Each capability is a callable unit the LLM can
 * invoke as a tool_call, and that can also be triggered directly via the CLI
 * or a router rule. Mirrors esp-claw's claw_cap system.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_CAP_H
#define VELA_CLAW_CAP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct claw_cap_s {
  const char *name;        /* tool name, e.g. "lua_run_script" */
  const char *description; /* shown to the LLM as the tool description */
  claw_err_t (*run)(const char *args_json, char *out, size_t outlen);
} claw_cap_t;

claw_err_t claw_cap_register(const claw_cap_t *cap);
claw_err_t claw_cap_call(const char *name, const char *args_json,
                         char *out, size_t outlen);
claw_err_t claw_cap_list(char *buf, size_t buflen);

/* Build the inner JSON of an OpenAI-style "tools" array (no surrounding
 * brackets): one {"type":"function","function":{...}} object per registered
 * capability. The LLM client includes this so the model can call tools by
 * name. Returns CLAW_OK. */
claw_err_t claw_cap_tools_json(char *buf, size_t buflen);

/* Built-in capabilities are registered by this init. */
claw_err_t claw_cap_init(void);
void       claw_cap_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_CAP_H */
