/****************************************************************************
 * app/vela_claw/src/cli/claw_cli.c
 *
 * Serial/stdio REPL input channel. Commands:
 *   ask <text>        send a prompt to the agent (default session)
 *   ask_once <text>   send a one-shot prompt (ephemeral session)
 *   auto <text>       alias of ask (autonomous mode)
 *   session <name>    switch the active session
 *   cap <name> [json] call a capability directly (bypasses the LLM)
 *   /run <lua>        run a Lua snippet directly
 *   help              show help
 *   exit|quit         leave the REPL
 *
 * Every natural-language line is normalized into a claw_event (platform
 * "cli") and pushed to the configured sink (normally the event router). The
 * router/agent thread delivers the answer back through the registered
 * "cli" sender.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "claw_common.h"
#include "claw_config.h"
#include "claw_event.h"
#include "claw_cli.h"
#include "claw_cap.h"
#include "claw_log.h"

static claw_err_t (*g_sink)(claw_event_t *ev) = NULL;
static char  g_session[CLAW_EVENT_SESS_LEN] = "default";
static volatile int g_running = 1;

claw_err_t claw_cli_set_event_sink(claw_err_t (*sink)(claw_event_t *ev))
{
  g_sink = sink;
  return CLAW_OK;
}

static void print_help(void)
{
  printf(
    "Vela-Claw commands:\n"
    "  ask <text>        send a prompt to the agent (default session)\n"
    "  ask_once <text>   send a one-shot prompt (ephemeral session)\n"
    "  auto <text>       alias of ask (autonomous mode)\n"
    "  session <name>    switch active session\n"
    "  cap <name> [json] call a capability directly (no LLM)\n"
    "  /run <lua>        run a Lua snippet directly\n"
    "  help              show this help\n"
    "  exit|quit         leave the REPL\n");
}

/* Dispatch one input line. Returns 0 to quit the REPL, 1 to continue. */
static int dispatch_line(const char *line)
{
  /* Trim leading whitespace. */
  while (*line == ' ' || *line == '\t') line++;
  if (*line == '\0') return 1;

  if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) return 0;
  if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0)
    {
      print_help();
      return 1;
    }

  claw_event_t ev;
  if (strncmp(line, "ask ", 4) == 0)
    {
      claw_event_init(&ev, CLAW_EVENT_MESSAGE, line + 4, "cli", "user",
                      g_session);
    }
  else if (strncmp(line, "ask_once ", 9) == 0)
    {
      claw_event_init(&ev, CLAW_EVENT_MESSAGE, line + 9, "cli", "user",
                      "once");
    }
  else if (strncmp(line, "auto ", 5) == 0)
    {
      claw_event_init(&ev, CLAW_EVENT_MESSAGE, line + 5, "cli", "user",
                      g_session);
    }
  else if (strncmp(line, "session ", 8) == 0)
    {
      strncpy(g_session, line + 8, sizeof(g_session) - 1);
      g_session[sizeof(g_session) - 1] = '\0';
      printf("session -> %s\n", g_session);
      return 1;
    }
  else if (strncmp(line, "cap ", 4) == 0)
    {
      const char *rest = line + 4;
      char name[64];
      int i = 0;
      while (*rest && *rest != ' ' && i < (int)sizeof(name) - 1)
        name[i++] = *rest++;
      name[i] = '\0';
      const char *args = (*rest == ' ') ? rest + 1 : "{}";
      char out[1024];
      claw_cap_call(name, args, out, sizeof(out));
      printf("%s\n", out);
      return 1;
    }
  else if (strncmp(line, "/run ", 5) == 0)
    {
      /* Routed by the router (runs Lua directly). */
      claw_event_init(&ev, CLAW_EVENT_MESSAGE, line, "cli", "user",
                      g_session);
    }
  else
    {
      /* Default: treat as a plain prompt. */
      claw_event_init(&ev, CLAW_EVENT_MESSAGE, line, "cli", "user",
                      g_session);
    }

  if (g_sink) (void)g_sink(&ev);
  return 1;
}

claw_err_t claw_cli_submit_line(const char *line)
{
  if (!line) return CLAW_EINVAL;
  dispatch_line(line);
  return CLAW_OK;
}

claw_err_t claw_cli_start(void)
{
  printf("\n=== Vela-Claw %s ===\n", VELA_CLAW_VERSION);
  print_help();
  printf("%s", g_claw_config.cli_prompt);
  fflush(stdout);

  char line[CLAW_EVENT_TEXT_LEN];
  while (g_running)
    {
      if (!fgets(line, sizeof(line), stdin)) break;
      size_t n = strlen(line);
      while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        line[--n] = '\0';
      if (!dispatch_line(line)) break;
      printf("%s", g_claw_config.cli_prompt);
      fflush(stdout);
    }

  return CLAW_OK;
}

void claw_cli_stop(void)
{
  g_running = 0;
}
