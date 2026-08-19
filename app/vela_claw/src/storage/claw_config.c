/****************************************************************************
 * app/vela_claw/src/storage/claw_config.c
 *
 * Device configuration load/save over the KV store. Mirrors esp-claw's
 * app_config schema (LLM backend/key/model/url, transport, CLI).
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "claw_common.h"
#include "claw_config.h"
#include "claw_storage.h"

claw_config_t g_claw_config;

const char *claw_backend_name(claw_llm_backend_t b) {
  switch (b) {
    case CLAW_BACKEND_OPENAI:   return "openai_compatible";
    case CLAW_BACKEND_ANTHROPIC: return "anthropic_compatible";
    case CLAW_BACKEND_CUSTOM:   return "custom";
    default: return "openai_compatible";
  }
}

claw_llm_backend_t claw_backend_from_name(const char *name) {
  if (!name) return CLAW_BACKEND_OPENAI;
  if (strcmp(name, "anthropic_compatible") == 0) return CLAW_BACKEND_ANTHROPIC;
  if (strcmp(name, "custom") == 0) return CLAW_BACKEND_CUSTOM;
  return CLAW_BACKEND_OPENAI;
}

void claw_config_default(claw_config_t *c) {
  memset(c, 0, sizeof(*c));
  c->llm_backend = CLAW_BACKEND_OPENAI;
  strncpy(c->llm_model, "gpt-4o-mini", sizeof(c->llm_model) - 1);
  strncpy(c->llm_base_url, "https://api.openai.com/v1",
          sizeof(c->llm_base_url) - 1);
  c->llm_auth = CLAW_AUTH_BEARER;
  c->llm_timeout_ms = 120000;
  c->llm_max_tokens = 8192;
  c->llm_supports_tools = true;
  strncpy(c->transport, "curl", sizeof(c->transport) - 1);
  c->cli_enabled = true;
  strncpy(c->cli_prompt, "vela-claw> ", sizeof(c->cli_prompt) - 1);
  strncpy(c->data_dir, "/data/vela_claw", sizeof(c->data_dir) - 1);
  strncpy(c->system_dir, "/system/vela_claw", sizeof(c->system_dir) - 1);
}

claw_err_t claw_config_load(const char *kv_dir) {
  claw_config_default(&g_claw_config);
  if (claw_kv_open(kv_dir) != CLAW_OK) return CLAW_EIO;

  char *v;
  if ((v = claw_kv_get("llm_backend"))) {
    g_claw_config.llm_backend = claw_backend_from_name(v);
    free(v);
  }
  if ((v = claw_kv_get("llm_api_key"))) {
    strncpy(g_claw_config.llm_api_key, v, sizeof(g_claw_config.llm_api_key) - 1);
    free(v);
  }
  if ((v = claw_kv_get("llm_model"))) {
    strncpy(g_claw_config.llm_model, v, sizeof(g_claw_config.llm_model) - 1);
    free(v);
  }
  if ((v = claw_kv_get("llm_base_url"))) {
    strncpy(g_claw_config.llm_base_url, v, sizeof(g_claw_config.llm_base_url) - 1);
    free(v);
  }
  if ((v = claw_kv_get("llm_auth"))) {
    if (strcmp(v, "apikey") == 0) g_claw_config.llm_auth = CLAW_AUTH_APIKEY;
    else if (strcmp(v, "none") == 0) g_claw_config.llm_auth = CLAW_AUTH_NONE;
    else g_claw_config.llm_auth = CLAW_AUTH_BEARER;
    free(v);
  }
  if ((v = claw_kv_get("transport"))) {
    strncpy(g_claw_config.transport, v, sizeof(g_claw_config.transport) - 1);
    free(v);
  }
  if ((v = claw_kv_get("data_dir"))) {
    strncpy(g_claw_config.data_dir, v, sizeof(g_claw_config.data_dir) - 1);
    free(v);
  }
  return CLAW_OK;
}

claw_err_t claw_config_save(const char *kv_dir) {
  (void)kv_dir;
  claw_kv_set("llm_backend", claw_backend_name(g_claw_config.llm_backend));
  claw_kv_set("llm_api_key", g_claw_config.llm_api_key);
  claw_kv_set("llm_model", g_claw_config.llm_model);
  claw_kv_set("llm_base_url", g_claw_config.llm_base_url);
  claw_kv_set("llm_auth",
    g_claw_config.llm_auth == CLAW_AUTH_APIKEY ? "apikey" :
    (g_claw_config.llm_auth == CLAW_AUTH_NONE ? "none" : "bearer"));
  claw_kv_set("transport", g_claw_config.transport);
  claw_kv_set("data_dir", g_claw_config.data_dir);
  return claw_kv_commit();
}
