/****************************************************************************
 * app/vela_claw/src/vela_claw_init.c
 *
 * Application wiring. Brings up the full stack and connects the pieces:
 *   transports  -> capability registry -> core (executor)
 *   core        -> event router (agent runner) -> cli/ui sender (output)
 *
 * The core, router, capabilities and transports are RTOS-portable and need
 * no hardware. The screen UI (when enabled) registers its own sender and
 * input handler on top of the same router.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdio.h>

#include "claw_common.h"
#include "claw_config.h"
#include "claw_storage.h"
#include "claw_memory.h"
#include "claw_transport.h"
#include "claw_cap.h"
#include "claw_lua.h"
#include "claw_core.h"
#include "claw_event_router.h"
#include "claw_cli.h"
#include "claw_log.h"
#include "vela_claw_app.h"

/* Sender for the serial CLI platform: print the agent answer to the console
 * and re-show the prompt. Runs on the agent worker thread. */
static claw_err_t cli_sender(const char *text, const char *session)
{
  (void)session;
  if (text) printf("\n%s\n%s", text, g_claw_config.cli_prompt);
  fflush(stdout);
  return CLAW_OK;
}

claw_err_t vela_claw_app_init(const char *kv_dir)
{
  claw_config_load(kv_dir ? kv_dir : "/data/vela_claw");
  claw_memory_init(g_claw_config.data_dir);

  claw_transport_mock_register();
#ifdef HAVE_LIBCURL
  claw_transport_curl_register();
#endif

  claw_cap_init();
  claw_lua_init();

  claw_event_router_init(NULL);
  claw_event_router_set_agent_runner(claw_core_submit);
  claw_event_router_set_sender("cli", cli_sender);

  claw_core_init();
  claw_core_set_tool_executor(claw_cap_call);

#ifdef CONFIG_VELA_CLAW_UI
  vela_claw_ui_init();
#endif

  CLAW_LOGI("Vela-Claw %s initialized (transport=%s backend)",
            VELA_CLAW_VERSION, g_claw_config.transport);
  return CLAW_OK;
}

void vela_claw_app_deinit(void)
{
#ifdef CONFIG_VELA_CLAW_UI
  vela_claw_ui_deinit();
#endif
  claw_core_deinit();
  claw_event_router_deinit();
  claw_lua_deinit();
  claw_cap_deinit();
  claw_memory_deinit();
  claw_kv_close();
}
