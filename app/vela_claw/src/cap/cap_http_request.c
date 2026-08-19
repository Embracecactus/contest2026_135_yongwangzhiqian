/****************************************************************************
 * app/vela_claw/src/cap/cap_http_request.c
 *
 * http_request capability: make an outbound HTTPS request. On target it uses
 * the curl transport; useful for the LLM to fetch arbitrary data.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_cap.h"
#include "claw_transport.h"

static claw_err_t run(const char *args_json, char *out, size_t outlen)
{
  /* {"url":"..."} */
  const char *url = args_json ? strstr(args_json, "\"url\"") : NULL;
  if (!url) { snprintf(out, outlen, "[missing url]"); return CLAW_EINVAL; }
  const char *q = strchr(url + 5, '"');
  if (!q) { snprintf(out, outlen, "[bad url]"); return CLAW_EINVAL; }
  const char *e = strchr(q + 1, '"');
  if (!e) { snprintf(out, outlen, "[bad url]"); return CLAW_EINVAL; }
  char u[512];
  size_t n = (size_t)(e - (q + 1));
  if (n >= sizeof(u)) n = sizeof(u) - 1;
  memcpy(u, q + 1, n);
  u[n] = '\0';

  claw_transport_t *t = claw_transport_default();
  if (!t) { snprintf(out, outlen, "[no transport]"); return CLAW_ENOSYS; }
  claw_http_response_t resp = {0};
  claw_err_t rc = t->post_json(u, "{}", NULL, 15000, &resp);
  if (rc == CLAW_OK && resp.body)
    snprintf(out, outlen, "status=%d body=%.*s",
             resp.status, (int)MIN(outlen - 32, resp.len), resp.body);
  else
    snprintf(out, outlen, "[request failed]");
  claw_http_response_free(&resp);
  return rc;
}

claw_cap_t cap_http_request = {
  "http_request",
  "Perform an outbound HTTPS GET/POST and return the response.",
  run
};
