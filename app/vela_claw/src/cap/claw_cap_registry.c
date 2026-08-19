/****************************************************************************
 * app/vela_claw/src/cap/claw_cap_registry.c
 *
 * Capability (tool) registry. Built-in capabilities are registered here and
 * also available to the LLM as tool_calls and to the CLI / router rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_cap.h"

#define MAX_CAPS 64

static claw_cap_t g_caps[MAX_CAPS];
static int g_count;

claw_err_t claw_cap_register(const claw_cap_t *cap) {
  if (!cap || !cap->name || g_count >= MAX_CAPS) return CLAW_EINVAL;
  int i;
  for (i = 0; i < g_count; i++)
    if (strcmp(g_caps[i].name, cap->name) == 0) {
      g_caps[i] = *cap;
      return CLAW_OK;
    }
  g_caps[g_count++] = *cap;
  return CLAW_OK;
}

claw_err_t claw_cap_call(const char *name, const char *args_json,
                         char *out, size_t outlen)
{
  if (!name) return CLAW_EINVAL;
  int i;
  for (i = 0; i < g_count; i++)
    if (strcmp(g_caps[i].name, name) == 0)
      return g_caps[i].run(args_json ? args_json : "{}", out, outlen);
  snprintf(out, outlen, "[unknown capability: %s]", name);
  return CLAW_EINVAL;
}

claw_err_t claw_cap_list(char *buf, size_t buflen) {
  int off = 0;
  int i;
  for (i = 0; i < g_count; i++) {
    off += snprintf(buf + off, buflen - off, "- %s: %s\n",
                     g_caps[i].name,
                     g_caps[i].description ? g_caps[i].description : "");
  }
  return CLAW_OK;
}

claw_err_t claw_cap_tools_json(char *buf, size_t buflen) {
  int off = 0;
  int i;
  for (i = 0; i < g_count; i++) {
    const char *d = g_caps[i].description ? g_caps[i].description : "";
    off += snprintf(buf + off, buflen - off,
      "%s{\"type\":\"function\",\"function\":{\"name\":\"%s\","
      "\"description\":\"%s\",\"parameters\":{\"type\":\"object\","
      "\"properties\":{}}}",
      (off ? "," : ""), g_caps[i].name, d);
  }
  return CLAW_OK;
}

/* ---- built-in capabilities ---- */

extern claw_cap_t cap_llm_config;
extern claw_cap_t cap_files;
extern claw_cap_t cap_lua;
extern claw_cap_t cap_cli;
extern claw_cap_t cap_system;
extern claw_cap_t cap_session_mgr;
extern claw_cap_t cap_skill_mgr;
extern claw_cap_t cap_agent_mgr;
extern claw_cap_t cap_router_mgr;
extern claw_cap_t cap_scheduler;
extern claw_cap_t cap_http_request;
extern claw_cap_t cap_web_search;

claw_err_t claw_cap_init(void) {
  /* order matters only for listing */
  claw_cap_register(&cap_llm_config);
  claw_cap_register(&cap_files);
  claw_cap_register(&cap_lua);
  claw_cap_register(&cap_cli);
  claw_cap_register(&cap_system);
  claw_cap_register(&cap_session_mgr);
  claw_cap_register(&cap_skill_mgr);
  claw_cap_register(&cap_agent_mgr);
  claw_cap_register(&cap_router_mgr);
  claw_cap_register(&cap_scheduler);
  claw_cap_register(&cap_http_request);
  claw_cap_register(&cap_web_search);
  return CLAW_OK;
}

void claw_cap_deinit(void) {
  g_count = 0;
}
