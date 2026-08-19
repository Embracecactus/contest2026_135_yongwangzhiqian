/****************************************************************************
 * app/vela_claw/include/claw_config.h
 *
 * Device configuration schema (LLM backend, model, transport, CLI). Mirrors
 * esp-claw's app_config_t and is persisted through the KV store.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_CONFIG_H
#define VELA_CLAW_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  CLAW_BACKEND_OPENAI,
  CLAW_BACKEND_ANTHROPIC,
  CLAW_BACKEND_CUSTOM
} claw_llm_backend_t;

typedef enum {
  CLAW_AUTH_BEARER,
  CLAW_AUTH_APIKEY,
  CLAW_AUTH_NONE
} claw_auth_t;

typedef struct claw_config_s {
  claw_llm_backend_t llm_backend;
  char llm_api_key[321];
  char llm_model[129];
  char llm_base_url[257];
  claw_auth_t        llm_auth;
  int                llm_timeout_ms;
  int                llm_max_tokens;
  bool               llm_supports_tools;

  char transport[32];     /* "mock" | "curl" */

  bool cli_enabled;
  char cli_prompt[32];

  char data_dir[257];     /* memory / scripts working dir */
  char system_dir[257];   /* read-only recovery seeds */
} claw_config_t;

extern claw_config_t g_claw_config;

void          claw_config_default(claw_config_t *c);
claw_err_t    claw_config_load(const char *kv_dir);
claw_err_t    claw_config_save(const char *kv_dir);

/* Map backend enum <-> string (for config display). */
const char *claw_backend_name(claw_llm_backend_t b);
claw_llm_backend_t claw_backend_from_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_CONFIG_H */
