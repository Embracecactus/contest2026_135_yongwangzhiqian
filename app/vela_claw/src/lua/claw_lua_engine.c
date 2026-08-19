/****************************************************************************
 * app/vela_claw/src/lua/claw_lua_engine.c
 *
 * Lua execution engine. On target it embeds the NuttX Lua interpreter
 * (HAVE_LUA). On host (no interpreter linked) it runs a deterministic mock
 * engine that recognizes the hardware-control primitives used by the demo
 * scripts (led_blink) so the full agent loop is testable without Lua.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_log.h"
#include "claw_lua.h"
#include "claw_rtos.h"   /* for claw_now_ms, not strictly required here */

#ifdef HAVE_LUA
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#endif

claw_err_t claw_lua_init(void) {
  CLAW_LOGI("lua engine init (HAVE_LUA=%d)",
#ifdef HAVE_LUA
            1
#else
            0
#endif
  );
  return CLAW_OK;
}

void claw_lua_deinit(void) {}

static void mock_engine(const char *code, char *out, size_t outlen) {
  /* Recognize a simple call: led_blink(<n>) */
  const char *p = strstr(code, "led_blink");
  if (p) {
    const char *par = strchr(p, '(');
    const char *end = par ? strchr(par, ')') : NULL;
    int n = 1;
    if (par && end) {
      char num[32];
      size_t k = 0;
      const char *q = par + 1;
      while (q < end && *q >= '0' && *q <= '9' && k < sizeof(num) - 1)
        num[k++] = *q++;
      num[k] = '\0';
      if (k) n = atoi(num);
    }
    snprintf(out, outlen,
             "[lua:mock] executed led_blink(%d): toggled LED GPIO 3 times", n);
    return;
  }
  snprintf(out, outlen, "[lua:mock] executed script (%zu bytes, no HW action)",
           strlen(code));
}

#ifdef HAVE_LUA
claw_err_t claw_lua_run_script(const char *code_or_path, bool is_path,
                               const char *args_json, char *out, size_t outlen)
{
  lua_State *L = luaL_newstate();
  if (!L) { snprintf(out, outlen, "[lua: cannot create state]"); return CLAW_ENOMEM; }
  luaL_openlibs(L);
  lua_dostring(L, "function led_blink(n) print('led_blink '..tostring(n)) end");
  int rc;
  if (is_path)
    rc = luaL_dofile(L, code_or_path);
  else
    rc = luaL_dostring(L, code_or_path);
  if (rc != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    snprintf(out, outlen, "[lua error: %s]", err ? err : "?");
    lua_close(L);
    return CLAW_EIO;
  }
  snprintf(out, outlen, "[lua: ok]");
  lua_close(L);
  return CLAW_OK;
}
#else
claw_err_t claw_lua_run_script(const char *code_or_path, bool is_path,
                               const char *args_json, char *out, size_t outlen)
{
  (void)args_json;
  if (is_path) {
    FILE *f = fopen(code_or_path, "rb");
    if (!f) { snprintf(out, outlen, "[lua: cannot open %s]", code_or_path); return CLAW_ENOENT; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return CLAW_ENOMEM; }
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);
    mock_engine(buf, out, outlen);
    free(buf);
    return CLAW_OK;
  }
  mock_engine(code_or_path, out, outlen);
  return CLAW_OK;
}
#endif
