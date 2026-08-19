/****************************************************************************
 * app/vela_claw/src/vela_claw_main.c
 *
 * NSH entry point. Builtin entry is aliased to vela_claw_main by the build
 * system (main -> <PROGNAME>_main). Usage:  vela_claw [kv_dir]
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdio.h>

#include "claw_common.h"
#include "claw_config.h"
#include "claw_cli.h"
#include "claw_event_router.h"
#include "vela_claw_app.h"

int main(int argc, char *argv[])
{
  const char *kv_dir = (argc > 1) ? argv[1] : "/data/vela_claw";

  vela_claw_app_init(kv_dir);

  /* Both the serial CLI and the screen UI submit through the same router. */
  claw_cli_set_event_sink(claw_event_router_handle);

  claw_cli_start();

  vela_claw_app_deinit();
  return 0;
}
