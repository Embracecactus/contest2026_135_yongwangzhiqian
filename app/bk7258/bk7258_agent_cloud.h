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
int bkagent_cloud_clear(void);
/* Public MCP1 model names from the installed protected configuration. */
int bkagent_cloud_models_get(struct bkcloud_models_s *models);
/* Published only after the product confirms the persisted setting; only the
 * selected backend that supports this parameter advertises the capability.
 */
void bkagent_cloud_set_thinking(bool enabled);
int bkagent_cloud_get_thinking(bool *enabled);
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
#endif
