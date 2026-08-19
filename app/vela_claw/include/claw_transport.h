/****************************************************************************
 * app/vela_claw/include/claw_transport.h
 *
 * Pluggable HTTP/TLS transport interface. The LLM client depends only on this
 * interface, so the backend (mock for offline/host-tests, libcurl+mbedTLS for
 * real hardware) is swappable without touching core logic.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef VELA_CLAW_TRANSPORT_H
#define VELA_CLAW_TRANSPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct claw_http_response_s {
  int    status;     /* HTTP status, 0 on transport failure */
  char  *body;       /* heap-allocated, NUL-terminated; caller frees */
  size_t len;
} claw_http_response_t;

typedef struct claw_transport_s {
  const char *name;  /* "mock" | "curl" */
  claw_err_t (*post_json)(const char *url, const char *body,
                          const char *auth_header, int timeout_ms,
                          claw_http_response_t *out);
} claw_transport_t;

void claw_http_response_free(claw_http_response_t *r);

void claw_transport_register(claw_transport_t *t);
claw_transport_t *claw_transport_get(const char *name);
/* Falls back to "curl" on target, "mock" on host. */
claw_transport_t *claw_transport_default(void);

/* Backend registration (implemented in transport_mock.c / transport_curl.c).
 * Declared here so callers (e.g. vela_claw_init) need not include the
 * backend-specific headers. */
void claw_transport_mock_register(void);
void claw_transport_curl_register(void);

#ifdef __cplusplus
}
#endif

#endif /* VELA_CLAW_TRANSPORT_H */
