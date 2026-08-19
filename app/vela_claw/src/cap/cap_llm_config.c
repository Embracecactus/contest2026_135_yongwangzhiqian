/****************************************************************************
 * app/vela_claw/src/cap/cap_llm_config.c
 *
 * llm_config capability: inspect / report the current LLM configuration.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_cap.h"
#include "claw_config.h"

static claw_err_t run(const char *args_json, char *out, size_t outlen)
{
  (void)args_json;
  snprintf(out, outlen,
           "backend=%s model=%s base_url=%s auth=%s timeout_ms=%d "
           "max_tokens=%d tools=%d",
           claw_backend_name(g_claw_config.llm_backend),
           g_claw_config.llm_model, g_claw_config.llm_base_url,
           g_claw_config.llm_auth == CLAW_AUTH_BEARER ? "bearer" :
             (g_claw_config.llm_auth == CLAW_AUTH_APIKEY ? "apikey" : "none"),
           g_claw_config.llm_timeout_ms, g_claw_config.llm_max_tokens,
           g_claw_config.llm_supports_tools ? 1 : 0);
  return CLAW_OK;
}

claw_cap_t cap_llm_config = {
  "llm_config",
  "Inspect the current LLM backend, model and endpoint configuration.",
  run
};
