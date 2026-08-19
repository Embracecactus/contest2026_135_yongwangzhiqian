/****************************************************************************
 * app/vela_claw/src/cap/cap_mgrs.c
 *
 * Session / skill / agent / router managers. These mirror esp-claw's
 * claw_session_mgr / claw_skill_mgr / claw_agent_mgr / claw_router_mgr.
 * Initially they expose inspection; full lifecycle management is a later
 * phase of the faithful port.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_cap.h"
#include "claw_memory.h"
#include "claw_cap.h"

/* session_mgr ---------------------------------------------------------- */
static claw_err_t session_run(const char *args_json, char *out, size_t outlen)
{
  (void)args_json;
  claw_memory_get_session_history("default", out, outlen);
  if (out[0] == '\0') snprintf(out, outlen, "[no session history]");
  return CLAW_OK;
}
claw_cap_t cap_session_mgr = {
  "session_mgr",
  "Inspect the current conversation session history.",
  session_run
};

/* skill_mgr ------------------------------------------------------------ */
static claw_err_t skill_run(const char *args_json, char *out, size_t outlen)
{
  (void)args_json;
  snprintf(out, outlen, "[skills: none registered yet]");
  return CLAW_OK;
}
claw_cap_t cap_skill_mgr = {
  "skill_mgr",
  "List available skills.",
  skill_run
};

/* agent_mgr ------------------------------------------------------------ */
static claw_err_t agent_run(const char *args_json, char *out, size_t outlen)
{
  (void)args_json;
  snprintf(out, outlen, "agent=root | subagents=0");
  return CLAW_OK;
}
claw_cap_t cap_agent_mgr = {
  "agent_mgr",
  "Report agent hierarchy status.",
  agent_run
};

/* router_mgr ----------------------------------------------------------- */
static claw_err_t router_run(const char *args_json, char *out, size_t outlen)
{
  (void)args_json;
  snprintf(out, outlen, "router=rules(default) | actions=run_agent,run_script,call_cap,send_message,drop");
  return CLAW_OK;
}
claw_cap_t cap_router_mgr = {
  "router_mgr",
  "Report the active event-router rules and actions.",
  router_run
};
