/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_TTS_H
#define __APP_BK7258_CLOUD_TTS_H
#include "bk7258_cloud_request.h"
#include <stdbool.h>

enum bkcloud_tts_observation_e
{
  BKCLOUD_TTS_OBSERVE_SSE_EVENT = 1,
  BKCLOUD_TTS_OBSERVE_BASE64_DECODE = 2,
};

/* Optional bounded timing observation. It exposes only monotonic times,
 * sizes and an errno-style result; no text, encoded audio or PCM is retained.
 */
typedef void (*bkcloud_tts_observe_t)(void *context,
                                      enum bkcloud_tts_observation_e kind,
                                      uint64_t begin_ms, uint64_t end_ms,
                                      size_t bytes, int result);

/* MiMo chat-audio SSE decoder. Output is PCM16-LE mono at 24000 Hz;
 * the audio owner must configure that rate or resample before playback.
 * A successful finish is not a playback acknowledgement. Abort/discard
 * queued partial audio when feed or finish fails.
 */
struct bkcloud_tts_s
{
  char *line;
  char *event;
  size_t line_capacity;
  size_t event_capacity;
  uint8_t pcm[3072];
  size_t line_size;
  size_t event_size;
  size_t total;
  size_t audio_events;
  size_t first_audio_bytes;
  size_t max_audio_bytes;
  int error;
  bool stopped;
  bool done;
  bkcloud_write_t write;
  void *context;
  bkcloud_tts_observe_t observe;
  void *observe_context;
};
void bkcloud_tts_init(struct bkcloud_tts_s *tts, bkcloud_write_t write, void *context);
void bkcloud_tts_set_observer(struct bkcloud_tts_s *tts,
                              bkcloud_tts_observe_t observe, void *context);
int bkcloud_tts_feed(void *context, const void *data, size_t size);
int bkcloud_tts_finish(struct bkcloud_tts_s *tts);
void bkcloud_tts_clear(struct bkcloud_tts_s *tts);
#endif
