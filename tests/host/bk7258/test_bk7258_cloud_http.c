/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_http.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct peer_s { const char *reply; size_t offset; char request[512]; size_t sent;
  unsigned int opens; unsigned int closes; int failure; };

static int open_peer(void *arg, const char *host, uint16_t port, uint64_t due)
{ struct peer_s *p = arg; assert(!strcmp(host, "cloud.example") && port == 443 && due == 123456); p->opens++; return 0; }
static ssize_t send_peer(void *arg, const uint8_t *data, size_t size, uint64_t due)
{ struct peer_s *p = arg; assert(due == 123456); if (p->failure) return p->failure; if (size > 7) size = 7; assert(p->sent + size < sizeof(p->request)); memcpy(p->request + p->sent, data, size); p->sent += size; return size; }
static ssize_t recv_peer(void *arg, uint8_t *data, size_t size, uint64_t due)
{ struct peer_s *p = arg; size_t left; assert(due == 123456); left = strlen(p->reply) - p->offset; if (size > left) size = left; if (size > 3) size = 3; memcpy(data, p->reply + p->offset, size); p->offset += size; return size; }
static int close_peer(void *arg) { ((struct peer_s *)arg)->closes++; return 0; }
static const struct bkvoice_wss_tls_ops_s g_tls = {.open_verified = open_peer, .send = send_peer, .recv = recv_peer, .close = close_peer};
static int body(void *buffer, size_t *size, const void **data, size_t requested, void *context)
{ (void)context; assert(*size >= 2 && requested == 2); memcpy(buffer, "{}", 2); *data = buffer; *size = 2; return 0; }
static void post(const char *reply, int expected, int failure, size_t capacity)
{
  struct bkcloud_http_s *http = calloc(1, sizeof(*http));
  struct bkcloud_config_s config = {.port = 443};
  struct peer_s peer = {.reply = reply, .failure = failure}; char output[128];
  assert(http != NULL); strcpy(config.host, "cloud.example"); strcpy(config.base_path, "/v1"); strcpy(config.api_key, "test-only-key"); memset(output, 'x', sizeof(output));
  int actual = bkcloud_http_post(http, &config, "chat/completions", &g_tls,
                                 &peer, 123456, body, NULL, 2, output, capacity);
  if (actual != expected)
    fprintf(stderr, "cloud HTTP expected=%d actual=%d\n", expected, actual);
  assert(actual == expected);
  assert(peer.opens == 1 && peer.closes == 1);
  assert(!http->connected && http->config == NULL && http->response == NULL);
  if (expected == 0) { assert(!strcmp(output, "{}")); assert(strstr(peer.request, "POST /v1/chat/completions HTTP/1.1\r\n")); assert(strstr(peer.request, "Authorization: Bearer test-only-key\r\n")); }
  free(http);
}
int main(void)
{
  post("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}", 0, 0, 128);
  post("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\n{\r\n1\r\n}\r\n0\r\n\r\n", 0, 0, 128);
  post("HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n", -EPROTO, 0, 128);
  post("HTTP/1.1 302 Found\r\nLocation: https://other.example/\r\nContent-Length: 0\r\n\r\n", -EPERM, 0, 128);
  post("HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\n\r\n{}", -EACCES, 0, 128);
  post("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}", -E2BIG, 0, 2);
  post("", -ECANCELED, -ECANCELED, 128);
  return 0;
}
