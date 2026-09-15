/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_AUDIO_H
#define __APP_BK7258_CLOUD_AUDIO_H
#include "bk7258_cloud_config.h"
#include "bk7258_cloud_http.h"
#include "bk7258_cloud_request.h"
#include "bk7258_cloud_tts.h"

/* Caller-owned workspace for one synchronous audio protocol request. */
struct bkcloud_client_s
{
  struct bkcloud_http_s http;
  struct bkcloud_asr_source_s source;
  char response[32768];
};

bool bkcloud_audio_valid_text(const char *text);

int bkcloud_recognize(struct bkcloud_client_s *client,
                      const struct bkcloud_config_s *config,
                      const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                      uint64_t deadline_ms, const uint8_t *pcm, size_t pcm_size,
                      char *text, size_t capacity);
int bkcloud_synthesize(struct bkcloud_client_s *client,
                       struct bkcloud_tts_s *decoder,
                       const struct bkcloud_config_s *config,
                       const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                       uint64_t deadline_ms, const char *text,
                       bkcloud_write_t pcm, void *context);
#endif
