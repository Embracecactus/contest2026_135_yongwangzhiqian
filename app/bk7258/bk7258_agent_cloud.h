/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_AGENT_CLOUD_H
#define __APP_BK7258_AGENT_CLOUD_H
#include <stddef.h>
#include <stdbool.h>
struct bkcloud_models_s;

/* Service-protocol backends for the official voice registries. No capture,
 * playback, conversation, history, worker or recovery owner lives here. */
int bkagent_cloud_register(void);
int bkagent_cloud_activate_llm(void);
/* Explicitly configured real-time ASR only; never changes the user's backend. */
int bkagent_cloud_prepare_asr(const char *name);
int bkagent_cloud_clear(void);
/* Public MCP1 model names from the installed protected configuration. */
int bkagent_cloud_models_get(struct bkcloud_models_s *models);
/* Published only after the product confirms the persisted setting; only the
 * selected backend that supports this parameter advertises the capability.
 */
void bkagent_cloud_set_thinking(bool enabled);
int bkagent_cloud_get_thinking(bool *enabled);
/* 0 standard leaves the application request unchanged; nonzero modes only
 * express a service-side preference and never truncate a streamed reply.
 */

void bkagent_cloud_set_response_length(unsigned int mode);
int bkagent_cloud_get_response_length(unsigned int *mode);
/* Verify the selected server's TLS identity. This does not claim ASR/LLM/TTS
 * request success. Called before the product enables voice requests. */
int bkagent_cloud_verify_service(void);

/* Called by the existing authenticated configuration owner at an idle voice
 * boundary. BVC1 and CCF1 remain secret records in the existing storage path.
 * Copies/validates service configuration. The owner then explicitly selects
 * each backend via the official registry; this setter never switches TTS.
 * No request is made. Local TTS uses its own ops
 * and never requires these cloud records. */
int bkagent_cloud_configure(const void *trust, size_t trust_size,
                           const void *cloud, size_t cloud_size);
/* Install an authenticated CCF1 candidate with public model names supplied by
 * the control transaction. The caller persists those names only after backend
 * activation and TLS verification have succeeded. */
int bkagent_cloud_configure_models(const void *trust, size_t trust_size,
                                  const void *cloud, size_t cloud_size,
                                  const struct bkcloud_models_s *models);
#ifdef CONFIG_BK7258_AUDIO_PIPELINE_VALIDATION
#include <stdint.h>
struct bkagent_cloud_validation_s
{
  size_t bytes[2];
  uint32_t hash[2];
  bool text_matches[2];
};
int bkagent_cloud_validation_begin(void);
int bkagent_cloud_validation_reset(int cancel_tail);
int bkagent_cloud_validation_end(void);
void bkagent_cloud_validation_pcm(struct bkagent_cloud_validation_s *out);
#endif
#endif
