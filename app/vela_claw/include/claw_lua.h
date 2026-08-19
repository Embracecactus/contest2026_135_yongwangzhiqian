/****************************************************************************
 * app/vela_claw/include/claw_lua.h
 *
 * Lua execution engine. On target it embeds the NuttX Lua 5.x interpreter;
 * on host (no interpreter linked) it falls back to a mock engine so the full
 * agent loop is still exercisable in unit tests. Drivers register native
 * functions (e.g. gpio control) into the Lua state.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_LUA_H
#define VELA_CLAW_LUA_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

claw_err_t claw_lua_init(void);
void       claw_lua_deinit(void);

/* Run Lua source, or a file if is_path is true. args_json is passed to the
 * script as a global `args` table. Output (printed text) goes to out. */
claw_err_t claw_lua_run_script(const char *code_or_path, bool is_path,
                               const char *args_json,
                               char *out, size_t outlen);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_LUA_H */
