/****************************************************************************
 * app/vela_claw/src/cap/cap_web_search.c
 *
 * web_search capability: stub for Brave/Tavily-style search. Full port wires
 * a real search backend; here it reports the query and a placeholder.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_cap.h"

static claw_err_t run(const char *args_json, char *out, size_t outlen)
{
  const char *q = args_json ? strstr(args_json, "\"query\"") : NULL;
  if (q) {
    const char *s = strchr(q + 7, '"');
    const char *e = s ? strchr(s + 1, '"') : NULL;
    if (s && e) {
      size_t n = (size_t)(e - (s + 1));
      char buf[256];
      if (n >= sizeof(buf)) n = sizeof(buf) - 1;
      memcpy(buf, s + 1, n); buf[n] = '\0';
      snprintf(out, outlen, "[web_search: %s -> (backend not configured)]", buf);
      return CLAW_OK;
    }
  }
  snprintf(out, outlen, "[web_search: no query]");
  return CLAW_EINVAL;
}

claw_cap_t cap_web_search = {
  "web_search",
  "Search the web for a query (backend configurable).",
  run
};
