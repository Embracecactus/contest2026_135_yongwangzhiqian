/****************************************************************************
 * app/vela_claw/src/cap/cap_system.c
 *
 * system capability: report device / firmware identity (board, version).
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
           "vela-claw %s | board=t5_board | rtos=openvela/NuttX | "
           "transport=%s", VELA_CLAW_VERSION, g_claw_config.transport);
  return CLAW_OK;
}

claw_cap_t cap_system = {
  "system_info",
  "Report device identity, firmware version and active transport.",
  run
};
