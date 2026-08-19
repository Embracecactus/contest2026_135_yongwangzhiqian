/****************************************************************************
 * app/vela_claw/src/core/claw_core.c
 *
 * Agent core: a worker thread drains a request queue and runs the
 * perceive -> decide (LLM) -> execute (tool calls) loop. Context is composed
 * from registered providers; tool calls are dispatched through the
 * configured executor (normally the capability system). The final answer is
 * delivered asynchronously via the per-request callback.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_log.h"
#include "claw_rtos.h"
#include "claw_config.h"
#include "claw_core.h"
#include "claw_json.h"
#include "claw_transport.h"
#include "claw_memory.h"
#include "claw_event.h"
#include "claw_cap.h"

#define MAX_PROVIDERS 16
#define REQ_MSG_SIZE  (sizeof(request_t))

typedef struct request_s {
  char prompt[CLAW_EVENT_TEXT_LEN];
  char session[CLAW_EVENT_SESS_LEN];
  claw_core_response_cb cb;
  void *arg;
} request_t;

typedef struct {
  char name[48];
  claw_context_provider_t fn;
} provider_t;

static claw_queue_t *g_queue;
static claw_thread_t g_thread;
static volatile bool g_running;
static claw_tool_executor_t g_executor;
static provider_t g_providers[MAX_PROVIDERS];
static int g_provider_count;

static void build_system_prompt(char *buf, size_t buflen) {
  int off = snprintf(buf, buflen,
    "You are Vela-Claw, an on-device AI agent for openvela/NuttX IoT "
    "devices. You act by calling capabilities (tools). When the user "
    "asks to control hardware, emit a tool call.\n");
  int i;
  for (i = 0; i < g_provider_count; i++) {
    char pb[512];
    if (g_providers[i].fn(pb, sizeof(pb)) == CLAW_OK && pb[0]) {
      off += snprintf(buf + off, buflen - off, "\n%s", pb);
    }
  }
}

/* Extract a tool call (name + args) from an LLM response body, or NULL. */
static bool extract_tool_call(const char *body, char *name, size_t namesz,
                              char *args, size_t argssz) {
  claw_json_t *root = claw_json_parse(body);
  if (!root) return false;
  const claw_json_t *choices = claw_json_get(root, "choices/0/message");
  bool found = false;
  if (choices && choices->type == CLAW_JSON_OBJ) {
    size_t i;
    for (i = 0; i < choices->u.obj.count; i++) {
      if (strcmp(choices->u.obj.keys[i], "tool_calls") == 0) {
        claw_json_t *tc = choices->u.obj.vals[i];
        if (tc->type == CLAW_JSON_ARR && tc->u.arr.count > 0) {
          claw_json_t *first = tc->u.arr.items[0];
          const claw_json_t *fn = claw_json_get(first, "function/name");
          const claw_json_t *fa = claw_json_get(first, "function/arguments");
          const char *n = claw_json_str(fn);
          const char *a = claw_json_str(fa);
          if (n) {
            strncpy(name, n, namesz - 1);
            name[namesz - 1] = '\0';
            strncpy(args, a ? a : "{}", argssz - 1);
            args[argssz - 1] = '\0';
            found = true;
          }
        }
        break;
      }
    }
  }
  claw_json_free(root);
  return found;
}

static bool extract_content(const char *body, char *out, size_t outlen) {
  claw_json_t *root = claw_json_parse(body);
  if (!root) return false;
  const claw_json_t *c = claw_json_get(root, "choices/0/message/content");
  const char *s = claw_json_str(c);
  bool ok = false;
  if (s) { strncpy(out, s, outlen - 1); out[outlen - 1] = '\0'; ok = true; }
  claw_json_free(root);
  return ok;
}

static claw_err_t run_one_turn(const char *prompt, char *answer, size_t anslen)
{
  claw_transport_t *t = claw_transport_default();
  if (!t) { strncpy(answer, "[no transport]", anslen - 1); return CLAW_ENOSYS; }

  char sys[2048];
  build_system_prompt(sys, sizeof(sys));

  char tools[2048];
  tools[0] = '\0';
  if (g_claw_config.llm_supports_tools)
    claw_cap_tools_json(tools, sizeof(tools));

  char *req = claw_json_build_chat_request(g_claw_config.llm_model,
                                            sys, prompt,
                                            tools[0] ? tools : NULL, NULL);
  if (!req) return CLAW_ENOMEM;

  char auth[384];
  if (g_claw_config.llm_auth == CLAW_AUTH_BEARER)
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s",
             g_claw_config.llm_api_key);
  else if (g_claw_config.llm_auth == CLAW_AUTH_APIKEY)
    snprintf(auth, sizeof(auth), "X-API-Key: %s", g_claw_config.llm_api_key);
  else
    auth[0] = '\0';

  claw_http_response_t resp = {0};
  char url[320];
  snprintf(url, sizeof(url), "%s/chat/completions",
           g_claw_config.llm_base_url);

  claw_err_t rc = t->post_json(url, req, auth[0] ? auth : NULL,
                               g_claw_config.llm_timeout_ms, &resp);
  free(req);

  if (rc != CLAW_OK || !resp.body) {
    strncpy(answer, "[transport error]", anslen - 1);
    claw_http_response_free(&resp);
    return rc;
  }

  int iter = 0;
  char working_prompt[CLAW_EVENT_TEXT_LEN * 4];
  strncpy(working_prompt, prompt ? prompt : "",
          sizeof(working_prompt) - 1);
  working_prompt[sizeof(working_prompt) - 1] = '\0';

  while (iter < CLAW_CORE_MAX_TOOL_ITER) {
    char tc_name[128];
    char tc_args[1024];
    if (extract_tool_call(resp.body, tc_name, sizeof(tc_name),
                          tc_args, sizeof(tc_args))) {
      /* Execute the tool via the executor (capability system). */
      char tool_out[1024];
      if (g_executor) {
        rc = g_executor(tc_name, tc_args, tool_out, sizeof(tool_out));
      } else {
        snprintf(tool_out, sizeof(tool_out), "[no executor]");
        rc = CLAW_ENOSYS;
      }
      CLAW_LOGI("tool_call %s -> %s", tc_name, tool_out);
      /* Feed the tool result back as a follow-up user turn. Bounded append
       * into the tail of the buffer (never reads from the destination while
       * writing to it). */
      size_t cur = strlen(working_prompt);
      if (cur < sizeof(working_prompt) - 1) {
        snprintf(working_prompt + cur, sizeof(working_prompt) - cur,
                 "\n[tool %s returned: %s]", tc_name, tool_out);
      }

      free(resp.body); resp.body = NULL; resp.len = 0;
      char *req2 = claw_json_build_chat_request(g_claw_config.llm_model,
                                                sys, working_prompt,
                                                tools[0] ? tools : NULL, NULL);
      t->post_json(url, req2, auth[0] ? auth : NULL,
                  g_claw_config.llm_timeout_ms, &resp);
      free(req2);
      iter++;
      continue;
    }

    /* No tool call: final answer. */
    extract_content(resp.body, answer, anslen);
    claw_http_response_free(&resp);
    return CLAW_OK;
  }

  strncpy(answer, "[max tool iterations reached]", anslen - 1);
  claw_http_response_free(&resp);
  return CLAW_OK;
}

static void *agent_loop(void *arg) {
  (void)arg;
  request_t req;
  while (g_running) {
    if (claw_queue_pop(g_queue, &req, 500) != CLAW_OK) continue;

    /* A request with a NULL callback is the shutdown sentinel pushed by
     * claw_core_deinit: do not treat it as a real agent run (otherwise we
     * would burn an LLM call on an empty prompt on real hardware). */
    if (req.cb == NULL) break;

    CLAW_LOGI("agent run: session=%s prompt=%.40s", req.session, req.prompt);

    char answer[2048];
    claw_memory_append_session(req.session, "user", req.prompt);
    run_one_turn(req.prompt, answer, sizeof(answer));
    claw_memory_append_session(req.session, "assistant", answer);

    if (req.cb) req.cb(answer, req.arg);
  }
  return NULL;
}

claw_err_t claw_core_register_context_provider(const char *name,
                                               claw_context_provider_t fn)
{
  if (g_provider_count >= MAX_PROVIDERS) return CLAW_ENOMEM;
  strncpy(g_providers[g_provider_count].name, name,
          sizeof(g_providers[g_provider_count].name) - 1);
  g_providers[g_provider_count].fn = fn;
  g_provider_count++;
  return CLAW_OK;
}

void claw_core_set_tool_executor(claw_tool_executor_t fn) {
  g_executor = fn;
}

claw_err_t claw_core_init(void) {
  g_queue = claw_queue_create(16, REQ_MSG_SIZE);
  if (!g_queue) return CLAW_ENOMEM;
  g_running = true;
  claw_thread_create(&g_thread, agent_loop, NULL, 0, 64 * 1024);
  return CLAW_OK;
}

void claw_core_deinit(void) {
  g_running = false;
  if (g_queue) claw_queue_push(g_queue, &(request_t){0}, 0);
  claw_thread_join(g_thread);
  claw_queue_destroy(g_queue);
  g_queue = NULL;
}

claw_err_t claw_core_submit(const char *prompt, const char *session,
                            claw_core_response_cb cb, void *arg)
{
  if (!g_queue) return CLAW_EINVAL;
  request_t req;
  memset(&req, 0, sizeof(req));
  strncpy(req.prompt, prompt ? prompt : "", sizeof(req.prompt) - 1);
  strncpy(req.session, session ? session : "default",
          sizeof(req.session) - 1);
  req.cb = cb;
  req.arg = arg;
  return claw_queue_push(g_queue, &req, 0);
}
