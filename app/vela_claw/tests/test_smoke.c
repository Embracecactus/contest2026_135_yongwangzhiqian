/****************************************************************************
 * app/vela_claw/tests/test_smoke.c
 *
 * Host smoke test (no NuttX, no LVGL, no libcurl): builds the whole Vela-Claw
 * core against the mock transport and verifies the agent loop converges:
 *   CLI submit -> mock LLM tool_call -> capability (lua_run_script) ->
 *   led_blink (mock engine) -> final answer delivered to the sender.
 *
 * Build with tests/Makefile. Link the same sources the firmware links, minus
 * vela_claw_main.c (this file provides main) and minus the UI module.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "claw_common.h"
#include "claw_config.h"
#include "claw_event.h"
#include "claw_event_router.h"
#include "claw_cap.h"
#include "claw_cli.h"
#include "vela_claw_app.h"

static char     g_last[2048];
static int      g_got;
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_c = PTHREAD_COND_INITIALIZER;

/* Capture agent replies (runs on the agent worker thread). */
static claw_err_t test_sender(const char *text, const char *session)
{
  (void)session;
  if (text)
    {
      pthread_mutex_lock(&g_m);
      strncpy(g_last, text, sizeof(g_last) - 1);
      g_last[sizeof(g_last) - 1] = '\0';
      g_got = 1;
      pthread_cond_signal(&g_c);
      pthread_mutex_unlock(&g_m);
    }
  return CLAW_OK;
}

int main(void)
{
  vela_claw_app_init("/tmp/vela_claw_test");

  /* Route CLI/submitted events through the router, and capture replies. */
  claw_cli_set_event_sink(claw_event_router_handle);
  claw_event_router_set_sender("cli", test_sender);

  /* ---- Unit: direct capability (lua_run_script -> led_blink) ---- */
  char out[1024];
  claw_cap_call("lua_run_script", "{\"code\":\"led_blink(3)\"}",
                out, sizeof(out));
  printf("[test] cap lua_run_script -> %s\n", out);
  if (strstr(out, "led_blink") == NULL)
    {
      printf("FAIL: led_blink capability did not run\n");
      vela_claw_app_deinit();
      return 1;
    }

  /* ---- Integration: full agent loop via CLI submit ---- */
  claw_cli_submit_line("ask please blink the led");

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += 10;
  pthread_mutex_lock(&g_m);
  while (!g_got)
    if (pthread_cond_timedwait(&g_c, &g_m, &ts) == ETIMEDOUT) break;
  int ok = g_got;
  char buf[2048];
  strncpy(buf, g_last, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  pthread_mutex_unlock(&g_m);

  printf("[test] agent reply -> %s\n", buf);
  if (!ok || strstr(buf, "LED") == NULL)
    {
      printf("FAIL: agent loop did not converge to a final answer\n");
      vela_claw_app_deinit();
      return 1;
    }

  printf("ALL TESTS PASSED\n");
  vela_claw_app_deinit();
  return 0;
}
