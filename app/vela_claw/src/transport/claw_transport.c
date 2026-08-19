/****************************************************************************
 * app/vela_claw/src/transport/claw_transport.c
 *
 * Transport registry. Backends register here; the LLM client resolves the
 * configured backend by name.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "claw_common.h"
#include "claw_transport.h"
#include "claw_config.h"

#define MAX_TRANSPORTS 8

static claw_transport_t *g_transports[MAX_TRANSPORTS];
static int g_count;

void claw_transport_register(claw_transport_t *t) {
  if (!t || g_count >= MAX_TRANSPORTS) return;
  int i;
  for (i = 0; i < g_count; i++)
    if (strcmp(g_transports[i]->name, t->name) == 0) {
      g_transports[i] = t;
      return;
    }
  g_transports[g_count++] = t;
}

claw_transport_t *claw_transport_get(const char *name) {
  if (!name) return NULL;
  int i;
  for (i = 0; i < g_count; i++)
    if (strcmp(g_transports[i]->name, name) == 0)
      return g_transports[i];
  return NULL;
}

claw_transport_t *claw_transport_default(void) {
  /* If a transport is configured, honor it. */
  if (g_claw_config.transport[0]) {
    claw_transport_t *t = claw_transport_get(g_claw_config.transport);
    if (t) return t;
  }
#ifdef __NuttX__
  return claw_transport_get("curl");
#else
  return claw_transport_get("mock");
#endif
}

void claw_http_response_free(claw_http_response_t *r) {
  if (!r) return;
  if (r->body) {
    free(r->body);
    r->body = NULL;
  }
  r->len = 0;
  r->status = 0;
}
