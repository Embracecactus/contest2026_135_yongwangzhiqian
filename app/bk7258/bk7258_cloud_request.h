/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_REQUEST_H
#define __APP_BK7258_CLOUD_REQUEST_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define BKCLOUD_PCM_MAX (30u * 16000u * 2u)
#define BKCLOUD_JPEG_MAX (512u * 1024u)
#define BKCLOUD_TEXT_MAX 4096u

/* Writer consumes the complete chunk and returns zero, or a negative errno.
 * The HTTPS owner enforces deadlines/cancellation and content length.
 * No credentials, network connections or retained audio are owned here.
 */
typedef int (*bkcloud_write_t)(void *context, const void *data, size_t size);
/* Pull source for OpenVela webclient_body_callback_t. PCM is borrowed and
 * must remain immutable until close. No complete Base64 copy is retained.
 */
struct bkcloud_asr_source_s
{
  const uint8_t *pcm;
  size_t pcm_size;
  size_t offset;
  size_t length;
  size_t prefix_size;
  char prefix[384];
  uint8_t wav[44];
};
int bkcloud_asr_source_init(struct bkcloud_asr_source_s *source,
                           const char *model, const uint8_t *pcm, size_t size);
int bkcloud_asr_body(void *buffer, size_t *size, const void **data,
                    size_t requested, void *context);
void bkcloud_asr_source_clear(struct bkcloud_asr_source_s *source);
/* JPEG is borrowed and immutable until clear. prefix/suffix are caller-owned
 * JSON fragments that surround the Base64 payload, so no full Base64 copy is
 * retained. The source validates only JPEG SOI/EOI framing, not image syntax.
 */
struct bkcloud_image_source_s
{
  const char *prefix;
  size_t prefix_size;
  const char *suffix;
  size_t suffix_size;
  const uint8_t *jpeg;
  size_t jpeg_size;
  size_t offset;
  size_t length;
};
int bkcloud_image_source_init(struct bkcloud_image_source_s *source,
                              const char *prefix, size_t prefix_size,
                              const char *suffix, size_t suffix_size,
                              const uint8_t *jpeg, size_t jpeg_size);
int bkcloud_image_body(void *buffer, size_t *size, const void **data,
                       size_t requested, void *context);
void bkcloud_image_source_clear(struct bkcloud_image_source_s *source);
/* Chat Completions input_audio/WAV dialect, supported by MiMo and compatible
 * audio models. This is not the multipart /audio/transcriptions protocol.
 * Provider adapters choose the model and dialect explicitly.
 */
int bkcloud_asr_size(const char *model, size_t pcm_size, size_t *body_size);
int bkcloud_asr_write(const char *model, const uint8_t *pcm, size_t size,
                      bkcloud_write_t write, void *context);
/* Strict single-choice completed ASR/chat response; output cleared on error.
 * JSON is bounded and need not have a terminating NUL. Output must not alias
 * JSON. Tool calls, truncated completions and trailing data are rejected.
 */
bool bkcloud_json_safe(const char *json, size_t size);
int bkcloud_text_parse(const char *json, size_t size,
                      char *text, size_t capacity);
#endif
