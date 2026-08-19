/****************************************************************************
 * app/vela_claw/src/transport/transport_curl.c
 *
 * Real HTTP/TLS transport using libcurl. On the host it links the system
 * libcurl; on NuttX it links libcurl4nx (same API, backed by mbedTLS). This
 * is the backend used on real t5board hardware to reach the cloud LLM.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "claw_common.h"
#include "claw_transport.h"

#ifdef HAVE_LIBCURL
#include <curl/curl.h>

typedef struct {
  char  *buf;
  size_t len;
  size_t cap;
} membuf_t;

static size_t write_cb(void *data, size_t size, size_t nmemb, void *user) {
  membuf_t *m = (membuf_t *)user;
  size_t total = size * nmemb;
  if (m->len + total + 1 > m->cap) {
    m->cap = (m->len + total + 1) * 2;
    char *nb = realloc(m->buf, m->cap);
    if (!nb) return 0;
    m->buf = nb;
  }
  memcpy(m->buf + m->len, data, total);
  m->len += total;
  m->buf[m->len] = '\0';
  return total;
}

static claw_err_t curl_post(const char *url, const char *body,
                            const char *auth_header, int timeout_ms,
                            claw_http_response_t *out)
{
  CURL *c = curl_easy_init();
  if (!c) return CLAW_ENOMEM;
  membuf_t m = {0};
  m.cap = 4096;
  m.buf = malloc(m.cap);
  if (!m.buf) { curl_easy_cleanup(c); return CLAW_ENOMEM; }
  m.buf[0] = '\0';

  struct curl_slist *hdr = NULL;
  hdr = curl_slist_append(hdr, "Content-Type: application/json");
  if (auth_header) hdr = curl_slist_append(hdr, auth_header);

  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_POST, 1L);
  curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &m);
  curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeout_ms);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);

  CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);

  out->status = (rc == CURLE_OK) ? (int)status : 0;
  out->body = m.buf;
  out->len = m.len;

  curl_slist_free_all(hdr);
  curl_easy_cleanup(c);
  return (rc == CURLE_OK) ? CLAW_OK : CLAW_EIO;
}

static claw_transport_t g_curl = { "curl", curl_post };

void claw_transport_curl_register(void) {
  claw_transport_register(&g_curl);
}

#else /* !HAVE_LIBCURL */

/* No libcurl linked: the curl backend degrades to a clear error so the build
 * still succeeds and the app can run with the mock transport. */
static claw_err_t curl_post(const char *url, const char *body,
                            const char *auth_header, int timeout_ms,
                            claw_http_response_t *out)
{
  (void)url; (void)body; (void)auth_header; (void)timeout_ms;
  out->status = 0;
  out->body = strdup("{\"error\":\"curl transport not compiled in\"}");
  out->len = out->body ? strlen(out->body) : 0;
  return CLAW_ENOSYS;
}

static claw_transport_t g_curl = { "curl", curl_post };

void claw_transport_curl_register(void) {
  claw_transport_register(&g_curl);
}
#endif
