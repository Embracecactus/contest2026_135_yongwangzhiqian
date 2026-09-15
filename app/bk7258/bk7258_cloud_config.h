/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_CONFIG_H
#define __APP_BK7258_CLOUD_CONFIG_H
#include <stddef.h>
#include <stdint.h>

#define BKCLOUD_KEY_MAX 4096u
#define BKCLOUD_NAME_MAX 127u
#define BKCLOUD_CONFIG_MAX (24u + BKCLOUD_KEY_MAX + 5u * BKCLOUD_NAME_MAX)
#define BKCLOUD_MODELS_RECORD_MAX (12u + 3u * BKCLOUD_NAME_MAX)

/* MCP1 is deliberately public: it carries only the three selected model
 * identifiers.  CCF1 retains the endpoint, dialect and credential material.
 */
struct bkcloud_models_s
{
  char asr_model[128];
  char chat_model[128];
  char tts_model[128];
};

/* CCF1 is a secret provisioning payload, not a readable status record.
 * Header (network byte order): magic[4], dialect[1], reserved[1], port[2],
 * six lengths[2 each] (host, base_path, key, ASR, chat, TTS), reserved[4].
 * Followed by the six strings without NUL. HTTPS is mandatory.
 * Dialect 1 = OpenAI Chat Completions audio; 2 = MiMo extensions.
 * Declaring a dialect does not imply its runtime adapter is installed.
 */
struct bkcloud_config_s
{
  uint8_t dialect;
  uint16_t port;
  char host[128];
  char base_path[128];
  char api_key[BKCLOUD_KEY_MAX + 1u];
  char asr_model[128];
  char chat_model[128];
  char tts_model[128];
  /* Optional protocol voice selection; CCF1/MCP1 wire formats are unchanged.
   * The official TTS configuration adapter fills this non-secret value. */
  char tts_voice[128];
};

/* Clear output on error; record must not alias output. */
int bkcloud_config_decode(struct bkcloud_config_s *config,
                          const void *record, size_t size);
int bkcloud_models_decode(struct bkcloud_models_s *models,
                          const void *record, size_t size);
int bkcloud_models_encode(const struct bkcloud_models_s *models,
                          uint8_t *record, size_t capacity, size_t *size);
void bkcloud_config_clear(struct bkcloud_config_s *config);
#endif
