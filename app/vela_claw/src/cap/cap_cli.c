/****************************************************************************
 * app/vela_claw/src/cap/cap_cli.c
 *
 * cli capability: echo helper / report CLI status. Lets the LLM surface
 * information back through the serial console.
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
  snprintf(out, outlen, "cli enabled=%d prompt=\"%s\"",
           g_claw_config.cli_enabled ? 1 : 0, g_claw_config.cli_prompt);
  (void)args_json;
  return CLAW_OK;
}

claw_cap_t cap_cli = {
  "cli_status",
  "Report the serial CLI status and prompt.",
  run
};
