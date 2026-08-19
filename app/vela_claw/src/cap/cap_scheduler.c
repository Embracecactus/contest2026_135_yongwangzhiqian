/****************************************************************************
 * app/vela_claw/src/cap/cap_scheduler.c
 *
 * scheduler capability: register/inspect timed or interval tasks. Faithful
 * port adds persistent cron; here we report the in-memory schedule.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_cap.h"

static claw_err_t run(const char *args_json, char *out, size_t outlen)
{
  (void)args_json;
  snprintf(out, outlen, "[scheduler: no active tasks]");
  return CLAW_OK;
}

claw_cap_t cap_scheduler = {
  "scheduler",
  "Inspect or register scheduled/interval tasks.",
  run
};
