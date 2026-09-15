/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_AGENT_CLOUD_H
#define __APP_BK7258_AGENT_CLOUD_H
#include <stddef.h>

/* Service-protocol backends for the official voice registries. No capture,
 * playback, conversation, history, worker or recovery owner lives here. */
int bkagent_cloud_register(void);
int bkagent_cloud_activate_llm(void);
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
#endif
