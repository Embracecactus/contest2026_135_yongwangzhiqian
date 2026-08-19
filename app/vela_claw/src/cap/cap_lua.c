/****************************************************************************
 * app/vela_claw/src/cap/cap_lua.c
 *
 * lua capability: run a Lua snippet or file. This is the "chat as creation"
 * execution layer — the LLM emits a tool call whose arguments carry Lua
 * source that drives real hardware via the registered drivers.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_cap.h"
#include "claw_lua.h"

static claw_err_t run(const char *args_json, char *out, size_t outlen)
{
  /* Minimal arg parse: {"code":"...", "path":"..."} */
  const char *code = NULL;
  const char *path = NULL;
  if (args_json) {
    code = strstr(args_json, "\"code\"");
    path = strstr(args_json, "\"path\"");
  }

  char snippet[1024];
  bool is_path = false;
  const char *src;

  if (path) {
    /* extract the string after "path":" */
    const char *q = strchr(path + 6, '"');
    if (!q) { snprintf(out, outlen, "[bad path arg]"); return CLAW_EINVAL; }
    const char *e = strchr(q + 1, '"');
    if (!e) { snprintf(out, outlen, "[bad path arg]"); return CLAW_EINVAL; }
    size_t n = (size_t)(e - (q + 1));
    if (n >= sizeof(snippet)) n = sizeof(snippet) - 1;
    memcpy(snippet, q + 1, n);
    snippet[n] = '\0';
    is_path = true;
    src = snippet;
  } else if (code) {
    const char *q = strchr(code + 6, '"');
    if (!q) { snprintf(out, outlen, "[bad code arg]"); return CLAW_EINVAL; }
    const char *e = strchr(q + 1, '"');
    if (!e) { snprintf(out, outlen, "[bad code arg]"); return CLAW_EINVAL; }
    size_t n = (size_t)(e - (q + 1));
    if (n >= sizeof(snippet)) n = sizeof(snippet) - 1;
    memcpy(snippet, q + 1, n);
    snippet[n] = '\0';
    is_path = false;
    src = snippet;
  } else {
    snprintf(out, outlen, "[no code/path provided]");
    return CLAW_EINVAL;
  }

  return claw_lua_run_script(src, is_path, "{}", out, outlen);
}

claw_cap_t cap_lua = {
  "lua_run_script",
  "Run a Lua script (code or path). Used by the agent to control hardware.",
  run
};
