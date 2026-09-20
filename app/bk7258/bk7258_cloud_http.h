/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_HTTP_H
#define __APP_BK7258_CLOUD_HTTP_H
#include "bk7258_cloud_config.h"
#include "bk7258_cloud_request.h"
#include "bk7258_voice_transport.h"
#include <netutils/webclient.h>

/* SSE consumers may explicitly finish a validated application stream. */
#define BKCLOUD_HTTP_STREAM_COMPLETE 1

/* One request owner; no concurrent calls on a transport. Use the verified TLS
 * provider with server_auth_only, a resolved address and trusted time. The
 * owner cancels through its TLS interrupt operation and joins before freeing
 * any borrowed config, PCM, response or transport. No retries are implicit.
 */
struct bkcloud_http_s
{
  const struct bkvoice_wss_tls_ops_s *tls;
  void *tls_context;
  uint64_t deadline_ms;
  const struct bkcloud_config_s *config;
  char *response;
  size_t capacity;
  size_t received;
  uint64_t connect_ms;
  uint64_t send_ms;
  uint32_t send_calls;
  uint64_t receive_ms;
  uint64_t max_receive_ms;
  uint32_t receive_calls;
  uint32_t slow_receives;
  uint64_t consume_ms;
  unsigned int status;
  bool connected;
  bkcloud_write_t consume;
  void *consume_context;
  const struct webclient_context *active;
  bool event_stream;
  bool pcm_response;
  bool pcm_stream;
  bool stream_complete;
  char authorization[BKCLOUD_KEY_MAX + 32];
  char buffer[BKCLOUD_KEY_MAX + 1024];
};
/* Config must come from successful CCF1 decoding. Endpoint is a constant
 * adapter path, relative to base_path, e.g. "chat/completions". The caller
 * allocates this workspace outside the small audio worker stack. Response
 * is cleared on failure; 2xx means HTTP success, not valid model output.
 */
int bkcloud_http_post(struct bkcloud_http_s *http,
                     const struct bkcloud_config_s *config,
                     const char *endpoint,
                     const struct bkvoice_wss_tls_ops_s *tls,
                     void *tls_context, uint64_t deadline_ms,
                     webclient_body_callback_t body, void *body_context,
                     size_t body_size, char *response, size_t capacity);
/* Protected-resource GET: borrows only host/port from config, uses no cloud
 * credentials and performs no implicit retry. The capacity includes the
 * trailing reserved byte, so the actual binary length is http->received.
 */
int bkcloud_http_get(struct bkcloud_http_s *http,
                    const struct bkcloud_config_s *config, const char *url,
                    const struct bkvoice_wss_tls_ops_s *tls,
                    void *tls_context, uint64_t deadline_ms,
                    char *response, size_t capacity);
/* Streaming SSE response. Consumer owns rollback/abort of any partial audio.
 * Non-2xx bodies are never passed to it. Limit counts raw HTTP body bytes.
 * Return 0 for more data, BKCLOUD_HTTP_STREAM_COMPLETE after validating the
 * terminal event, or a negative error. Completion closes this one request;
 * it never converts transport cancellation or incomplete output to success.
 */
int bkcloud_http_events(struct bkcloud_http_s *http,
                       const struct bkcloud_config_s *config,
                       const struct bkvoice_wss_tls_ops_s *tls,
                       void *tls_context, uint64_t deadline_ms,
                       const char *request, size_t request_size,
                       bkcloud_write_t consume, void *context, size_t limit);
/* Standard audio/speech adapter: raw 24kHz mono PCM16-LE. Non-audio
 * responses are rejected before consumption; caller aborts partial playback
 * on failure. Limit applies to the raw byte stream, without full buffering.
 */
int bkcloud_http_pcm(struct bkcloud_http_s *http,
                     const struct bkcloud_config_s *config,
                     const struct bkvoice_wss_tls_ops_s *tls,
                     void *tls_context, uint64_t deadline_ms,
                     const char *request, size_t request_size,
                     bkcloud_write_t consume, void *context, size_t limit);
#endif
