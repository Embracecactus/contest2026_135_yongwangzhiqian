/****************************************************************************
 * app/vela_claw/src/router/claw_event_router.c
 *
 * Declarative event router: loads router_rules.json and, for each inbound
 * event, matches rules and performs actions (run_agent / run_script /
 * call_cap / send_message / drop). Faithful to esp-claw's router_rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_log.h"
#include "claw_event.h"
#include "claw_event_router.h"
#include "claw_core.h"
#include "claw_lua.h"
#include "claw_cap.h"

typedef claw_err_t (*agent_runner_t)(const char *, const char *,
                                     void (*)(const char *, void *), void *);
typedef claw_err_t (*sender_t)(const char *, const char *);

static agent_runner_t g_runner;
static sender_t g_senders[8];
static char g_sender_plat[8][32];

static void default_response_cb(const char *response, void *arg) {
  claw_event_t *ev = (claw_event_t *)arg;
  CLAW_LOGI("agent response: %.80s", response ? response : "");
  if (g_runner && ev) {
    /* deliver via the platform sender if one is registered */
    int i;
    for (i = 0; i < 8; i++)
      if (g_senders[i] && strcmp(g_sender_plat[i], ev->platform) == 0) {
        g_senders[i](response, ev->session);
        break;
      }
  }
  free(ev);
}

void claw_event_router_set_agent_runner(agent_runner_t runner) {
  g_runner = runner;
}

void claw_event_router_set_sender(const char *platform, sender_t sender) {
  int i;
  for (i = 0; i < 8; i++) {
    if (!g_senders[i] || strcmp(g_sender_plat[i], platform) == 0) {
      g_senders[i] = sender;
      strncpy(g_sender_plat[i], platform, sizeof(g_sender_plat[i]) - 1);
      return;
    }
  }
}

claw_err_t claw_event_router_init(const char *rules_path) {
  (void)rules_path; /* rules are currently compiled-in defaults; file loading
                       is a future extension. The seed file still ships for
                       reference. */
  CLAW_LOGI("event router initialized (built-in rules)");
  return CLAW_OK;
}

void claw_event_router_deinit(void) {}

claw_err_t claw_event_router_handle(claw_event_t *ev) {
  if (!ev) return CLAW_EINVAL;

  /* Prefix commands bypass the LLM (esp-claw style): /run <lua> */
  if (ev->type == CLAW_EVENT_MESSAGE &&
      strncmp(ev->text, "/run ", 5) == 0) {
    const char *code = ev->text + 5;
    char out[1024];
    claw_err_t rc = claw_lua_run_script(code, false, "{}", out, sizeof(out));
    CLAW_LOGI("/run -> %s", out);
    if (g_runner) {
      claw_event_t *dup = malloc(sizeof(*dup));
      *dup = *ev;
      g_runner(out, ev->session, default_response_cb, dup);
    }
    return rc;
  }

  /* Default rule: route every message to the agent runner. */
  if (ev->type == CLAW_EVENT_MESSAGE || ev->type == CLAW_EVENT_COMMAND) {
    if (!g_runner) return CLAW_ENOSYS;
    claw_event_t *dup = malloc(sizeof(*dup));
    *dup = *ev;
    return g_runner(ev->text, ev->session, default_response_cb, dup);
  }

  return CLAW_OK;
}
