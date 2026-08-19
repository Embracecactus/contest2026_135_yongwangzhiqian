/****************************************************************************
 * app/vela_claw/src/transport/transport_mock.c
 *
 * Mock HTTP/TLS transport for offline use and host unit tests. It does not
 * touch the network: it returns scripted chat-completion responses so the
 * full agent loop (tool-call iteration, capability execution, Lua) is
 * verifiable without Wi-Fi or an API key.
 *
 * Behavior: if the request looks like a first turn, return a tool_call that
 * asks the agent to run a Lua script that blinks an LED. On later turns
 * (when the tool result is supplied), return a final natural-language answer.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_transport.h"

/* Call counter: the FIRST call emulates the LLM asking the agent to run a
 * Lua script (tool_call). Every subsequent call emulates the LLM returning
 * the final natural-language answer. This lets the agent loop converge
 * exactly like a real LLM would, independent of request content. */
static int g_mock_calls;

static claw_err_t mock_post(const char *url, const char *body,
                            const char *auth_header, int timeout_ms,
                            claw_http_response_t *out)
{
  (void)url;
  (void)auth_header;
  (void)timeout_ms;

  const char *payload;
  if (g_mock_calls++ == 0) {
    /* First turn: instruct the agent to invoke the lua capability. */
    payload =
      "{\"choices\":[{\"message\":{\"role\":\"assistant\","
      "\"content\":null,"
      "\"tool_calls\":[{\"id\":\"call_001\",\"type\":\"function\","
      "\"function\":{\"name\":\"lua_run_script\","
      "\"arguments\":\"{\\\"code\\\":\\\"led_blink(3)\\\"}\"}}]}}]}";
  } else {
    /* Later turns: final natural-language answer. */
    payload =
      "{\"choices\":[{\"message\":{\"role\":\"assistant\","
      "\"content\":\"Done. The LED was blinked via Lua as requested.\\n\"}}]}";
  }

  (void)body;
  out->body = strdup(payload);
  out->len = strlen(payload);
  out->status = 200;
  return CLAW_OK;
}

static claw_transport_t g_mock = { "mock", mock_post };

void claw_transport_mock_register(void) {
  claw_transport_register(&g_mock);
}
